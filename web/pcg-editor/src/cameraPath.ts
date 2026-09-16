import * as THREE from 'three';

import { sampleCameraTrack, samplePathProgress } from './cameraMotion';
import { applyCameraTransform, findCameraTransform } from './cameraTransform';
import { defaultPhysicalCamera, type PhysicalCameraState } from './physicalCamera';
import {
  findShotCamera,
  type ShotDocument,
  type ShotMotionCurve,
} from './shot';

export function findCameraPathCurve(
  shot: ShotDocument,
  cameraId: string,
): ShotMotionCurve | null {
  const edge = shot.cameraEdges.find((entry) => (
    entry.target === cameraId
    && (shot.motionCurves ?? []).some((curve) => curve.id === entry.source)
  ));
  if (!edge) return null;
  return (shot.motionCurves ?? []).find((curve) => curve.id === edge.source) ?? null;
}

export function createDefaultMotionCurvePoints(
  camera: PhysicalCameraState,
): [number, number, number][] {
  const position = new THREE.Vector3(...camera.position);
  const target = new THREE.Vector3(...camera.target);
  const ahead = target.clone().sub(position);
  if (ahead.lengthSq() < 1e-6) ahead.set(0, 0, -1);
  ahead.normalize();
  const up = new THREE.Vector3(...camera.up);
  const right = ahead.clone().cross(up);
  if (right.lengthSq() < 1e-6) right.set(1, 0, 0);
  right.normalize();
  const start = position.clone().add(right.clone().multiplyScalar(-1.6));
  const mid = position.clone().add(ahead.clone().multiplyScalar(0.4));
  const end = position.clone().add(ahead.clone().multiplyScalar(2.4)).add(right.clone().multiplyScalar(1.4));
  const finish = position.clone().add(ahead.clone().multiplyScalar(3.6));
  return [start.toArray(), mid.toArray(), end.toArray(), finish.toArray()] as [number, number, number][];
}

export function sampleMotionCurve(
  curve: ShotMotionCurve,
  u: number,
): { position: [number, number, number]; tangent: [number, number, number] } | null {
  const points = curve.controlPoints
    .filter((point) => point.length === 3 && point.every((axis) => Number.isFinite(axis)))
    .map((point) => new THREE.Vector3(point[0], point[1], point[2]));
  if (points.length === 0) return null;
  if (points.length === 1) {
    return {
      position: points[0].toArray() as [number, number, number],
      tangent: [0, 0, -1],
    };
  }
  const path = new THREE.CatmullRomCurve3(points, curve.closed === true, 'catmullrom', 0.5);
  const t = THREE.MathUtils.clamp(u, 0, 1);
  const position = path.getPoint(t);
  const tangent = path.getTangent(t);
  if (tangent.lengthSq() < 1e-8) tangent.set(0, 0, -1);
  else tangent.normalize();
  return {
    position: position.toArray() as [number, number, number],
    tangent: tangent.toArray() as [number, number, number],
  };
}

export function applyMotionCurveToCamera(
  state: PhysicalCameraState,
  curve: ShotMotionCurve,
  timeSeconds: number,
  durationSeconds: number,
): PhysicalCameraState {
  const u = samplePathProgress(curve.cameraKeyframes ?? [], timeSeconds, durationSeconds);
  const sampled = sampleMotionCurve(curve, u);
  if (!sampled) return state;
  const position = new THREE.Vector3(...sampled.position);
  const tangent = new THREE.Vector3(...sampled.tangent);
  const lookDistance = Math.max(state.focusDistance, 0.5);
  const target = position.clone().add(tangent.multiplyScalar(lookDistance));
  return {
    ...state,
    position: sampled.position,
    target: curve.lookMode === 'target' ? state.target : target.toArray() as [number, number, number],
  };
}

export function sampleShotCameraWorld(
  shot: ShotDocument,
  cameraId: string,
  timeSeconds: number,
): PhysicalCameraState {
  const station = findShotCamera(shot, cameraId);
  if (!station) return defaultPhysicalCamera();
  const isActive = cameraId === shot.activeCameraId;
  const base = isActive ? shot.camera : station.camera;
  const keyframes = isActive ? shot.cameraKeyframes : station.cameraKeyframes;
  let sampled = sampleCameraTrack(
    base,
    keyframes,
    timeSeconds,
    shot.width / Math.max(shot.height, 1),
  );
  const curve = findCameraPathCurve(shot, cameraId);
  if (curve) {
    const posed = applyMotionCurveToCamera(sampled, curve, timeSeconds, shot.durationSeconds);
    // The curve supplies position/tangent; authored channels (including look target) override it.
    sampled = sampleCameraTrack(posed, curve.cameraKeyframes ?? [], timeSeconds, shot.width / Math.max(shot.height, 1));
  }
  return applyCameraTransform(sampled, findCameraTransform(shot, cameraId));
}
