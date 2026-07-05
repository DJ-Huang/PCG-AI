import path from 'node:path';
import fs from 'node:fs';
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
    },
  };
}

// https://vite.dev/config/
export default defineConfig({
  plugins: [react(), exportGraphPlugin()],
});
