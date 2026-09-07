import { describe, expect, it } from 'vitest';

import { createDefaultShot, normalizeShotDocument } from './shot';

describe('shot document', () => {
  it('creates a deterministic single-shot default', () => {
    const shot = createDefaultShot('Crosswalk');
    expect(shot).toMatchObject({
      version: '1.0',
      name: 'Crosswalk',
      durationSeconds: 5,
      fps: 24,
      width: 1920,
      height: 1080,
    });
    expect(shot.cameraKeyframes).toEqual([]);
    expect(shot.cameras).toHaveLength(1);
    expect(shot.activeCameraId).toBe('cam_a');
    expect(shot.cameras[0].presetId).toBe('full_frame');
    expect(shot.motionCurves).toEqual([]);
  });

  it('normalizes timing, components and camera keys at the trust boundary', () => {
    const shot = normalizeShotDocument({
      name: '  Dolly  ',
      durationSeconds: 5,
      fps: 240,
      width: 1920.4,
      components: [
        { componentId: 'hero', transform: { scale: [1, 0, 2] } },
        { componentId: 'hero' },
        { componentId: '' },
      ],
      objectAnimations: [
        { componentId: 'hero', clip: 'Walk', startSeconds: -1, playbackRate: 0, loop: true },
      ],
      cameraKeyframes: [
        { timeSeconds: 9, interpolation: 'ease-in-out', value: { focalLengthMm: 85 } },
        { timeSeconds: 0, interpolation: 'invalid', value: { focalLengthMm: 35 } },
      ],
    });

    expect(shot.name).toBe('Dolly');
    expect(shot.fps).toBe(120);
    expect(shot.width).toBe(1920);
    expect(shot.components).toHaveLength(1);
    expect(shot.components[0].transform.scale).toEqual([1, 0.001, 2]);
    expect(shot.objectAnimations[0]).toMatchObject({ startSeconds: 0, playbackRate: 0.01 });
    expect(shot.cameraKeyframes.map((key) => key.timeSeconds)).toEqual([0, 5]);
    expect(shot.cameraKeyframes[0].interpolation).toBe('linear');
  });
});
