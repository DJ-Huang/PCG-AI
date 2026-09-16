import * as THREE from 'three';

import {
  mergeCameraCommand,
  sphericalPosition,
  type CameraCommand,
  type PhysicalCameraState,
} from './physicalCamera';
import type { ShotCameraKeyframe, ShotInterpolation } from './shot';

export const CAMERA_MOTION_PRESETS = [
  'static',
  'pan-tilt',
  'dolly',
  'truck',
  'pedestal',
  'crane',
  'orbit',
  'handheld',
] as const;

export type CameraMotionPreset = (typeof CAMERA_MOTION_PRESETS)[number];

function ease(interpolation: ShotInterpolation, t: number): number {
  const x = THREE.MathUtils.clamp(t, 0, 1);
  if (interpolation === 'step') return x < 1 ? 0 : 1;
  if (interpolation === 'ease-in') return x * x;
  if (interpolation === 'ease-out') return 1 - (1 - x) * (1 - x);
  if (interpolation === 'ease-in-out') return x * x * (3 - 2 * x);
  return x;
}

function lerp(a: number, b: number, t: number): number {
  return a + (b - a) * t;
}

function lerpVec3(
  a: [number, number, number],
  b: [number, number, number],
  t: number,
): [number, number, number] {
  return [lerp(a[0], b[0], t), lerp(a[1], b[1], t), lerp(a[2], b[2], t)];
}

export function interpolateCamera(
  from: PhysicalCameraState,
  to: PhysicalCameraState,
  t: number,
): PhysicalCameraState {
  return {
    position: lerpVec3(from.position, to.position, t),
    target: lerpVec3(from.target, to.target, t),
    up: lerpVec3(from.up, to.up, t),
    projection: t < 1 ? from.projection : to.projection,
    focalLengthMm: lerp(from.focalLengthMm, to.focalLengthMm, t),
    sensorWidthMm: lerp(from.sensorWidthMm, to.sensorWidthMm, t),
    sensorHeightMm: lerp(from.sensorHeightMm, to.sensorHeightMm, t),
    sensorFit: t < 1 ? from.sensorFit : to.sensorFit,
    shiftX: lerp(from.shiftX, to.shiftX, t),
    shiftY: lerp(from.shiftY, to.shiftY, t),
    apertureFstop: lerp(from.apertureFstop, to.apertureFstop, t),
    apertureBlades: t < 1 ? from.apertureBlades : to.apertureBlades,
    apertureRotationDeg: lerp(from.apertureRotationDeg, to.apertureRotationDeg, t),
    apertureRatio: lerp(from.apertureRatio, to.apertureRatio, t),
    focusDistance: lerp(from.focusDistance, to.focusDistance, t),
    dofEnabled: t < 1 ? from.dofEnabled : to.dofEnabled,
    exposure: lerp(from.exposure, to.exposure, t),
    near: lerp(from.near, to.near, t),
    far: lerp(from.far, to.far, t),
    orthographicScale: lerp(from.orthographicScale, to.orthographicScale, t),
  };
}

export function cameraStateToCommand(state: PhysicalCameraState): CameraCommand {
  return {
    position: [...state.position],
    target: [...state.target],
    up: [...state.up],
    projection: state.projection,
    focalLengthMm: state.focalLengthMm,
    sensorWidthMm: state.sensorWidthMm,
    sensorHeightMm: state.sensorHeightMm,
    sensorFit: state.sensorFit,
    shiftX: state.shiftX,
    shiftY: state.shiftY,
    apertureFstop: state.apertureFstop,
    apertureBlades: state.apertureBlades,
    apertureRotationDeg: state.apertureRotationDeg,
    apertureRatio: state.apertureRatio,
    focusDistance: state.focusDistance,
    dofEnabled: state.dofEnabled,
    exposure: state.exposure,
    near: state.near,
    far: state.far,
    orthographicScale: state.orthographicScale,
  };
}

/** Pure, deterministic camera-track sampling shared by playback and export. */
export function sampleCameraTrack(
  base: PhysicalCameraState,
  keyframes: readonly ShotCameraKeyframe[],
  timeSeconds: number,
  outputAspect: number,
): PhysicalCameraState {
  const ordered = [...keyframes].filter((key) => Number.isFinite(key.timeSeconds))
    .sort((a, b) => a.timeSeconds - b.timeSeconds);
  let previous = base;
  const resolved = ordered.map((key) => {
    const state = mergeCameraCommand(previous, key.value, outputAspect);
    const fields = new Set(Object.keys(key.value));
    // Commands such as fov / focusOnTarget author their resolved channels.
    for (const field of Object.keys(base) as Array<keyof PhysicalCameraState>) {
      if (JSON.stringify(state[field]) !== JSON.stringify(previous[field])) fields.add(field);
    }
    previous = state;
    return { ...key, state, fields };
  });
  const result = { ...base };
  for (const field of Object.keys(base) as Array<keyof PhysicalCameraState>) {
    const keys = [...new Map(resolved.filter((key) => key.fields.has(field)).map((key) => [key.timeSeconds, key])).values()];
    let from = { timeSeconds: 0, state: base };
    let sampled = base;
    for (const key of keys) {
      if (key.timeSeconds <= Math.max(timeSeconds, 0)) {
        from = key;
        sampled = key.state;
      } else {
        const t = (Math.max(timeSeconds, 0) - from.timeSeconds) / Math.max(key.timeSeconds - from.timeSeconds, Number.EPSILON);
        sampled = interpolateCamera(from.state, key.state, ease(key.interpolation, t));
        break;
      }
    }
    Object.assign(result, { [field]: sampled[field] });
  }
  return result;
}

