import { describe, expect, it } from 'vitest';

import {
  CAMERA_MOTION_PRESETS,
  createCameraPresetKeyframes,
  sampleCameraTrack,
} from './cameraMotion';
import { defaultPhysicalCamera } from './physicalCamera';
import type { ShotCameraKeyframe } from './shot';

describe('cameraMotion', () => {
  it('samples camera pose, lens, focus, and shift deterministically', () => {
    const camera = defaultPhysicalCamera();
    const keys: ShotCameraKeyframe[] = [
      {
        id: 'end',
        timeSeconds: 4,
        interpolation: 'linear',
        value: {
          position: [1, 2, 3],
          target: [0, 1, 0],
          focalLengthMm: 70,
          focusDistance: 2,
          apertureFstop: 2.8,
          shiftX: 0.2,
          shiftY: -0.1,
        },
      },
    ];
    const first = sampleCameraTrack(camera, keys, 2, 16 / 9);
    const second = sampleCameraTrack(camera, keys, 2, 16 / 9);
    expect(first).toEqual(second);
    expect(first.position).toEqual([2, 2.25, 3.5]);
    expect(first.focalLengthMm).toBe(48);
    expect(first.focusDistance).toBeCloseTo(3.85);
    expect(first.shiftX).toBeCloseTo(0.1);
    expect(first.shiftY).toBeCloseTo(-0.05);
  });

  it('uses destination interpolation and resolves duplicate times predictably', () => {
    const camera = defaultPhysicalCamera();
    const keys: ShotCameraKeyframe[] = [
      { id: 'first', timeSeconds: 2, interpolation: 'ease-in', value: { focalLengthMm: 50 } },
      { id: 'replace', timeSeconds: 2, interpolation: 'linear', value: { focalLengthMm: 60 } },
    ];
    expect(sampleCameraTrack(camera, keys, 1, 1).focalLengthMm).toBe(43);
    expect(sampleCameraTrack(camera, keys, 2, 1).focalLengthMm).toBe(60);
  });

  it('steps discrete lens fields instead of interpolating them', () => {
    const camera = { ...defaultPhysicalCamera(), apertureBlades: 5, dofEnabled: false };
    const keys: ShotCameraKeyframe[] = [
      {
        id: 'end',
        timeSeconds: 4,
        interpolation: 'linear',
        value: { apertureBlades: 8, dofEnabled: true, projection: 'orthographic' },
      },
    ];
    const mid = sampleCameraTrack(camera, keys, 2, 16 / 9);
    expect(mid.apertureBlades).toBe(5);
    expect(mid.dofEnabled).toBe(false);
    expect(mid.projection).toBe('perspective');
    const end = sampleCameraTrack(camera, keys, 4, 16 / 9);
    expect(end.apertureBlades).toBe(8);
    expect(end.dofEnabled).toBe(true);
    expect(end.projection).toBe('orthographic');
  });

  it('generates editable keyframes for every motion preset', () => {
    const camera = defaultPhysicalCamera();
    for (const preset of CAMERA_MOTION_PRESETS) {
      const keys = createCameraPresetKeyframes(preset, camera, 5);
      expect(keys.length).toBeGreaterThanOrEqual(2);
      expect(keys[0].timeSeconds).toBe(0);
      expect(keys.at(-1)?.timeSeconds).toBe(5);
      expect(keys.every((key) => key.id && key.value)).toBe(true);
    }
  });

  it('repeats all 120 samples of a five-second 24fps dolly and focus pull', () => {
    const camera = defaultPhysicalCamera();
    const keys = createCameraPresetKeyframes('dolly', camera, 5);
    keys[1].value.focusDistance = 2;
    const renderA = Array.from({ length: 120 }, (_, frame) =>
      sampleCameraTrack(camera, keys, frame / 24, 16 / 9));
    const renderB = Array.from({ length: 120 }, (_, frame) =>
      sampleCameraTrack(camera, keys, frame / 24, 16 / 9));
    expect(renderA).toEqual(renderB);
    expect(renderA[0].position).not.toEqual(renderA[119].position);
    expect(renderA[0].focusDistance).toBeGreaterThan(renderA[119].focusDistance);
  });
});
