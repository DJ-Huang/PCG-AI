// physicalCamera.ts — session-scoped "physical camera" model for the preview
// viewport. Lens parameters follow photographic conventions (focal length in
// mm, aperture in f-stops, focus distance in world units) and are converted to
// three.js camera / BokehPass values here. Coordinates are viewport world
// space (three.js right-handed; Unity +Z is flipped to -Z), matching the
// camera block in PreviewCapture metadata.

import * as THREE from 'three';

export const CAMERA_PROJECTIONS = ['perspective', 'orthographic'] as const;
export type CameraProjection = (typeof CAMERA_PROJECTIONS)[number];

export interface PhysicalCameraState {
  position: [number, number, number];
  target: [number, number, number];
  up: [number, number, number];
  projection: CameraProjection;
  /** Full-frame-equivalent focal length. */
  focalLengthMm: number;
  /** Sensor height in mm; 24 = 35mm full frame. */
  sensorHeightMm: number;
  apertureFstop: number;
  /** World-unit focus distance for depth of field. */
  focusDistance: number;
  dofEnabled: boolean;
  /** toneMappingExposure multiplier. */
  exposure: number;
  near: number;
  far: number;
  /** Vertical world-space extent when projection is orthographic. */
  orthographicFrustumHeight?: number;
}

export const FULL_FRAME_SENSOR_HEIGHT_MM = 24;

export const CAMERA_LIMITS = {
  focalLengthMm: { min: 8, max: 400 },
  sensorHeightMm: { min: 5, max: 70 },
  apertureFstop: { min: 0.7, max: 64 },
  focusDistance: { min: 0.01, max: 100000 },
  exposure: { min: 0.05, max: 8 },
  near: { min: 0.0001, max: 1000 },
  far: { min: 0.1, max: 1000000 },
} as const;

export function defaultPhysicalCamera(): PhysicalCameraState {
  return {
    position: [3, 2.5, 4],
    target: [0, 0, 0],
    up: [0, 1, 0],
    projection: 'perspective',
    focalLengthMm: 26,
    sensorHeightMm: FULL_FRAME_SENSOR_HEIGHT_MM,
    apertureFstop: 8,
    focusDistance: 5.7,
    dofEnabled: false,
    exposure: 1,
    near: 0.01,
    far: 5000,
  };
}

export function focalLengthToFov(
  focalLengthMm: number,
  sensorHeightMm: number = FULL_FRAME_SENSOR_HEIGHT_MM,
): number {
  return THREE.MathUtils.radToDeg(2 * Math.atan(sensorHeightMm / (2 * focalLengthMm)));
}

export function fovToFocalLength(
  fovDeg: number,
  sensorHeightMm: number = FULL_FRAME_SENSOR_HEIGHT_MM,
): number {
  return sensorHeightMm / (2 * Math.tan(THREE.MathUtils.degToRad(fovDeg) / 2));
}

/** Photographic aperture → BokehShader uniform. 50mm f/1.4 maps to ≈0.025
 *  (the three.js DOF example default). */
export function apertureToBokehUniform(focalLengthMm: number, apertureFstop: number): number {
  const entrancePupilMm = focalLengthMm / Math.max(apertureFstop, 0.1);
  return THREE.MathUtils.clamp(entrancePupilMm * 7e-4, 0, 0.05);
}

type Vec3Tuple = [number, number, number];

/** Camera command payload accepted from MCP / the bridge. All fields optional;
 *  omitted fields keep the current session state. */
export interface CameraCommand {
  position?: Vec3Tuple;
  target?: Vec3Tuple;
  up?: Vec3Tuple;
  /** Spherical pose around target: degrees, distance in world units. */
  azimuth?: number;
  elevation?: number;
  distance?: number;
  projection?: CameraProjection;
  focalLengthMm?: number;
  /** Vertical FOV in degrees; alternative to focalLengthMm. */
  fov?: number;
  sensorHeightMm?: number;
  apertureFstop?: number;
  focusDistance?: number;
  /** Set focusDistance to the position↔target distance. */
  focusOnTarget?: boolean;
  dofEnabled?: boolean;
  exposure?: number;
  near?: number;
  far?: number;
  orthographicFrustumHeight?: number;
}

function isVec3(value: unknown): value is Vec3Tuple {
  return Array.isArray(value) && value.length === 3 && value.every((v) => Number.isFinite(v));
}

function clampNumber(value: unknown, min: number, max: number): number | null {
  if (typeof value !== 'number' || !Number.isFinite(value)) return null;
  return THREE.MathUtils.clamp(value, min, max);
}

export function sphericalPosition(
  target: Vec3Tuple,
  azimuthDeg: number,
  elevationDeg: number,
  distance: number,
): Vec3Tuple {
  const azimuth = THREE.MathUtils.degToRad(azimuthDeg);
  const elevation = THREE.MathUtils.degToRad(THREE.MathUtils.clamp(elevationDeg, -89.9, 89.9));
  const r = Math.max(distance, 0.001);
  return [
    target[0] + r * Math.cos(elevation) * Math.sin(azimuth),
    target[1] + r * Math.sin(elevation),
    target[2] + r * Math.cos(elevation) * Math.cos(azimuth),
  ];
}

