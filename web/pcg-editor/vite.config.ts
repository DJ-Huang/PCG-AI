import path from 'node:path';
import fs from 'node:fs';
import http from 'node:http';
import { exec } from 'node:child_process';
import { defineConfig, type Plugin } from 'vite';
import react from '@vitejs/plugin-react';

const SCHEMA_EXPORT = path.resolve(__dirname, '../../schema/editor-export.pcg');
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
    },
  };
}

function exportGraphPlugin(): Plugin {
  return {
    name: 'pcg-export-graph',
    configureServer(server) {
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
            // Resolve relative to workspace root (parent of web/)
            const resolved = path.resolve(__dirname, '..', filePath);
            fs.mkdirSync(path.dirname(resolved), { recursive: true });
            fs.writeFileSync(resolved, JSON.stringify(graphData, null, 2), 'utf8');
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
            const filePath = parsed.filePath;
            if (!filePath) {
              res.statusCode = 400;
              res.end(JSON.stringify({ ok: false, error: 'Missing filePath' }));
              return;
            }
            const resolved = path.resolve(__dirname, '..', filePath);
            exec(`open -R "${resolved}"`, (err) => {
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
    },
  };
}

// https://vite.dev/config/
export default defineConfig({
  plugins: [react(), exportGraphPlugin(), cookProxyPlugin()],
});
