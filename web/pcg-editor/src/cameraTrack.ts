import { findCameraPathCurve, sampleShotCameraWorld } from './cameraPath';
import type { CameraCommand, PhysicalCameraState } from './physicalCamera';
import {
  normalizeShotDocument,
  syncShotCameras,
  type ShotCameraKeyframe,
  type ShotDocument,
  type ShotInterpolation,
} from './shot';

const INTERPOLATIONS: ShotInterpolation[] = ['linear', 'ease-in', 'ease-out', 'ease-in-out'];

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
  const nextKey = normalizeCameraKeyframe(keyframe, shot.cameraKeyframes.length, shot.durationSeconds);
  if (!nextKey) return shot;
  const epsilon = keyframeTimeEpsilon(shot.fps);
  const existingIndex = shot.cameraKeyframes.findIndex((candidate) => (
    candidate.id === nextKey.id
    || Math.abs(candidate.timeSeconds - nextKey.timeSeconds) <= epsilon
  ));
  const keyframes = [...shot.cameraKeyframes];
  if (existingIndex >= 0) {
    keyframes[existingIndex] = {
      ...keyframes[existingIndex],
      ...nextKey,
      id: keyframes[existingIndex].id,
    };
  } else {
    keyframes.push(nextKey);
  }
  return syncShotCameras({ ...shot, cameraKeyframes: sortKeyframes(keyframes) });
}

export function removeCameraKeyframe(shot: ShotDocument, keyframeId: string): ShotDocument {
  return syncShotCameras({
    ...shot,
    cameraKeyframes: shot.cameraKeyframes.filter((keyframe) => keyframe.id !== keyframeId),
  });
}

export function moveCameraKeyframeTime(
  shot: ShotDocument,
  keyframeId: string,
  timeSeconds: number,
): ShotDocument {
  return syncShotCameras({
    ...shot,
    cameraKeyframes: sortKeyframes(shot.cameraKeyframes.map((keyframe) => (
      keyframe.id === keyframeId
        ? { ...keyframe, timeSeconds: Math.min(shot.durationSeconds, Math.max(0, timeSeconds)) }
        : keyframe
    ))),
  });
}

export function replaceCameraKeyframes(
  shot: ShotDocument,
  keyframes: readonly Partial<ShotCameraKeyframe>[],
): ShotDocument {
  const normalized = keyframes.flatMap((keyframe, index) => {
    const next = normalizeCameraKeyframe(keyframe, index, shot.durationSeconds);
    return next ? [next] : [];
  });
  return syncShotCameras({ ...shot, cameraKeyframes: sortKeyframes(normalized) });
}

export function setKeyframeInterpolation(
  shot: ShotDocument,
  keyframeId: string,
  interpolation: ShotInterpolation,
): ShotDocument {
  return syncShotCameras({
    ...shot,
    cameraKeyframes: shot.cameraKeyframes.map((keyframe) => (
      keyframe.id === keyframeId ? { ...keyframe, interpolation } : keyframe
    )),
  });
}

export function setKeyframeCamera(
  shot: ShotDocument,
  keyframeId: string,
  camera: PhysicalCameraState,
): ShotDocument {
  const value = cameraStateToCommand(camera);
  return syncShotCameras({
    ...shot,
    cameraKeyframes: shot.cameraKeyframes.map((keyframe) => (
      keyframe.id === keyframeId ? { ...keyframe, value } : keyframe
    )),
  });
}

export function findKeyframeAtTime(
  shot: ShotDocument,
  timeSeconds: number,
): ShotCameraKeyframe | null {
  const epsilon = keyframeTimeEpsilon(shot.fps);
  return shot.cameraKeyframes.find((keyframe) => Math.abs(keyframe.timeSeconds - timeSeconds) <= epsilon) ?? null;
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
  const keys = sortKeyframes(shot.cameraKeyframes);
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
