import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { createServer, type IncomingMessage, type ServerResponse } from 'node:http';
import type { ViteDevServer } from 'vite';
import { expect, it } from 'vitest';
import { previsFilePath, registerPrevisFileRoutes } from './previsFileRoutes';

it('saves a video locally, serves playback ranges and rejects paths outside the export folder', async () => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'picg-export-'));
  const routes = new Map<string, (req: IncomingMessage, res: ServerResponse, next: () => void) => void>();
  registerPrevisFileRoutes({ middlewares: { use: (name: string, handler: typeof routes extends Map<string, infer T> ? T : never) => routes.set(name, handler) } } as unknown as Pick<ViteDevServer, 'middlewares'>, root);
  const server = createServer((req, res) => routes.get(req.url!.split('?')[0])!(req, res, () => res.end()));
  await new Promise<void>((resolve) => server.listen(0, '127.0.0.1', resolve));
  const address = server.address() as { port: number };
  const base = `http://127.0.0.1:${address.port}`;
  try {
    const response = await fetch(`${base}/api/save-previs?name=shot.mp4`, { method: 'POST', body: new Uint8Array([0, 1, 2, 3, 4, 5]) });
    const saved = await response.json() as { ok: boolean; path: string; url: string };
    expect(saved.ok).toBe(true);
    expect(fs.readFileSync(path.join(root, saved.path))).toEqual(Buffer.from([0, 1, 2, 3, 4, 5]));
    const segment = await fetch(`${base}${saved.url}`, { headers: { Range: 'bytes=1-3' } });
    expect(segment.status).toBe(206);
    expect(segment.headers.get('content-range')).toBe('bytes 1-3/6');
    expect(new Uint8Array(await segment.arrayBuffer())).toEqual(new Uint8Array([1, 2, 3]));
    expect((await fetch(`${base}${saved.url}`, { headers: { Range: 'bytes=99-' } })).status).toBe(416);
    expect(() => previsFilePath(root, '../private.mp4')).toThrow('Invalid');
    expect((await fetch(`${base}/api/previs-file?name=..%2Fprivate.mp4`)).status).toBe(404);
  } finally {
    await new Promise<void>((resolve) => server.close(() => resolve()));
    fs.rmSync(root, { recursive: true, force: true });
  }
});
