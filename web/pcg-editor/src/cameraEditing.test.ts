import { describe, expect, it } from 'vitest';
import { editCameraAtTime } from './cameraEditing';
import { applyShotOperations } from './shotOperations';
import { findCameraTransform, moveCameraRig, setCameraTransform } from './cameraTransform';
import { sampleShotCameraWorld } from './cameraPath';
import { sampleCameraTrack } from './cameraMotion';
import { editableCameraKeyframes, moveCameraChannelKey, moveCameraKeyframeTime, removeCameraChannelKey, upsertCameraKeyframe } from './cameraTrack';
import { createDefaultShot, normalizeShotDocument, syncShotCameras } from './shot';
import { parseShotFile } from './shotFile';

describe('PICG camera workflow', () => {
  it('creates one connected Transform for repeated viewport moves, survives reload and does not double apply', () => {
    let shot = createDefaultShot();
    const start = sampleShotCameraWorld(shot, shot.activeCameraId, 0);
    shot = moveCameraRig(shot, shot.activeCameraId, start, { ...start, position: [6, 2.5, 4], target: [3, 0, 0] });
    const moved = sampleShotCameraWorld(shot, shot.activeCameraId, 0);
    expect(moved.position).toEqual([6, 2.5, 4]);
    expect(moved.target[0]).toBeCloseTo(3);
    shot = moveCameraRig(shot, shot.activeCameraId, moved, { ...moved, position: [7, 2.5, 4], target: [4, 0, 0] });
    expect(shot.cameraTransforms).toHaveLength(1);
    const rig = findCameraTransform(shot, shot.activeCameraId)!;
    expect(shot.cameraEdges.some((edge) => edge.source === shot.activeCameraId && edge.target === rig.id)).toBe(true);
    expect(shot.cameraEdges.some((edge) => edge.source === rig.id && edge.target === 'shot_output')).toBe(true);
    const restored = normalizeShotDocument(JSON.parse(JSON.stringify(shot)));
    expect(sampleShotCameraWorld(restored, restored.activeCameraId, 0).position).toEqual([7, 2.5, 4]);
  });

  it('keys the displayed pose before the rig and interpolates independent lens/focus channels', () => {
    let shot = createDefaultShot();
    shot = editCameraAtTime(shot, { position: [5, 2.5, 4], target: [2, 0, 0] }, 0, false);
    shot = editCameraAtTime(shot, { position: [8, 2.5, 4], target: [5, 0, 0] }, 2, true);
    shot = syncShotCameras(shot);
    expect(sampleShotCameraWorld(shot, shot.activeCameraId, 2).position).toEqual([8, 2.5, 4]);
    const camera = shot.camera;
    const sampled = sampleCameraTrack(camera, [
      { id: 'lens0', timeSeconds: 0, interpolation: 'linear', value: { focalLengthMm: 20 } },
      { id: 'focus', timeSeconds: 1, interpolation: 'linear', value: { focusDistance: 3 } },
      { id: 'lens2', timeSeconds: 2, interpolation: 'linear', value: { focalLengthMm: 60 } },
    ], 1, 16 / 9);
    expect(sampled.focalLengthMm).toBe(40);
    expect(sampled.focusDistance).toBe(3);
  });

  it('reconnects an existing disconnected Transform and keeps the graph top-down', () => {
    let shot = setCameraTransform(createDefaultShot(), 'cam_a', { translation: [2, 0, 0] });
    const rig = shot.cameraTransforms[0];
    shot = { ...shot, cameraEdges: shot.cameraEdges.filter((edge) => edge.target !== rig.id) };
    shot = setCameraTransform(shot, 'cam_a', { translation: [4, 0, 0] });
    expect(shot.cameraTransforms).toHaveLength(1);
    expect(findCameraTransform(shot, 'cam_a')?.id).toBe(rig.id);
    expect(sampleShotCameraWorld(normalizeShotDocument(shot), 'cam_a', 0).position).toEqual([7, 2.5, 4]);
    expect(shot.cameraTransforms[0].position.x).toBe(shot.cameras[0].position.x);
    expect(shot.shotOutputPosition).toEqual({ x: shot.cameras[0].position.x, y: 480 });
  });

  it('authors an entire rail/lens/focus shot atomically with stable Agent ids', () => {
    const shot = applyShotOperations(createDefaultShot(), [
      { op: 'upsert_camera', id: 'hero', camera: { position: [0, 1, 4], target: [0, 1, 0] } },
      { op: 'upsert_motion_curve', id: 'rail', cameraId: 'hero', controlPoints: [[0, 1, 4], [0, 1, 0]], lookMode: 'target' },
      { op: 'set_transform', cameraId: 'hero', translation: [2, 0, 0], rotationEulerDeg: [0, 0, 0] },
      { op: 'set_keyframes', nodeId: 'rail', keyframes: [
        { id: 'start', timeSeconds: 0, interpolation: 'linear', value: { pathProgress: 0, focalLengthMm: 24, focusDistance: 6 } },
        { id: 'end', timeSeconds: 5, interpolation: 'linear', value: { pathProgress: 1, focalLengthMm: 72, focusDistance: 2 } },
      ] },
      { op: 'select', nodeId: 'hero' },
    ]);
    expect(shot.cameras.some((camera) => camera.id === 'hero')).toBe(true);
    const middle = sampleShotCameraWorld(shot, 'hero', 2.5);
    expect(middle.position).toEqual([2, 1, 2]);
    expect(middle.focalLengthMm).toBe(48);
    expect(middle.focusDistance).toBe(4);
    expect(shot.motionCurves[0].cameraKeyframes).toHaveLength(2);
    expect(shot.cameras.find((camera) => camera.id === 'cam_a')!.cameraKeyframes).toHaveLength(0);
    const loaded = parseShotFile(JSON.stringify(shot));
    expect(sampleShotCameraWorld(loaded, 'hero', 2.5)).toEqual(middle);
    const offset = editCameraAtTime(loaded, { position: [4, 1, 4], target: [4, 1, 0] }, 0, false);
    expect(findCameraTransform(offset, 'hero')?.translation).toEqual([4, 0, 0]);
    expect(offset.motionCurves[0].cameraKeyframes).toEqual(loaded.motionCurves[0].cameraKeyframes);
  });

  it('rejects bad Agent batches without mutating the original and protects occupied frames', () => {
    const shot = createDefaultShot();
    const original = JSON.stringify(shot);
    expect(() => applyShotOperations(shot, [{ op: 'upsert_camera', id: 'test' }, { op: 'set_transform', cameraId: 'missing' }])).toThrow('Unknown camera');
    expect(JSON.stringify(shot)).toBe(original);
    expect(() => applyShotOperations(shot, [{ op: 'upsert_motion_curve', id: 'bad', controlPoints: [[0, 1, NaN], [0, 2, 3]] }])).toThrow('finite');
    let keyed = upsertCameraKeyframe(shot, { id: 'a', timeSeconds: 0, value: { focalLengthMm: 20 } });
    keyed = upsertCameraKeyframe(keyed, { id: 'b', timeSeconds: 1, value: { focalLengthMm: 50 } });
    expect(moveCameraKeyframeTime(keyed, 'a', 1)).toBe(keyed);
    const moved = moveCameraKeyframeTime(keyed, 'b', 1.123);
    expect(editableCameraKeyframes(moved)[1].timeSeconds * shot.fps).toBe(27);
    expect(() => parseShotFile('{}')).toThrow('PICG shot');
  });

  it('authors component transforms, action clips and visibility atomically', () => {
    const shot = applyShotOperations(createDefaultShot(), [
      { op: 'upsert_component', componentId: 'ellie', assetId: 'pcg.lib:character:human_proxy_standing', role: 'character', transform: { position: [1, 0, 2] } },
      { op: 'set_action_clip', componentId: 'ellie', clip: 'walk', startSeconds: 0.5, playbackRate: 1.25, loop: false },
      { op: 'set_object_keyframes', componentId: 'ellie', keyframes: [
        { id: 'start', timeSeconds: 0, interpolation: 'linear', transform: { position: [1, 0, 2] } },
        { id: 'end', timeSeconds: 5, interpolation: 'ease-in-out', transform: { position: [4, 0, 2] } },
      ] },
      { op: 'set_visibility_range', componentId: 'ellie', fromSeconds: 0.25, untilSeconds: 4.75 },
    ]);
    expect(shot.components[0]).toMatchObject({ componentId: 'ellie', role: 'character', visibleFromSeconds: 0.25, visibleUntilSeconds: 4.75, transform: { position: [1, 0, 2] } });
    expect(shot.objectAnimations[0]).toMatchObject({ componentId: 'ellie', clip: 'walk', playbackRate: 1.25 });
    expect(shot.objectAnimations[0].keyframes).toHaveLength(2);
    const original = JSON.stringify(shot);
    expect(() => applyShotOperations(shot, [
      { op: 'upsert_component', componentId: 'dina' },
      { op: 'set_visibility_range', componentId: 'missing', fromSeconds: 0, untilSeconds: 1 },
    ])).toThrow('Unknown component');
    expect(JSON.stringify(shot)).toBe(original);
  });

  it('moves and deletes a lens key without changing the rail timing at the same frame', () => {
    let shot = upsertCameraKeyframe(createDefaultShot(), { id: 'shared', timeSeconds: 0, value: { pathProgress: 0, focalLengthMm: 24, focusDistance: 5 } });
    const moved = moveCameraChannelKey(shot, 'shared', 'focalLengthMm', 1);
    shot = moved.shot;
    expect(editableCameraKeyframes(shot).find((key) => key.id === 'shared')).toMatchObject({ timeSeconds: 0, value: { pathProgress: 0, focusDistance: 5 } });
    expect(editableCameraKeyframes(shot).find((key) => key.id === moved.keyframeId)).toMatchObject({ timeSeconds: 1, value: { focalLengthMm: 24 } });
    shot = removeCameraChannelKey(shot, moved.keyframeId, 'focalLengthMm');
    expect(editableCameraKeyframes(shot)).toHaveLength(1);
    expect(editableCameraKeyframes(shot)[0].value).toEqual({ pathProgress: 0, focusDistance: 5 });
  });
});
