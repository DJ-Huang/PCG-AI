import { findCameraPathCurve, sampleShotCameraWorld } from './cameraPath';
import { cameraStateToCommand } from './cameraMotion';
import type { CameraCommand, PhysicalCameraState } from './physicalCamera';
import {
  normalizeShotDocument,
  syncShotCameras,
  type ShotCameraKeyframe,
  type ShotDocument,
  type ShotInterpolation,
} from './shot';

const INTERPOLATIONS: ShotInterpolation[] = ['linear', 'ease-in', 'ease-out', 'ease-in-out', 'step'];

export function editableMotionCurve(shot: ShotDocument) {
  return shot.motionCurves.find((curve) => curve.id === shot.selectedNodeId)
    ?? findCameraPathCurve(shot, shot.activeCameraId);
}

export function editableCameraKeyframes(shot: ShotDocument): ShotCameraKeyframe[] {
  const curve = editableMotionCurve(shot);
  return curve ? curve.cameraKeyframes ?? [] : shot.cameraKeyframes;
}

function writeCameraKeyframes(shot: ShotDocument, keys: ShotCameraKeyframe[]): ShotDocument {
  const curve = editableMotionCurve(shot);
  return curve ? { ...shot, motionCurves: shot.motionCurves.map((entry) => entry.id === curve.id ? { ...entry, cameraKeyframes: keys } : entry) }
    : syncShotCameras({ ...shot, cameraKeyframes: keys });
}

export function snapShotTime(shot: ShotDocument, time: number): number {
  return Number.isFinite(time) ? Math.min(shot.durationSeconds, Math.max(0, Math.round(time * shot.fps) / shot.fps)) : 0;
}

export function canonicalShotPayload(shot: ShotDocument): string {
  return JSON.stringify(normalizeShotDocument(shot));
}

export function keyframeTimeEpsilon(fps: number): number {
  return 0.5 / Math.max(fps, 1);
}

function sortKeyframes(keyframes: ShotCameraKeyframe[]): ShotCameraKeyframe[] {
  return [...keyframes].sort((a, b) => a.timeSeconds - b.timeSeconds || a.id.localeCompare(b.id));
}

export function normalizeCameraKeyframe(
  value: Partial<ShotCameraKeyframe> | null | undefined,
  index: number,
  durationSeconds: number,
): ShotCameraKeyframe | null {
  if (!value || typeof value !== 'object' || !value.value || typeof value.value !== 'object') {
    return null;
  }
  const interpolation = INTERPOLATIONS.includes(value.interpolation as ShotInterpolation)
    ? value.interpolation as ShotInterpolation
    : 'ease-in-out';
  return {
    id: typeof value.id === 'string' && value.id.trim() ? value.id.trim() : `camera_key_${index + 1}`,
    timeSeconds: Math.min(durationSeconds, Math.max(0, Number.isFinite(value.timeSeconds) ? value.timeSeconds as number : 0)),
    interpolation,
    value: { ...value.value },
  };
}

export function upsertCameraKeyframe(
  shot: ShotDocument,
  keyframe: Partial<ShotCameraKeyframe> & { value: CameraCommand },
): ShotDocument {
  const keys = editableCameraKeyframes(shot);
  const nextKey = normalizeCameraKeyframe(keyframe, keys.length, shot.durationSeconds);
  if (!nextKey) return shot;
  nextKey.timeSeconds = snapShotTime(shot, nextKey.timeSeconds);
  const epsilon = keyframeTimeEpsilon(shot.fps);
  const existingIndex = keys.findIndex((candidate) => (
    (keyframe.id && candidate.id === keyframe.id)
    || Math.abs(candidate.timeSeconds - nextKey.timeSeconds) <= epsilon
  ));
  const keyframes = [...keys];
  if (existingIndex >= 0) {
    const old = keyframes[existingIndex];
    keyframes[existingIndex] = { ...old, ...nextKey, id: old.id, value: { ...old.value, ...nextKey.value } };
  } else {
    const ids = new Set(keys.map((key) => key.id));
    for (let i = keys.length + 1; ids.has(nextKey.id); i++) nextKey.id = `camera_key_${i}`;
    keyframes.push(nextKey);
  }
  return writeCameraKeyframes(shot, sortKeyframes(keyframes));
}

export function removeCameraKeyframe(shot: ShotDocument, keyframeId: string): ShotDocument {
  return writeCameraKeyframes(shot, editableCameraKeyframes(shot).filter((key) => key.id !== keyframeId));
}

/** Curve editing changes one property; other keys at the same frame stay put. */
export function removeCameraChannelKey(shot: ShotDocument, keyframeId: string, field: keyof CameraCommand): ShotDocument {
  return writeCameraKeyframes(shot, editableCameraKeyframes(shot).flatMap((key) => {
    if (key.id !== keyframeId) return [key];
    const value = { ...key.value };
    delete value[field];
    return Object.keys(value).length ? [{ ...key, value }] : [];
  }));
}

