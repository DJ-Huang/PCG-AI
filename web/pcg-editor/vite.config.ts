import path from 'node:path';
import fs from 'node:fs';
import { exec } from 'node:child_process';
import { defineConfig, type Plugin } from 'vite';
import react from '@vitejs/plugin-react';

const SCHEMA_EXPORT = path.resolve(__dirname, '../../schema/editor-export.pcg');

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
  plugins: [react(), exportGraphPlugin()],
});
