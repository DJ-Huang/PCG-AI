import { describe, expect, it } from 'vitest';

import { createCameraPresetKeyframes } from './cameraMotion';
import {
  cameraTrajectoryPoints,
  findKeyframeAtTime,
  moveCameraKeyframeTime,
  removeCameraKeyframe,
  replaceCameraKeyframes,
  upsertCameraKeyframe,
} from './cameraTrack';
import { defaultPhysicalCamera } from './physicalCamera';
import { createDefaultShot } from './shot';

describe('cameraTrack', () => {
  it('upserts a key at the same time instead of duplicating it', () => {
    const shot = createDefaultShot();
    const first = upsertCameraKeyframe(shot, {
      id: 'a',
      timeSeconds: 1,
      interpolation: 'linear',
      value: { focalLengthMm: 35 },
    });
    const second = upsertCameraKeyframe(first, {
      timeSeconds: 1,
      interpolation: 'ease-in-out',
      value: { focalLengthMm: 50, position: [1, 2, 3] },
    });
    expect(second.cameraKeyframes).toHaveLength(1);
    expect(second.cameraKeyframes[0].id).toBe('a');
    expect(second.cameraKeyframes[0].value.focalLengthMm).toBe(50);
    expect(second.cameraKeyframes[0].value.position).toEqual([1, 2, 3]);
  });

  it('moves, finds, and removes keyframes', () => {
    const shot = replaceCameraKeyframes(createDefaultShot(), [
      { id: 'start', timeSeconds: 0, interpolation: 'linear', value: { position: [0, 1, 4] } },
      { id: 'end', timeSeconds: 5, interpolation: 'linear', value: { position: [2, 1, 0] } },
    ]);
    expect(findKeyframeAtTime(shot, 0)?.id).toBe('start');
    const moved = moveCameraKeyframeTime(shot, 'end', 3.25);
    expect(moved.cameraKeyframes.map((key) => key.timeSeconds)).toEqual([0, 3.25]);
    expect(removeCameraKeyframe(moved, 'start').cameraKeyframes).toHaveLength(1);
  });

  it('builds a trajectory that passes through every keyed camera position', () => {
    const camera = defaultPhysicalCamera();
    const shot = {
      ...createDefaultShot(),
      camera,
      cameraKeyframes: createCameraPresetKeyframes('dolly', camera, 5),
    };
    const points = cameraTrajectoryPoints(shot, 4);
    expect(points[0].keyframeId).toBe(shot.cameraKeyframes[0].id);
    expect(points.some((point) => point.keyframeId === shot.cameraKeyframes.at(-1)?.id)).toBe(true);
    expect(points.length).toBeGreaterThan(shot.cameraKeyframes.length);
    expect(points[0].position).not.toEqual(points[points.length - 1].position);
  });
});
