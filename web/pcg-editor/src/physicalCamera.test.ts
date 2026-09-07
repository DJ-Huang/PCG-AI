import { describe, expect, it } from 'vitest';
import * as THREE from 'three';

import {
  apertureToBokehUniform,
  applyPhysicalProjection,
  axialFocusDistance,
  blenderProjection,
  blenderVerticalFovToFocalLength,
  defaultPhysicalCamera,
  focalLengthToFov,
  fovToFocalLength,
  mergeCameraCommand,
  sphericalPosition,
  syncStateFromLiveCamera,
} from './physicalCamera';

describe('physicalCamera', () => {
  it('converts focal length and fov round-trip', () => {
    expect(focalLengthToFov(50, 24)).toBeCloseTo(26.99, 1);
    expect(focalLengthToFov(26, 24)).toBeCloseTo(49.5, 0.5);
    expect(fovToFocalLength(focalLengthToFov(35), 24)).toBeCloseTo(35, 6);
    expect(fovToFocalLength(50, 24)).toBeCloseTo(25.76, 1);
  });

  it('maps aperture to the bokeh uniform with 50mm f/1.4 ≈ 0.025', () => {
    expect(apertureToBokehUniform(50, 1.4)).toBeCloseTo(0.025, 3);
    expect(apertureToBokehUniform(50, 8)).toBeLessThan(apertureToBokehUniform(50, 2));
    expect(apertureToBokehUniform(400, 0.7)).toBeLessThanOrEqual(0.05);
  });

  it('places the camera with spherical coordinates around the target', () => {
    const position = sphericalPosition([0, 0, 0], 0, 0, 10);
    expect(position[0]).toBeCloseTo(0);
    expect(position[1]).toBeCloseTo(0);
    expect(position[2]).toBeCloseTo(10);
    const elevated = sphericalPosition([1, 2, 3], 90, 90, 5);
    expect(elevated[0]).toBeCloseTo(1, 1);
    expect(elevated[1]).toBeCloseTo(7);
    expect(elevated[2]).toBeCloseTo(3, 1);
  });

  it('merges a command, keeping untouched fields and clamping ranges', () => {
    const base = defaultPhysicalCamera();
    const merged = mergeCameraCommand(base, {
      focalLengthMm: 85,
      apertureFstop: 1.2,
      dofEnabled: true,
      position: [1, 2, 3],
      near: 0.000000001,
    });
    expect(merged.focalLengthMm).toBe(85);
    expect(merged.apertureFstop).toBe(1.2);
    expect(merged.dofEnabled).toBe(true);
    expect(merged.position).toEqual([1, 2, 3]);
    expect(merged.target).toEqual(base.target);
    expect(merged.near).toBeGreaterThan(0);
  });

  it('derives focal length from a fov command', () => {
    const base = defaultPhysicalCamera();
    const merged = mergeCameraCommand(base, { fov: 50 });
    expect(merged.focalLengthMm).toBeCloseTo(
      blenderVerticalFovToFocalLength(base, 50, 16 / 9),
      6,
    );
  });

  it('keeps an explicit orthographic semantic frame height', () => {
    const merged = mergeCameraCommand(defaultPhysicalCamera(), {
      projection: 'orthographic', orthographicFrustumHeight: 7.5,
    });
    expect(merged.orthographicScale).toBe(7.5);
  });

  it('matches Blender horizontal and vertical sensor-fit framing', () => {
    const base = defaultPhysicalCamera();
    const horizontal = blenderProjection({ ...base, sensorFit: 'horizontal', focalLengthMm: 50 }, 16 / 9);
    expect(horizontal.verticalFovDeg).toBeCloseTo(22.895, 3);
    expect(horizontal.resolvedSensorFit).toBe('horizontal');

    const vertical = blenderProjection({ ...base, sensorFit: 'vertical', focalLengthMm: 50 }, 16 / 9);
    expect(vertical.verticalFovDeg).toBeCloseTo(26.991, 3);
    expect(vertical.resolvedSensorFit).toBe('vertical');
    expect(blenderVerticalFovToFocalLength(
      { ...base, sensorFit: 'horizontal' },
      horizontal.verticalFovDeg,
      16 / 9,
    )).toBeCloseTo(50, 6);
  });

  it('matches Blender AUTO portrait fit and lens-shift scaling', () => {
    const projection = blenderProjection({
      ...defaultPhysicalCamera(),
      sensorFit: 'auto',
      shiftX: 0.1,
      shiftY: -0.2,
    }, 9 / 16);
    expect(projection.resolvedSensorFit).toBe('vertical');
    // AUTO still uses Blender's sensor width while resolving the fit vertically.
    expect(projection.verticalFovDeg).toBeCloseTo(focalLengthToFov(26, 36), 6);
    expect(projection.shiftNdc[0]).toBeCloseTo(0.2 / (9 / 16), 6);
    expect(projection.shiftNdc[1]).toBeCloseTo(-0.4, 6);
  });

  it('moves the camera spherically while preserving unspecified angles', () => {
    const base = defaultPhysicalCamera();
    base.target = [0, 0, 0];
    base.position = [10, 0, 0];
    const merged = mergeCameraCommand(base, { elevation: 90 });
    // elevation clamps to 89.9° to avoid the gimbal pole
    expect(merged.position[0]).toBeCloseTo(0, 1);
    expect(merged.position[1]).toBeCloseTo(10, 4);
    expect(merged.position[2]).toBeCloseTo(0, 1);
  });

  it('focusOnTarget sets focus distance to the axial pose distance', () => {
    const merged = mergeCameraCommand(defaultPhysicalCamera(), {
      position: [0, 0, 8],
      target: [0, 0, 0],
      focusOnTarget: true,
    });
    expect(merged.focusDistance).toBeCloseTo(8);
    expect(axialFocusDistance([0, 0, 8], [0, 0, 0], [2, 0, 4])).toBeCloseTo(4);
  });

  it('applies Blender FOV and lens shift onto the live three.js camera', () => {
    const camera = new THREE.PerspectiveCamera(50, 16 / 9, 0.1, 100);
    const state = {
      ...defaultPhysicalCamera(),
      focalLengthMm: 50,
      sensorFit: 'horizontal' as const,
      shiftX: 0.1,
      shiftY: -0.2,
    };
    applyPhysicalProjection(camera, state, 16 / 9);
    const projection = blenderProjection(state, 16 / 9);
    expect(camera.fov).toBeCloseTo(projection.verticalFovDeg, 6);
    expect(camera.projectionMatrix.elements[8]).toBeCloseTo(projection.shiftNdc[0], 6);
    expect(camera.projectionMatrix.elements[9]).toBeCloseTo(projection.shiftNdc[1], 6);
  });

  it('syncs state back from the live camera', () => {
    const camera = new THREE.PerspectiveCamera(40, 1, 0.1, 2000);
    camera.position.set(1, 2, 3);
    const state = syncStateFromLiveCamera(
      defaultPhysicalCamera(),
      camera,
      new THREE.Vector3(4, 5, 6),
    );
    expect(state.position).toEqual([1, 2, 3]);
    expect(state.target).toEqual([4, 5, 6]);
    expect(state.near).toBe(0.1);
    expect(state.far).toBe(2000);
    expect(state.focalLengthMm).toBeCloseTo(fovToFocalLength(40, 36), 6);
    expect(state.projection).toBe('perspective');
  });
});
