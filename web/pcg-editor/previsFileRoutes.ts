import fs from 'node:fs';
import path from 'node:path';
import { randomUUID } from 'node:crypto';
import type { ViteDevServer } from 'vite';
import { assertWorkspaceGraphParent } from './devFileSecurity.js';

export function previsFilePath(root: string, name: string): string {
  if (!/^[\p{L}\p{N}_-]+\.(mp4|webm)$/u.test(name)) throw new Error('Invalid previs video name');
  const file = path.join(root, 'exports/previs', name);
  assertWorkspaceGraphParent(root, file);
  return file;
}

export function registerPrevisFileRoutes(server: Pick<ViteDevServer, 'middlewares'>, root: string) {
  server.middlewares.use('/api/save-previs', (req, res, next) => {
    if (req.method !== 'POST') { next(); return; }
    const chunks: Buffer[] = []; let size = 0;
    req.on('data', (chunk: Buffer) => { size += chunk.length; if (size <= 512 * 1024 * 1024) chunks.push(chunk); });
    req.on('end', () => {
      res.setHeader('Content-Type', 'application/json');
      try {
        if (!size || size > 512 * 1024 * 1024) throw new Error('Video must be between 1 byte and 512 MB');
        const input = new URL(req.url ?? '', 'http://localhost').searchParams.get('name') ?? '';
        const extension = path.extname(input).toLowerCase();
        if (extension !== '.mp4' && extension !== '.webm') throw new Error('Choose MP4 or WebM');
        const stem = path.basename(input, extension).replace(/[^\p{L}\p{N}_-]/gu, '_').slice(0, 100) || 'shot';
        const name = `${stem}-${Date.now()}-${randomUUID().slice(0, 8)}${extension}`;
        fs.mkdirSync(path.join(root, 'exports/previs'), { recursive: true });
        const file = previsFilePath(root, name);
        fs.writeFileSync(file, Buffer.concat(chunks), { flag: 'wx' });
        res.end(JSON.stringify({ ok: true, path: `exports/previs/${name}`, url: `/api/previs-file?name=${encodeURIComponent(name)}` }));
      } catch (error) { res.statusCode = 400; res.end(JSON.stringify({ ok: false, error: String(error) })); }
    });
  });
  server.middlewares.use('/api/previs-file', (req, res, next) => {
    if (req.method !== 'GET' && req.method !== 'HEAD') { next(); return; }
    try {
      const name = new URL(req.url ?? '', 'http://localhost').searchParams.get('name') ?? '';
      const file = previsFilePath(root, name);
      const size = fs.statSync(file).size;
      const range = req.headers.range?.match(/^bytes=(\d+)-(\d*)$/);
      const start = range ? Number(range[1]) : 0;
      const end = range && range[2] ? Math.min(Number(range[2]), size - 1) : size - 1;
      if (start > end || start >= size) { res.statusCode = 416; res.setHeader('Content-Range', `bytes */${size}`); res.end(); return; }
      res.setHeader('Content-Type', name.endsWith('.mp4') ? 'video/mp4' : 'video/webm');
      res.setHeader('Accept-Ranges', 'bytes');
      res.setHeader('Content-Length', end - start + 1);
      if (range) { res.statusCode = 206; res.setHeader('Content-Range', `bytes ${start}-${end}/${size}`); }
      if (req.method === 'HEAD') res.end();
      else fs.createReadStream(file, { start, end }).pipe(res);
    } catch { res.statusCode = 404; res.end('Previs video not found'); }
  });
}
