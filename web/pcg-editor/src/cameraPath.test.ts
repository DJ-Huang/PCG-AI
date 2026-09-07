import { describe, expect, it } from 'vitest';

import {
  addMotionCurvePoint,
  addShotCamera,
  addShotMotionCurve,
  connectShotCameras,
  removeMotionCurvePoint,
  shotCamerasToFlow,
} from './cameraGraph';
import { applyMotionCurveToCamera, findCameraPathCurve, sampleMotionCurve, sampleShotCameraWorld } from './cameraPath';
import { createDefaultShot } from './shot';

describe('camera path', () => {
  it('adds a motion-curve node that cameras can wire to', () => {
    const withCurve = addShotMotionCurve(createDefaultShot(), { x: 40, y: 0 });
    expect(withCurve.motionCurves).toHaveLength(1);
    expect(withCurve.selectedNodeId).toBe(withCurve.motionCurves[0].id);
    const wired = connectShotCameras(withCurve, withCurve.motionCurves[0].id, withCurve.cameras[0].id);
    expect(findCameraPathCurve(wired, wired.cameras[0].id)?.id).toBe(withCurve.motionCurves[0].id);
    const flow = shotCamerasToFlow(wired);
    expect(flow.nodes.some((node) => node.type === 'MotionCurve')).toBe(true);
    expect(flow.nodes.some((node) => node.type === 'Camera')).toBe(true);
    expect(flow.edges.some((edge) => (
      edge.source === withCurve.motionCurves[0].id && edge.target === withCurve.cameras[0].id
    ))).toBe(true);
  });

  it('samples a curve from start to end and rides the camera along it', () => {
    const curve = {
      id: 'path_a',
      name: 'Dolly',
      position: { x: 0, y: 0 },
      controlPoints: [
        [0, 1, 0],
        [2, 1, 0],
        [4, 1, 0],
      ] as [number, number, number][],
      closed: false,
    };
    const start = sampleMotionCurve(curve, 0);
    const end = sampleMotionCurve(curve, 1);
    expect(start?.position[0]).toBeCloseTo(0, 5);
    expect(end?.position[0]).toBeCloseTo(4, 5);

    const shot = connectShotCameras(
      addShotMotionCurve(createDefaultShot()),
      'path_a',
      'cam_a',
    );
    const path = shot.motionCurves[0];
    const posed = applyMotionCurveToCamera(shot.camera, path, shot.durationSeconds, shot.durationSeconds);
    const world = sampleShotCameraWorld(shot, 'cam_a', shot.durationSeconds);
    expect(world.position[0]).toBeCloseTo(posed.position[0], 5);
    expect(world.position).not.toEqual(shot.camera.position);
  });

  it('appends and removes motion-curve control points', () => {
    const withCurve = addShotMotionCurve(createDefaultShot());
    const id = withCurve.motionCurves[0].id;
    const added = addMotionCurvePoint(withCurve, id);
    expect(added.motionCurves[0].controlPoints.length).toBe(withCurve.motionCurves[0].controlPoints.length + 1);
    const trimmed = removeMotionCurvePoint(added, id, added.motionCurves[0].controlPoints.length - 1);
    expect(trimmed.motionCurves[0].controlPoints).toHaveLength(withCurve.motionCurves[0].controlPoints.length);
    expect(removeMotionCurvePoint(withCurve, id, 0).motionCurves[0].controlPoints.length).toBeGreaterThanOrEqual(2);
  });

  it('keeps a second camera free when only the first is on a path', () => {
    const withCam = addShotCamera(createDefaultShot());
    const withCurve = addShotMotionCurve(withCam);
    const wired = connectShotCameras(withCurve, withCurve.motionCurves[0].id, withCurve.cameras[0].id);
    expect(findCameraPathCurve(wired, wired.cameras[0].id)).not.toBeNull();
    expect(findCameraPathCurve(wired, wired.cameras[1].id)).toBeNull();
  });
});
