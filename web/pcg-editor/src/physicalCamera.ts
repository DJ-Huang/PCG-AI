// physicalCamera.ts — session-scoped "physical camera" model for the preview
// viewport. Lens parameters follow photographic conventions (focal length in
// mm, aperture in f-stops, focus distance in world units) and are converted to
// three.js camera / BokehPass values here. Coordinates are viewport world
// space (three.js right-handed; Unity +Z is flipped to -Z), matching the
// camera block in PreviewCapture metadata.

import * as THREE from 'three';

export const CAMERA_PROJECTIONS = ['perspective', 'orthographic'] as const;
export type CameraProjection = (typeof CAMERA_PROJECTIONS)[number];
export const CAMERA_SENSOR_FITS = ['auto', 'horizontal', 'vertical'] as const;
export type CameraSensorFit = (typeof CAMERA_SENSOR_FITS)[number];
export type ResolvedCameraSensorFit = Exclude<CameraSensorFit, 'auto'>;

export interface PhysicalCameraState {
  position: [number, number, number];
  target: [number, number, number];
  up: [number, number, number];
  projection: CameraProjection;
  /** Blender Camera.lens, in millimetres. */
  focalLengthMm: number;
  /** Blender Camera.sensor_width. */
  sensorWidthMm: number;
  /** Blender Camera.sensor_height. */
  sensorHeightMm: number;
  /** Blender Camera.sensor_fit. */
  sensorFit: CameraSensorFit;
  /** Blender Camera.shift_x / shift_y. */
  shiftX: number;
  shiftY: number;
  apertureFstop: number;
  apertureBlades: number;
  apertureRotationDeg: number;
  apertureRatio: number;
  /** World-unit focus distance for depth of field. */
  focusDistance: number;
  dofEnabled: boolean;
  /** toneMappingExposure multiplier. */
  exposure: number;
  near: number;
  far: number;
  /** Blender Camera.ortho_scale (fit-axis extent, not always vertical). */
  orthographicScale: number;
}

export const FULL_FRAME_SENSOR_WIDTH_MM = 36;
export const FULL_FRAME_SENSOR_HEIGHT_MM = 24;

export const CAMERA_LIMITS = {
  focalLengthMm: { min: 8, max: 400 },
  sensorWidthMm: { min: 1, max: 100 },
  sensorHeightMm: { min: 5, max: 70 },
  shift: { min: -2, max: 2 },
  apertureFstop: { min: 0.7, max: 64 },
  apertureBlades: { min: 0, max: 16 },
  apertureRotationDeg: { min: -180, max: 180 },
  apertureRatio: { min: 0.01, max: 1 },
  focusDistance: { min: 0.01, max: 100000 },
  exposure: { min: 0.05, max: 8 },
  near: { min: 0.0001, max: 1000 },
  far: { min: 0.1, max: 1000000 },
  orthographicScale: { min: 0.001, max: 1000000 },
} as const;

