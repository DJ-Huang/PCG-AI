import path from 'node:path';
import fs from 'node:fs';
import http from 'node:http';
import { execFile } from 'node:child_process';
import { defineConfig, type Plugin } from 'vite';
import react from '@vitejs/plugin-react';
import { registerPrevisFileRoutes } from './previsFileRoutes.js';
import { registerDocumentFileRoutes } from './documentFileRoutes.js';
import {
  assertWorkspaceGraphParent,
  resolveExistingWorkspaceGraphPath,
  resolveWorkspaceGraphPath,
} from './devFileSecurity.js';

const SCHEMA_EXPORT = path.resolve(__dirname, '../../schema/editor-export.pcg');
const WORKSPACE_ROOT = path.resolve(__dirname, '../..');
const PCG_SERVER_PORT = Number(process.env.PCG_SERVER_PORT) || 17890;
const PCG_SERVER_HOST = '127.0.0.1';

function proxyToPcgServer(
  req: http.IncomingMessage,
  res: http.ServerResponse,
  serverPath: string,
) {
  const headers: Record<string, string> = {
    'content-type': req.headers['content-type'] ?? 'application/octet-stream',
  };
  if (req.headers['content-length'] !== undefined) {
    headers['content-length'] = req.headers['content-length'];
  }
  if (req.headers.authorization) {
    headers.authorization = req.headers.authorization;
  }
  if (req.headers.accept) {
    headers.accept = req.headers.accept;
  }
  const upstream = http.request(
    {
      host: PCG_SERVER_HOST,
      port: PCG_SERVER_PORT,
      path: serverPath,
      method: req.method,
      headers,
      timeout: 600_000,
    },
    (up) => {
      res.statusCode = up.statusCode ?? 502;
      if (up.headers['content-type']) {
        res.setHeader('Content-Type', up.headers['content-type']);
      }
      up.pipe(res);
    },
  );
  upstream.on('timeout', () => {
    upstream.destroy(new Error('pcg-server request timed out'));
  });
  upstream.on('error', (err) => {
    res.statusCode = 502;
    res.setHeader('Content-Type', 'application/json');
    res.end(
      JSON.stringify({
        ok: false,
        error: `pcg-server unreachable at ${PCG_SERVER_HOST}:${PCG_SERVER_PORT} (${err.message}). Start it with scripts/run-pcg-server.sh.`,
      }),
    );
  });
  req.pipe(upstream);
}

function cookProxyPlugin(): Plugin {
  return {
    name: 'pcg-cook-proxy',
    configureServer(server) {
      server.middlewares.use('/api/cook', (req, res, next) => {
        if (req.method !== 'POST') {
          next();
          return;
        }
        proxyToPcgServer(req, res, '/v1/cook');
      });
      server.middlewares.use('/api/cook-cancel', (req, res, next) => {
        if (req.method !== 'POST') {
          next();
          return;
        }
        proxyToPcgServer(req, res, '/v1/cancel');
      });
      server.middlewares.use('/api/cook-health', (req, res, next) => {
        if (req.method !== 'GET') {
          next();
          return;
        }
        proxyToPcgServer(req, res, '/v1/health');
      });
      server.middlewares.use('/api/agent', (req, res, next) => {
        if (!['GET', 'POST', 'PUT', 'DELETE'].includes(req.method ?? '')) {
          next();
          return;
        }
        proxyToPcgServer(req, res, `/v1/agent${req.url ?? ''}`);
      });
      server.middlewares.use('/api/editor-bridge', (req, res, next) => {
        if (!['GET', 'PUT', 'POST', 'PATCH', 'DELETE'].includes(req.method ?? '')) {
          next();
          return;
        }
        proxyToPcgServer(req, res, `/v1${req.url ?? ''}`);
      });
    },
  };
}