export function mergeCameraCommand(
  current: PhysicalCameraState,
  command: CameraCommand,
): PhysicalCameraState {
  const next: PhysicalCameraState = { ...current };
  if (command.projection && CAMERA_PROJECTIONS.includes(command.projection)) {
    next.projection = command.projection;
  }
  if (isVec3(command.target)) next.target = [...command.target];
  if (isVec3(command.up)) next.up = [...command.up];
  if (isVec3(command.position)) {
    next.position = [...command.position];
  } else if (
    command.azimuth !== undefined ||
    command.elevation !== undefined ||
    command.distance !== undefined
  ) {
    const offset = new THREE.Vector3(
      current.position[0] - next.target[0],
      current.position[1] - next.target[1],
      current.position[2] - next.target[2],
    );
    const spherical = new THREE.Spherical().setFromVector3(offset);
    const azimuthDeg = command.azimuth !== undefined
      ? command.azimuth
      : THREE.MathUtils.radToDeg(spherical.theta);
    const elevationDeg = command.elevation !== undefined
      ? command.elevation
      : 90 - THREE.MathUtils.radToDeg(spherical.phi);
    const distance = command.distance !== undefined ? command.distance : spherical.radius;
    next.position = sphericalPosition(next.target, azimuthDeg, elevationDeg, distance);
  }

  const sensorHeightMm = clampNumber(
    command.sensorHeightMm,
    CAMERA_LIMITS.sensorHeightMm.min,
    CAMERA_LIMITS.sensorHeightMm.max,
  );
  if (sensorHeightMm !== null) next.sensorHeightMm = sensorHeightMm;
  const focalLengthMm = clampNumber(
    command.focalLengthMm,
    CAMERA_LIMITS.focalLengthMm.min,
    CAMERA_LIMITS.focalLengthMm.max,
  );
  if (focalLengthMm !== null) {
    next.focalLengthMm = focalLengthMm;
  } else if (command.fov !== undefined) {
    const fov = clampNumber(command.fov, 1, 170);
    if (fov !== null) next.focalLengthMm = fovToFocalLength(fov, next.sensorHeightMm);
  }
  const apertureFstop = clampNumber(
    command.apertureFstop,
    CAMERA_LIMITS.apertureFstop.min,
    CAMERA_LIMITS.apertureFstop.max,
  );
  if (apertureFstop !== null) next.apertureFstop = apertureFstop;
  const focusDistance = clampNumber(
    command.focusDistance,
    CAMERA_LIMITS.focusDistance.min,
    CAMERA_LIMITS.focusDistance.max,
  );
  if (focusDistance !== null) next.focusDistance = focusDistance;
  if (typeof command.dofEnabled === 'boolean') next.dofEnabled = command.dofEnabled;
  const exposure = clampNumber(command.exposure, CAMERA_LIMITS.exposure.min, CAMERA_LIMITS.exposure.max);
  if (exposure !== null) next.exposure = exposure;
  const near = clampNumber(command.near, CAMERA_LIMITS.near.min, CAMERA_LIMITS.near.max);
  if (near !== null) next.near = near;
  const far = clampNumber(command.far, CAMERA_LIMITS.far.min, CAMERA_LIMITS.far.max);
  if (far !== null) next.far = Math.max(far, next.near * 10);
  const orthographicFrustumHeight = clampNumber(command.orthographicFrustumHeight, 0.001, 1000000);
  if (orthographicFrustumHeight !== null) next.orthographicFrustumHeight = orthographicFrustumHeight;

  if (command.focusOnTarget) {
    next.focusDistance = Math.max(
      new THREE.Vector3(
        next.position[0] - next.target[0],
        next.position[1] - next.target[1],
        next.position[2] - next.target[2],
      ).length(),
      CAMERA_LIMITS.focusDistance.min,
    );
  }
  return next;
}

/** Read back the session state from the live three.js camera + controls,
 *  keeping lens/session parameters from the stored state. */
export function syncStateFromLiveCamera(
  state: PhysicalCameraState,
  camera: THREE.PerspectiveCamera | THREE.OrthographicCamera,
  target: THREE.Vector3,
): PhysicalCameraState {
  const next: PhysicalCameraState = {
    ...state,
    position: camera.position.toArray() as Vec3Tuple,
    target: target.toArray() as Vec3Tuple,
    up: camera.up.toArray() as Vec3Tuple,
    projection: camera instanceof THREE.OrthographicCamera ? 'orthographic' : 'perspective',
    near: camera.near,
    far: camera.far,
    ...(camera instanceof THREE.OrthographicCamera
      ? { orthographicFrustumHeight: Number(camera.userData.frustumHeight) || (camera.top - camera.bottom) }
      : {}),
  };
  if (camera instanceof THREE.PerspectiveCamera) {
    next.focalLengthMm = fovToFocalLength(camera.fov, state.sensorHeightMm);
  }
  return next;
}