export function moveCameraChannelKey(shot: ShotDocument, keyframeId: string, field: keyof CameraCommand, timeSeconds: number): { shot: ShotDocument; keyframeId: string } {
  const unchanged = { shot, keyframeId };
  if (!Number.isFinite(timeSeconds)) return unchanged;
  const keys = editableCameraKeyframes(shot);
  const source = keys.find((key) => key.id === keyframeId);
  const time = snapShotTime(shot, timeSeconds);
  if (!source || source.value[field] === undefined || source.timeSeconds === time) return unchanged;
  const target = keys.find((key) => key.id !== keyframeId && Math.abs(key.timeSeconds - time) <= keyframeTimeEpsilon(shot.fps));
  if (target?.value[field] !== undefined) return unchanged;
  const value = { [field]: source.value[field] };
  const remaining = { ...source.value }; delete remaining[field];
  const hasRemaining = Object.keys(remaining).length > 0;
  let nextId = target?.id ?? (hasRemaining ? `${source.id}_${field}` : source.id);
  if (!target && hasRemaining) {
    const ids = new Set(keys.map((key) => key.id));
    for (let i = 2; ids.has(nextId); i++) nextId = `${source.id}_${field}_${i}`;
  }
  const nextKeys = keys.flatMap((key) => {
    if (key.id === source.id) return hasRemaining ? [{ ...key, value: remaining }] : [];
    if (key.id === target?.id) return [{ ...key, value: { ...key.value, ...value } }];
    return [key];
  });
  if (!target) nextKeys.push({ ...source, id: nextId, timeSeconds: time, value });
  return { shot: writeCameraKeyframes(shot, sortKeyframes(nextKeys)), keyframeId: nextId };
}

export function moveCameraKeyframeTime(shot: ShotDocument, keyframeId: string, timeSeconds: number): ShotDocument {
  if (!Number.isFinite(timeSeconds)) return shot;
  const time = snapShotTime(shot, timeSeconds);
  const keys = editableCameraKeyframes(shot);
  // A drag must never silently delete another key at the destination frame.
  if (keys.some((key) => key.id !== keyframeId && Math.abs(key.timeSeconds - time) <= keyframeTimeEpsilon(shot.fps))) return shot;
  return writeCameraKeyframes(shot, sortKeyframes(keys.map((key) => key.id === keyframeId ? { ...key, timeSeconds: time } : key)));
}

export function replaceCameraKeyframes(shot: ShotDocument, keyframes: readonly Partial<ShotCameraKeyframe>[]): ShotDocument {
  let next = writeCameraKeyframes(shot, []);
  for (const [index, key] of keyframes.entries()) {
    const normalized = normalizeCameraKeyframe(key, index, shot.durationSeconds);
    if (normalized) next = upsertCameraKeyframe(next, normalized);
  }
  return next;
}

export function setKeyframeInterpolation(shot: ShotDocument, keyframeId: string, interpolation: ShotInterpolation): ShotDocument {
  if (!INTERPOLATIONS.includes(interpolation)) return shot;
  return writeCameraKeyframes(shot, editableCameraKeyframes(shot).map((key) => key.id === keyframeId ? { ...key, interpolation } : key));
}

export function setKeyframeCamera(shot: ShotDocument, keyframeId: string, camera: PhysicalCameraState): ShotDocument {
  return patchKeyframeValue(shot, keyframeId, cameraStateToCommand(camera));
}

export function patchKeyframeValue(shot: ShotDocument, keyframeId: string, value: CameraCommand): ShotDocument {
  return writeCameraKeyframes(shot, editableCameraKeyframes(shot).map((key) => (
    key.id === keyframeId ? { ...key, value: { ...key.value, ...value } } : key
  )));
}

export function findKeyframeAtTime(shot: ShotDocument, timeSeconds: number): ShotCameraKeyframe | null {
  return editableCameraKeyframes(shot).find((key) => Math.abs(key.timeSeconds - timeSeconds) <= keyframeTimeEpsilon(shot.fps)) ?? null;
}

export interface CameraTrajectoryPoint {
  timeSeconds: number;
  position: [number, number, number];
  target: [number, number, number];
  keyframeId?: string;
}

export function cameraTrajectoryPoints(
  shot: ShotDocument,
  samplesPerSegment = 8,
): CameraTrajectoryPoint[] {
  const keys = sortKeyframes(editableCameraKeyframes(shot));
  if (keys.length === 0) {
    if (findCameraPathCurve(shot, shot.activeCameraId)) {
      const points: CameraTrajectoryPoint[] = [];
      const steps = Math.max(samplesPerSegment * 2, 8);
      for (let step = 0; step <= steps; step++) {
        const timeSeconds = shot.durationSeconds * (step / steps);
        const sampled = sampleShotCameraWorld(shot, shot.activeCameraId, timeSeconds);
        points.push({ timeSeconds, position: sampled.position, target: sampled.target });
      }
      return points;
    }
    const camera = sampleShotCameraWorld(shot, shot.activeCameraId, 0);
    return [{ timeSeconds: 0, position: camera.position, target: camera.target }];
  }
  const points: CameraTrajectoryPoint[] = [];
  for (let index = 0; index < keys.length; index++) {
    const key = keys[index];
    const sampled = sampleShotCameraWorld(shot, shot.activeCameraId, key.timeSeconds);
    points.push({
      timeSeconds: key.timeSeconds,
      position: sampled.position,
      target: sampled.target,
      keyframeId: key.id,
    });
    const next = keys[index + 1];
    if (!next) continue;
    const span = next.timeSeconds - key.timeSeconds;
    if (span <= 0) continue;
    for (let step = 1; step < samplesPerSegment; step++) {
      const timeSeconds = key.timeSeconds + (span * step) / samplesPerSegment;
      const mid = sampleShotCameraWorld(shot, shot.activeCameraId, timeSeconds);
      points.push({ timeSeconds, position: mid.position, target: mid.target });
    }
  }
  return points;
}
