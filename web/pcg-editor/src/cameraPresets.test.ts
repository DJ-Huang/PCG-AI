import { describe, expect, it } from 'vitest';

import {
  addShotCamera,
  applyShotCameraPreset,
  shotCamerasToFlow,
} from './cameraGraph';
import {
  applyCameraPreset,
  CAMERA_PRESETS,
  DEFAULT_CAMERA_PRESET_ID,
  getCameraPreset,
  inferCameraPresetId,
} from './cameraPresets';
import { getAllNodeTypes, getNodesByCategory } from './nodeManifest';
import { defaultPhysicalCamera } from './physicalCamera';
import { createDefaultShot, normalizeShotDocument } from './shot';
import { upsertCameraKeyframe } from './cameraTrack';

describe('camera presets', () => {
  it('maps every catalog body to the published sensor size', () => {
    expect(CAMERA_PRESETS.length).toBeGreaterThanOrEqual(15);
    for (const preset of CAMERA_PRESETS) {
      const applied = applyCameraPreset(defaultPhysicalCamera(), preset.id);
      expect(applied.sensorWidthMm).toBeCloseTo(preset.sensorWidthMm, 5);
      expect(applied.sensorHeightMm).toBeCloseTo(preset.sensorHeightMm, 5);
      expect(applied.focalLengthMm).toBe(preset.defaultFocalLengthMm);
      expect(applied.sensorFit).toBe('auto');
    }
  });

  it('does not wipe pose, aperture, or DOF when applying a body', () => {
    const posed = {
      ...defaultPhysicalCamera(),
      position: [9, 4, 2] as [number, number, number],
      target: [1, 0, 0] as [number, number, number],
      apertureFstop: 2.8,
      dofEnabled: true,
      focusDistance: 12,
    };
    const next = applyCameraPreset(posed, 'arri_alexa_35');
    expect(next.position).toEqual([9, 4, 2]);
    expect(next.target).toEqual([1, 0, 0]);
    expect(next.apertureFstop).toBe(2.8);
    expect(next.dofEnabled).toBe(true);
    expect(next.focusDistance).toBe(12);
    expect(next.sensorWidthMm).toBeCloseTo(27.99, 5);
  });

  it('creates a camera station from a catalog id without touching other tracks', () => {
    const keyed = upsertCameraKeyframe(createDefaultShot(), {
      id: 'wide',
      timeSeconds: 0,
      interpolation: 'linear',
      value: { position: [4, 2, 1] },
    });
    const next = addShotCamera(keyed, { x: 400, y: 160 }, 'arri_alexa_35');
    const added = next.cameras[1];
    expect(added.presetId).toBe('arri_alexa_35');
    expect(added.name).toBe('ARRI Alexa 35');
    expect(added.camera.sensorWidthMm).toBeCloseTo(27.99, 5);
    expect(added.camera.sensorHeightMm).toBeCloseTo(19.22, 5);
    expect(next.cameras[0].cameraKeyframes).toHaveLength(1);
    expect(next.cameraKeyframes).toEqual([]);
    const flow = shotCamerasToFlow(next);
    expect(flow.nodes.find((node) => node.id === added.id)?.data).toMatchObject({
      bodyName: 'ARRI Alexa 35',
      presetId: 'arri_alexa_35',
    });
  });

  it('keeps keyframes when switching body and only patches sensor fields on keys', () => {
    const keyed = upsertCameraKeyframe(createDefaultShot(), {
      id: 'move',
      timeSeconds: 1,
      interpolation: 'linear',
      value: { position: [2, 1, 0], focalLengthMm: 85 },
    });
    const next = applyShotCameraPreset(keyed, keyed.activeCameraId, 'red_komodo_6k');
    expect(next.cameraKeyframes).toHaveLength(1);
    expect(next.cameraKeyframes[0].value.position).toEqual([2, 1, 0]);
    expect(next.cameraKeyframes[0].value.focalLengthMm).toBe(85);
    expect(next.cameraKeyframes[0].value.sensorWidthMm).toBeCloseTo(27.03, 5);
    expect(next.cameras[0].presetId).toBe('red_komodo_6k');
    expect(next.camera.position).toEqual(keyed.camera.position);
  });

  it('fills missing presetId from sensor size on load', () => {
    const shot = normalizeShotDocument({
      cameras: [{
        id: 'cam_a',
        name: 'Hero',
        camera: { sensorWidthMm: 36.2, sensorHeightMm: 24.1 },
      }],
    });
    expect(shot.cameras[0].presetId).toBe('sony_venice_2');
    expect(inferCameraPresetId(defaultPhysicalCamera())).toBe(DEFAULT_CAMERA_PRESET_ID);
    expect(getCameraPreset('full_frame')?.sensorWidthMm).toBe(36);
  });

  it('never lists Camera or ShotOutput in the Graph node search catalog', () => {
    const types = getAllNodeTypes().map((node) => node.type);
    expect(types).not.toContain('Camera');
    expect(types).not.toContain('ShotOutput');
    const categories = [...getNodesByCategory().keys()];
    expect(categories.some((category) => category.toLowerCase() === 'camera')).toBe(false);
  });
});
