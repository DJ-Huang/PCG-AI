import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { createServer, type IncomingMessage, type ServerResponse } from 'node:http';
import type { ViteDevServer } from 'vite';
import { expect, it } from 'vitest';
import { registerDocumentFileRoutes } from './documentFileRoutes';
import { createDefaultShot } from './src/shot';
import { applyShotOperations } from './src/shotOperations';
import { parseShotFile } from './src/shotFile';
import { sampleShotCameraWorld } from './src/cameraPath';

it('exports a reusable project without browser downloads and preserves camera animation on import', async () => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'picg-document-'));
  const routes = new Map<string, (req: IncomingMessage, res: ServerResponse, next: () => void) => void>();
  registerDocumentFileRoutes({ middlewares: { use: (name: string, handler: typeof routes extends Map<string, infer T> ? T : never) => routes.set(name, handler) } } as unknown as Pick<ViteDevServer, 'middlewares'>, root);
  const server = createServer((req, res) => routes.get(req.url!.split('?')[0])!(req, res, () => res.end()));
  await new Promise<void>((resolve) => server.listen(0, '127.0.0.1', resolve));
  const base = `http://127.0.0.1:${(server.address() as { port: number }).port}`;
  try {
    const shot = applyShotOperations(createDefaultShot(), [
      { op: 'upsert_motion_curve', id: 'rail', cameraId: 'cam_a', controlPoints: [[2, 1, 4], [0, 1, 0]] },
      { op: 'set_transform', cameraId: 'cam_a', translation: [1, 0, 0] },
      { op: 'set_keyframes', nodeId: 'rail', keyframes: [
        { id: 'a', timeSeconds: 0, value: { pathProgress: 0, focalLengthMm: 24 } },
        { id: 'b', timeSeconds: 5, value: { pathProgress: 1, focalLengthMm: 60 } },
      ] },
    ]);
    const document = { format: 'PICG-project', graph: { version: '1.0', nodes: [], edges: [] }, shot };
    const saved = await (await fetch(`${base}/api/export-document?name=镜头.picgproject`, { method: 'POST', body: JSON.stringify(document) })).json() as { path: string; url: string };
    expect(JSON.parse(fs.readFileSync(path.join(root, saved.path), 'utf8'))).toEqual(document);
    const downloaded = await (await fetch(`${base}${saved.url}`)).json();
    const restored = parseShotFile(JSON.stringify(downloaded.shot));
    expect(sampleShotCameraWorld(restored, 'cam_a', 2.5)).toEqual(sampleShotCameraWorld(shot, 'cam_a', 2.5));
    expect((await fetch(`${base}/api/exported-document?name=..%2Fprivate.picgshot`)).status).toBe(404);
    expect((await fetch(`${base}/api/export-document?name=invalid.picgproject`, { method: 'POST', body: '{}' })).status).toBe(400);
  } finally {
    await new Promise<void>((resolve) => server.close(() => resolve()));
    fs.rmSync(root, { recursive: true, force: true });
  }
});