export function samplePathProgress(keys: readonly ShotCameraKeyframe[], time: number, duration: number): number {
  const progressKeys = keys.filter((key) => Number.isFinite(key.value.pathProgress)).sort((a, b) => a.timeSeconds - b.timeSeconds);
  if (!progressKeys.length) return THREE.MathUtils.clamp(time / Math.max(duration, 0.001), 0, 1);
  let from = { timeSeconds: 0, value: { pathProgress: 0 } };
  for (const key of progressKeys) {
    const value = THREE.MathUtils.clamp(key.value.pathProgress!, 0, 1);
    if (key.timeSeconds > time) return lerp(from.value.pathProgress, value,
      ease(key.interpolation, (time - from.timeSeconds) / Math.max(key.timeSeconds - from.timeSeconds, Number.EPSILON)));
    from = { timeSeconds: key.timeSeconds, value: { pathProgress: value } };
  }
  return from.value.pathProgress;
}

function keyframe(
  id: string,
  timeSeconds: number,
  value: CameraCommand,
  interpolation: ShotInterpolation = 'ease-in-out',
): ShotCameraKeyframe {
  return { id, timeSeconds, interpolation, value };
}

/** Presets only generate ordinary editable keyframes; no runtime controller is retained. */
export function createCameraPresetKeyframes(
  preset: CameraMotionPreset,
  camera: PhysicalCameraState,
  durationSeconds: number,
): ShotCameraKeyframe[] {
  const duration = Math.max(0.1, durationSeconds);
  const start = keyframe(`${preset}_start`, 0, cameraStateToCommand(camera), 'linear');
  const position = new THREE.Vector3(...camera.position);
  const target = new THREE.Vector3(...camera.target);
  const view = target.clone().sub(position);
  const right = view.clone().cross(new THREE.Vector3(...camera.up)).normalize();
  const distance = Math.max(view.length(), 0.001);
  if (preset === 'static') {
    return [start, keyframe('static_end', duration, cameraStateToCommand(camera), 'linear')];
  }
  if (preset === 'pan-tilt') {
    return [
      start,
      keyframe('pan_tilt_end', duration, {
        target: [target.x + distance * 0.25, target.y + distance * 0.12, target.z],
      }),
    ];
  }
  if (preset === 'dolly') {
    const end = position.clone().add(view.clone().normalize().multiplyScalar(distance * 0.35));
    return [start, keyframe('dolly_end', duration, { position: end.toArray() })];
  }
  if (preset === 'truck') {
    const offset = right.multiplyScalar(distance * 0.25);
    return [start, keyframe('truck_end', duration, {
      position: position.clone().add(offset).toArray(),
      target: target.clone().add(offset).toArray(),
    })];
  }
  if (preset === 'pedestal' || preset === 'crane') {
    const upDistance = distance * (preset === 'crane' ? 0.35 : 0.2);
    const offset = new THREE.Vector3(0, upDistance, 0);
    const endPosition = position.clone().add(offset);
    if (preset === 'crane') endPosition.add(view.clone().normalize().multiplyScalar(distance * 0.12));
    return [start, keyframe(`${preset}_end`, duration, {
      position: endPosition.toArray(),
      target: target.clone().add(offset).toArray(),
    })];
  }
  if (preset === 'orbit') {
    const offset = position.clone().sub(target);
    const spherical = new THREE.Spherical().setFromVector3(offset);
    const end = sphericalPosition(
      camera.target,
      THREE.MathUtils.radToDeg(spherical.theta) + 45,
      90 - THREE.MathUtils.radToDeg(spherical.phi),
      spherical.radius,
    );
    return [start, keyframe('orbit_end', duration, { position: end })];
  }

  const frames: ShotCameraKeyframe[] = [start];
  const step = 0.25;
  for (let time = step; time < duration; time += step) {
    const phase = time * 9.73;
    const offset = new THREE.Vector3(
      Math.sin(phase) * distance * 0.004,
      Math.sin(phase * 1.71) * distance * 0.003,
      Math.cos(phase * 1.19) * distance * 0.002,
    );
    frames.push(keyframe(`handheld_${frames.length}`, time, {
      position: position.clone().add(offset).toArray(),
      target: target.clone().add(offset.clone().multiplyScalar(0.35)).toArray(),
    }, 'ease-in-out'));
  }
  frames.push(keyframe('handheld_end', duration, cameraStateToCommand(camera), 'ease-in-out'));
  return frames;
}