function exportGraphPlugin(): Plugin {
  return {
    name: 'pcg-export-graph',
    configureServer(server) {
      registerPrevisFileRoutes(server, WORKSPACE_ROOT);
      registerDocumentFileRoutes(server, WORKSPACE_ROOT);
      server.middlewares.use('/api/export-graph', (req, res, next) => {
        if (req.method !== 'POST') {
          next();
          return;
        }

        let body = '';
        req.on('data', (chunk: Buffer) => {
          body += chunk.toString('utf8');
        });
        req.on('end', () => {
          try {
            const parsed = JSON.parse(body);
            fs.mkdirSync(path.dirname(SCHEMA_EXPORT), { recursive: true });
            fs.writeFileSync(SCHEMA_EXPORT, JSON.stringify(parsed, null, 2), 'utf8');
            res.setHeader('Content-Type', 'application/json');
            res.end(JSON.stringify({ ok: true, path: SCHEMA_EXPORT }));
          } catch (err) {
            res.statusCode = 400;
            res.end(JSON.stringify({ ok: false, error: String(err) }));
          }
        });
      });

      // Save graph to a specific file path (for Save button)
      server.middlewares.use('/api/save-graph', (req, res, next) => {
        if (req.method !== 'POST') {
          next();
          return;
        }

        let body = '';
        req.on('data', (chunk: Buffer) => {
          body += chunk.toString('utf8');
        });
        req.on('end', () => {
          try {
            const parsed = JSON.parse(body);
            const filePath = parsed.filePath;
            const graphData = parsed.graphData;
            if (!filePath || !graphData) {
              res.statusCode = 400;
              res.end(JSON.stringify({ ok: false, error: 'Missing filePath or graphData' }));
              return;
            }
            const resolved = resolveWorkspaceGraphPath(WORKSPACE_ROOT, filePath);
            fs.mkdirSync(path.dirname(resolved), { recursive: true });
            assertWorkspaceGraphParent(WORKSPACE_ROOT, resolved);
            fs.writeFileSync(resolved, JSON.stringify(graphData, null, 2), 'utf8');
            if (parsed.shotData) {
              const shotPath = resolved.replace(/\.(picg|pcg)$/i, '.picgshot');
              assertWorkspaceGraphParent(WORKSPACE_ROOT, shotPath);
              const temporary = `${shotPath}.${process.pid}.tmp`;
              fs.writeFileSync(temporary, JSON.stringify(parsed.shotData, null, 2), { encoding: 'utf8', flag: 'wx' });
              fs.renameSync(temporary, shotPath);
            }
            res.setHeader('Content-Type', 'application/json');
            res.end(JSON.stringify({ ok: true, path: resolved }));
          } catch (err) {
            res.statusCode = 400;
            res.end(JSON.stringify({ ok: false, error: String(err) }));
          }
        });
      });

      // Reveal a file in Finder
      server.middlewares.use('/api/reveal-in-finder', (req, res, next) => {
        if (req.method !== 'POST') {
          next();
          return;
        }

        let body = '';
        req.on('data', (chunk: Buffer) => {
          body += chunk.toString('utf8');
        });
        req.on('end', () => {
          try {
            const parsed = JSON.parse(body);
            const resolved = resolveExistingWorkspaceGraphPath(WORKSPACE_ROOT, parsed.filePath);
            execFile('/usr/bin/open', ['-R', resolved], (err) => {
              if (err) {
                res.statusCode = 500;
                res.end(JSON.stringify({ ok: false, error: String(err) }));
              } else {
                res.setHeader('Content-Type', 'application/json');
                res.end(JSON.stringify({ ok: true }));
              }
            });
          } catch (err) {
            res.statusCode = 400;
            res.end(JSON.stringify({ ok: false, error: String(err) }));
          }
        });
      });

      // Upload a local image into public/assets/uploads/ (Tripo Source Image, …).
      // POST /api/upload-texture?name=<file.png>  (raw bytes body, ≤ 20 MB)
      // Returns { ok, storage: "pcg-resource://uploads/<file>" }.
      server.middlewares.use('/api/upload-texture', (req, res, next) => {
        if (req.method !== 'POST') {
          next();
          return;
        }
        const url = new URL(req.url ?? '', 'http://localhost');
        const rawName = path.basename(url.searchParams.get('name') ?? '');
        const safeName = rawName.replace(/[^A-Za-z0-9._-]/g, '_');
        if (!/\.(png|jpe?g)$/i.test(safeName)) {
          res.statusCode = 400;
          res.setHeader('Content-Type', 'application/json');
          res.end(JSON.stringify({ ok: false, error: 'Only .png / .jpg images are supported' }));
          return;
        }
        const chunks: Buffer[] = [];
        let total = 0;
        req.on('data', (chunk: Buffer) => {
          total += chunk.length;
          if (total <= 20 * 1024 * 1024) chunks.push(chunk);
        });
        req.on('end', () => {
          if (total > 20 * 1024 * 1024) {
            res.statusCode = 413;
            res.setHeader('Content-Type', 'application/json');
            res.end(JSON.stringify({ ok: false, error: 'Image exceeds the 20 MB limit' }));
            return;
          }
          try {
            const dir = path.resolve(__dirname, 'public/assets/uploads');
            fs.mkdirSync(dir, { recursive: true });
            const fileName = `${Date.now()}-${safeName}`;
            fs.writeFileSync(path.join(dir, fileName), Buffer.concat(chunks));
            res.setHeader('Content-Type', 'application/json');
            res.end(JSON.stringify({ ok: true, storage: `pcg-resource://uploads/${fileName}` }));
          } catch (err) {
            res.statusCode = 500;
            res.end(JSON.stringify({ ok: false, error: String(err) }));
          }
        });
      });

      server.middlewares.use('/api/load-shot', (req, res, next) => {
        if (req.method !== 'GET') { next(); return; }
        try {
          const url = new URL(req.url ?? '', 'http://localhost');
          const graphPath = resolveExistingWorkspaceGraphPath(WORKSPACE_ROOT, url.searchParams.get('path'));
          const shotPath = graphPath.replace(/\.(picg|pcg)$/i, '.picgshot');
          assertWorkspaceGraphParent(WORKSPACE_ROOT, shotPath);
          res.setHeader('Content-Type', 'application/json');
          res.end(fs.readFileSync(shotPath, 'utf8'));
        } catch (error) {
          res.statusCode = 404;
          res.end(JSON.stringify({ ok: false, error: String(error) }));
        }
      });

      // Load a .picg / legacy .pcg graph file from disk (for the /review route)
      // GET /api/load-graph?path=<relative-path>
      server.middlewares.use('/api/load-graph', (req, res, next) => {
        if (req.method !== 'GET') {
          next();
          return;
        }
        const url = new URL(req.url ?? '', 'http://localhost');
        const relPath = url.searchParams.get('path');
        if (!relPath) {
          res.statusCode = 400;
          res.setHeader('Content-Type', 'application/json');
          res.end(JSON.stringify({ ok: false, error: 'Missing "path" query parameter' }));
          return;
        }
        let resolved: string;
        try {
          resolved = resolveExistingWorkspaceGraphPath(WORKSPACE_ROOT, relPath);
        } catch (err) {
          res.statusCode = 404;
          res.setHeader('Content-Type', 'application/json');
          res.end(JSON.stringify({ ok: false, error: String(err) }));
          return;
        }
        try {
          const content = fs.readFileSync(resolved, 'utf8');
          res.setHeader('Content-Type', 'application/json');
          res.end(content);
        } catch (err) {
          res.statusCode = 500;
          res.setHeader('Content-Type', 'application/json');
          res.end(JSON.stringify({ ok: false, error: String(err) }));
        }
      });
    },
  };
}

// https://vite.dev/config/
export default defineConfig({
  plugins: [react(), exportGraphPlugin(), cookProxyPlugin()],
});
