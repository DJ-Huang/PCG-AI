import { describe, expect, it } from 'vitest';

import {
  addShotCamera,
  addShotMotionCurve,
  connectShotCameras,
  nextCameraId,
  preserveFlowNodeLayout,
  removeShotCamera,
  shotCamerasToFlow,
  upsertShotCamera,
} from './cameraGraph';
import { createDefaultShot, selectShotCamera, SHOT_OUTPUT_ID } from './shot';
import { upsertCameraKeyframe } from './cameraTrack';

describe('camera graph', () => {
  it('adds a second camera station and keeps the first track intact', () => {
    const keyed = upsertCameraKeyframe(createDefaultShot(), {
      id: 'wide',
      timeSeconds: 0,
      interpolation: 'linear',
      value: { focalLengthMm: 24 },
    });
    const next = addShotCamera(keyed, { x: 400, y: 160 }, { name: 'Camera B' });
    expect(next.cameras).toHaveLength(2);
    expect(next.activeCameraId).toBe(next.cameras[1].id);
    expect(next.cameras[0].cameraKeyframes).toHaveLength(1);
    expect(next.cameraKeyframes).toEqual([]);
  });

  it('selecting a camera exposes that station for preview keying', () => {
    const withB = addShotCamera(createDefaultShot());
    const keyedB = upsertCameraKeyframe(withB, {
      timeSeconds: 1,
      interpolation: 'linear',
      value: { position: [2, 1, 0] },
    });
    const backToA = selectShotCamera(keyedB, keyedB.cameras[0].id);
    expect(backToA.activeCameraId).toBe(keyedB.cameras[0].id);
    expect(backToA.cameraKeyframes).toEqual([]);
    expect(selectShotCamera(backToA, keyedB.cameras[1].id).cameraKeyframes).toHaveLength(1);
  });

  it('connects camera stations to shot output and to each other', () => {
    const shot = addShotCamera(createDefaultShot());
    const connected = connectShotCameras(shot, shot.cameras[0].id, shot.cameras[1].id);
    expect(connected.cameraEdges.some((edge) => (
      edge.source === shot.cameras[0].id && edge.target === shot.cameras[1].id
    ))).toBe(true);
    const flow = shotCamerasToFlow(connected);
    expect(flow.nodes.some((node) => node.id === SHOT_OUTPUT_ID)).toBe(true);
    expect(flow.edges.length).toBeGreaterThanOrEqual(2);
  });

  it('keeps measured node sizes so the camera canvas can unhide pills', () => {
    const shot = addShotMotionCurve(createDefaultShot());
    const { nodes } = shotCamerasToFlow(shot);
    const measured = nodes.map((node) => ({
      ...node,
      width: 160,
      height: 28,
      measured: { width: 160, height: 28 },
    }));
    const merged = preserveFlowNodeLayout(shotCamerasToFlow(shot).nodes, measured);
    expect(merged).toHaveLength(measured.length);
    expect(merged.every((node) => node.measured?.width === 160 && node.measured?.height === 28)).toBe(true);
    expect(merged.map((node) => node.id)).toEqual(measured.map((node) => node.id));
  });

  it('refuses to delete the last camera and upserts by id', () => {
    const shot = createDefaultShot();
    expect(removeShotCamera(shot, shot.cameras[0].id).cameras).toHaveLength(1);
    expect(nextCameraId(shot.cameras)).toBe('cam_b');
    const renamed = upsertShotCamera(shot, { id: 'cam_a', name: 'Wide' });
    expect(renamed.cameras[0].name).toBe('Wide');
  });
});
