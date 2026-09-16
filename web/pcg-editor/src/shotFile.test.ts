import { afterEach, describe, expect, it, vi } from 'vitest';
import { applyShotOperations } from './shotOperations';
import { createDefaultShot } from './shot';
import { loadWorkspaceProject } from './shotFile';
import { sampleShotCameraWorld } from './cameraPath';

afterEach(() => vi.unstubAllGlobals());

describe('PICG workspace project loading', () => {
  const graph = { version: '1.0', nodes: [], edges: [] };
  it('restores the camera sidecar with the graph and preserves sampled motion', async () => {
    const shot = applyShotOperations(createDefaultShot(), [
      { op: 'upsert_motion_curve', id: 'rail', cameraId: 'cam_a', controlPoints: [[0, 1, 4], [0, 1, 0]] },
      { op: 'set_transform', cameraId: 'cam_a', translation: [1, 0, 0] },
      { op: 'set_keyframes', nodeId: 'rail', keyframes: [
        { id: 'a', timeSeconds: 0, interpolation: 'linear', value: { pathProgress: 0, focalLengthMm: 24 } },
        { id: 'b', timeSeconds: 5, interpolation: 'linear', value: { pathProgress: 1, focalLengthMm: 60 } },
      ] },
    ]);
    const fetch = vi.fn().mockResolvedValueOnce(new Response(JSON.stringify(graph)))
      .mockResolvedValueOnce(new Response(JSON.stringify(shot)));
    vi.stubGlobal('fetch', fetch);
    const loaded = await loadWorkspaceProject('examples/my shot.picg');
    expect(fetch).toHaveBeenNthCalledWith(2, '/api/load-shot?path=examples%2Fmy%20shot.picg');
    expect(loaded.hasShot).toBe(true);
    expect(loaded.shot.graphPath).toBe('examples/my shot.picg');
    expect(sampleShotCameraWorld(loaded.shot, 'cam_a', 2.5)).toEqual(sampleShotCameraWorld(shot, 'cam_a', 2.5));
  });

  it('opens a legacy graph without a sidecar but refuses a corrupted camera file', async () => {
    const fetch = vi.fn().mockResolvedValueOnce(new Response(JSON.stringify(graph)))
      .mockResolvedValueOnce(new Response('', { status: 404 }));
    vi.stubGlobal('fetch', fetch);
    expect((await loadWorkspaceProject('old.pcg')).hasShot).toBe(false);
    fetch.mockResolvedValueOnce(new Response(JSON.stringify(graph)))
      .mockResolvedValueOnce(new Response('{"broken":true}'));
    await expect(loadWorkspaceProject('broken.picg')).rejects.toThrow('PICG shot');
  });
});
