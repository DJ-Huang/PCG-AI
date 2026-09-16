import fs from 'node:fs';
import path from 'node:path';
import { randomUUID } from 'node:crypto';
import type { ViteDevServer } from 'vite';
import { assertWorkspaceGraphParent } from './devFileSecurity.js';

function documentPath(root: string, name: string): string {
  if (!/^[\p{L}\p{N}_-]+\.(picgproject|picgshot)$/u.test(name)) throw new Error('Invalid document name');
  const file = path.join(root, 'exports/documents', name);
  assertWorkspaceGraphParent(root, file);
  return file;
}

export function registerDocumentFileRoutes(server: Pick<ViteDevServer, 'middlewares'>, root: string) {
  server.middlewares.use('/api/export-document', (req, res, next) => {
    if (req.method !== 'POST') { next(); return; }
    const chunks: Buffer[] = []; let size = 0;
    req.on('data', (chunk: Buffer) => { size += chunk.length; if (size <= 32 * 1024 * 1024) chunks.push(chunk); });
    req.on('end', () => {
      res.setHeader('Content-Type', 'application/json');
      try {
        if (!size || size > 32 * 1024 * 1024) throw new Error('Document must be between 1 byte and 32 MB');
        const input = new URL(req.url ?? '', 'http://localhost').searchParams.get('name') ?? '';
        const extension = path.extname(input).toLowerCase();
        if (extension !== '.picgproject' && extension !== '.picgshot') throw new Error('Choose a PICG project or shot');
        const body = Buffer.concat(chunks);
        const document = JSON.parse(body.toString('utf8'));
        const shot = extension === '.picgproject' ? document.shot : document;
        if (extension === '.picgproject' && (document.format !== 'PICG-project' || !Array.isArray(document.graph?.nodes) || !Array.isArray(document.graph?.edges))) throw new Error('Invalid PICG project');
        if (!Array.isArray(shot?.cameras) || !Number.isFinite(shot.durationSeconds)) throw new Error('Invalid PICG shot');
        const stem = path.basename(input, extension).replace(/[^\p{L}\p{N}_-]/gu, '_').slice(0, 100) || 'project';
        const name = `${stem}-${Date.now()}-${randomUUID().slice(0, 8)}${extension}`;
        fs.mkdirSync(path.join(root, 'exports/documents'), { recursive: true });
        fs.writeFileSync(documentPath(root, name), body, { flag: 'wx' });
        res.end(JSON.stringify({ ok: true, path: `exports/documents/${name}`, url: `/api/exported-document?name=${encodeURIComponent(name)}` }));
      } catch (error) { res.statusCode = 400; res.end(JSON.stringify({ ok: false, error: String(error) })); }
    });
  });
  server.middlewares.use('/api/exported-document', (req, res, next) => {
    if (req.method !== 'GET') { next(); return; }
    try {
      const name = new URL(req.url ?? '', 'http://localhost').searchParams.get('name') ?? '';
      const body = fs.readFileSync(documentPath(root, name));
      res.setHeader('Content-Type', 'application/json');
      res.setHeader('Content-Disposition', `attachment; filename*=UTF-8''${encodeURIComponent(name)}`);
      res.end(body);
    } catch { res.statusCode = 404; res.end('PICG document not found'); }
  });
}