export function defaultPhysicalCamera(): PhysicalCameraState {
  return {
    position: [3, 2.5, 4],
    target: [0, 0, 0],
    up: [0, 1, 0],
    projection: 'perspective',
    focalLengthMm: 26,
    sensorWidthMm: FULL_FRAME_SENSOR_WIDTH_MM,
    sensorHeightMm: FULL_FRAME_SENSOR_HEIGHT_MM,
    sensorFit: 'auto',
    shiftX: 0,
    shiftY: 0,
    apertureFstop: 8,
    apertureBlades: 0,
    apertureRotationDeg: 0,
    apertureRatio: 1,
    focusDistance: 5.7,
    dofEnabled: false,
    exposure: 1,
    near: 0.01,
    far: 5000,
    orthographicScale: 6,
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

export interface BlenderProjection {
  resolvedSensorFit: ResolvedCameraSensorFit;
  verticalFovDeg: number;
  orthographicWidth: number;
  orthographicHeight: number;
  /** Projection-centre offset in NDC, matching Blender's viewplane shift. */
  shiftNdc: [number, number];
}

/** Blender's AUTO fit chooses the longest output dimension. */
export function resolveBlenderSensorFit(
  sensorFit: CameraSensorFit,
  aspect: number,
): ResolvedCameraSensorFit {
  if (sensorFit !== 'auto') return sensorFit;
  return aspect >= 1 ? 'horizontal' : 'vertical';
}

/** Reproduces BKE_camera_params_compute_viewplane for square output pixels. */
export function blenderProjection(
  state: Pick<
    PhysicalCameraState,
    | 'focalLengthMm'
    | 'sensorWidthMm'
    | 'sensorHeightMm'
    | 'sensorFit'
    | 'shiftX'
    | 'shiftY'
    | 'orthographicScale'
  >,
  outputAspect: number,
): BlenderProjection {
  const aspect = Math.max(outputAspect, 0.0001);
  const resolvedSensorFit = resolveBlenderSensorFit(state.sensorFit, aspect);
  // Blender AUTO uses sensor width even when a portrait output resolves to
  // vertical fit. Explicit VERTICAL is the only mode that uses sensor height.
  const sensorSizeMm = state.sensorFit === 'vertical'
    ? state.sensorHeightMm
    : state.sensorWidthMm;
  const effectiveVerticalSensorMm = resolvedSensorFit === 'horizontal'
    ? sensorSizeMm / aspect
    : sensorSizeMm;
  const verticalFovDeg = focalLengthToFov(state.focalLengthMm, effectiveVerticalSensorMm);
  const orthographicWidth = resolvedSensorFit === 'horizontal'
    ? state.orthographicScale
    : state.orthographicScale * aspect;
  const orthographicHeight = resolvedSensorFit === 'horizontal'
    ? state.orthographicScale / aspect
    : state.orthographicScale;
  const viewFactor = resolvedSensorFit === 'horizontal' ? aspect : 1;
  return {
    resolvedSensorFit,
    verticalFovDeg,
    orthographicWidth,
    orthographicHeight,
    shiftNdc: [
      (2 * state.shiftX * viewFactor) / aspect,
      2 * state.shiftY * viewFactor,
    ],
  };
}

export function blenderVerticalFovToFocalLength(
  state: Pick<PhysicalCameraState, 'sensorWidthMm' | 'sensorHeightMm' | 'sensorFit'>,
  fovDeg: number,
  outputAspect: number,
): number {
  const aspect = Math.max(outputAspect, 0.0001);
  const resolved = resolveBlenderSensorFit(state.sensorFit, aspect);
  const sensorSizeMm = state.sensorFit === 'vertical'
    ? state.sensorHeightMm
    : state.sensorWidthMm;
  const effectiveVerticalSensorMm = resolved === 'horizontal' ? sensorSizeMm / aspect : sensorSizeMm;
  return fovToFocalLength(fovDeg, effectiveVerticalSensorMm);
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
  sensorWidthMm?: number;
  sensorHeightMm?: number;
  sensorFit?: CameraSensorFit;
  shiftX?: number;
  shiftY?: number;
  apertureFstop?: number;
  apertureBlades?: number;
  apertureRotationDeg?: number;
  apertureRatio?: number;
  focusDistance?: number;
  /** Set focusDistance to the position↔target distance. */
  focusOnTarget?: boolean;
  dofEnabled?: boolean;
  exposure?: number;
  near?: number;
  far?: number;
  orthographicScale?: number;
  /** Backward-compatible import of pre-Blender-alignment shot documents. */
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
  outputAspect = 16 / 9,
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

  const sensorWidthMm = clampNumber(
    command.sensorWidthMm,
    CAMERA_LIMITS.sensorWidthMm.min,
    CAMERA_LIMITS.sensorWidthMm.max,
  );
  if (sensorWidthMm !== null) next.sensorWidthMm = sensorWidthMm;
  const sensorHeightMm = clampNumber(
    command.sensorHeightMm,
    CAMERA_LIMITS.sensorHeightMm.min,
    CAMERA_LIMITS.sensorHeightMm.max,
  );
  if (sensorHeightMm !== null) next.sensorHeightMm = sensorHeightMm;
  if (command.sensorFit && CAMERA_SENSOR_FITS.includes(command.sensorFit)) {
    next.sensorFit = command.sensorFit;
  }
  const shiftX = clampNumber(command.shiftX, CAMERA_LIMITS.shift.min, CAMERA_LIMITS.shift.max);
  if (shiftX !== null) next.shiftX = shiftX;
  const shiftY = clampNumber(command.shiftY, CAMERA_LIMITS.shift.min, CAMERA_LIMITS.shift.max);
  if (shiftY !== null) next.shiftY = shiftY;
  const focalLengthMm = clampNumber(
    command.focalLengthMm,
    CAMERA_LIMITS.focalLengthMm.min,
    CAMERA_LIMITS.focalLengthMm.max,
  );
  if (focalLengthMm !== null) {
    next.focalLengthMm = focalLengthMm;
  } else if (command.fov !== undefined) {
    const fov = clampNumber(command.fov, 1, 170);
    if (fov !== null) {
      next.focalLengthMm = blenderVerticalFovToFocalLength(next, fov, outputAspect);
    }
  }
  const apertureFstop = clampNumber(
    command.apertureFstop,
    CAMERA_LIMITS.apertureFstop.min,
    CAMERA_LIMITS.apertureFstop.max,
  );
  if (apertureFstop !== null) next.apertureFstop = apertureFstop;
  const apertureBlades = clampNumber(
    command.apertureBlades,
    CAMERA_LIMITS.apertureBlades.min,
    CAMERA_LIMITS.apertureBlades.max,
  );
  if (apertureBlades !== null) next.apertureBlades = Math.round(apertureBlades);
  const apertureRotationDeg = clampNumber(
    command.apertureRotationDeg,
    CAMERA_LIMITS.apertureRotationDeg.min,
    CAMERA_LIMITS.apertureRotationDeg.max,
  );
  if (apertureRotationDeg !== null) next.apertureRotationDeg = apertureRotationDeg;
  const apertureRatio = clampNumber(
    command.apertureRatio,
    CAMERA_LIMITS.apertureRatio.min,
    CAMERA_LIMITS.apertureRatio.max,
  );
  if (apertureRatio !== null) next.apertureRatio = apertureRatio;
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
  const orthographicScale = clampNumber(
    command.orthographicScale ?? command.orthographicFrustumHeight,
    CAMERA_LIMITS.orthographicScale.min,
    CAMERA_LIMITS.orthographicScale.max,
  );
  if (orthographicScale !== null) next.orthographicScale = orthographicScale;

  if (command.focusOnTarget) {
    next.focusDistance = axialFocusDistance(next.position, next.target, next.target);
  }
  return next;
}

/** Blender-style focus plane: axial distance along the view direction. */
export function axialFocusDistance(
  cameraPosition: Vec3Tuple,
  lookTarget: Vec3Tuple,
  focusPoint: Vec3Tuple = lookTarget,
): number {
  const view = new THREE.Vector3(
    lookTarget[0] - cameraPosition[0],
    lookTarget[1] - cameraPosition[1],
    lookTarget[2] - cameraPosition[2],
  );
  if (view.lengthSq() < 1e-12) {
    return CAMERA_LIMITS.focusDistance.min;
  }
  const offset = new THREE.Vector3(
    focusPoint[0] - cameraPosition[0],
    focusPoint[1] - cameraPosition[1],
    focusPoint[2] - cameraPosition[2],
  );
  return Math.max(Math.abs(offset.dot(view.normalize())), CAMERA_LIMITS.focusDistance.min);
}

export function applyPhysicalProjection(
  camera: THREE.PerspectiveCamera | THREE.OrthographicCamera,
  state: PhysicalCameraState,
  aspect: number,
) {
  const projection = blenderProjection(state, aspect);
  if (camera instanceof THREE.PerspectiveCamera) {
    camera.fov = projection.verticalFovDeg;
    camera.aspect = aspect;
    camera.updateProjectionMatrix();
    camera.projectionMatrix.elements[8] = projection.shiftNdc[0];
    camera.projectionMatrix.elements[9] = projection.shiftNdc[1];
    camera.projectionMatrixInverse.copy(camera.projectionMatrix).invert();
    return;
  }
  const centerX = state.shiftX * state.orthographicScale;
  const centerY = state.shiftY * state.orthographicScale;
  const halfWidth = projection.orthographicWidth / 2;
  const halfHeight = projection.orthographicHeight / 2;
  camera.left = centerX - halfWidth;
  camera.right = centerX + halfWidth;
  camera.bottom = centerY - halfHeight;
  camera.top = centerY + halfHeight;
  camera.userData.orthographicScale = state.orthographicScale;
  camera.updateProjectionMatrix();
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
      ? { orthographicScale: Number(camera.userData.orthographicScale) || state.orthographicScale }
      : {}),
  };
  if (camera instanceof THREE.PerspectiveCamera) {
    next.focalLengthMm = blenderVerticalFovToFocalLength(state, camera.fov, camera.aspect);
  }
  return next;
}
