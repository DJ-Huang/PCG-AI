import * as THREE from 'three';

export const REVIEW_CAMERA_VIEWS = ['front', 'side', 'top', 'three-quarter'] as const;
export type ReviewCameraView = (typeof REVIEW_CAMERA_VIEWS)[number];

export const FRONT_AXES = ['+x', '-x', '+z', '-z'] as const;
export type FrontAxis = (typeof FRONT_AXES)[number];

export const SIDE_VIEWS = ['right', 'left'] as const;
export type SideView = (typeof SIDE_VIEWS)[number];

export interface ReviewCameraRequest {
  view: ReviewCameraView;
  frontAxis?: FrontAxis;
  sideView?: SideView;
  margin?: number;
}

export interface ReviewCameraPose {
  view: ReviewCameraView;
  projection: 'orthographic' | 'perspective';
  frontAxis: FrontAxis;
  sideView: SideView;
  position: [number, number, number];
  target: [number, number, number];
  up: [number, number, number];
  near: number;
  far: number;
  fov: number;
  frustumHeight: number;
}

export function parseFrontAxis(value: string | null | undefined): FrontAxis {
  return FRONT_AXES.includes(value as FrontAxis) ? (value as FrontAxis) : '+z';
}

export function parseSideView(value: string | null | undefined): SideView {
  return SIDE_VIEWS.includes(value as SideView) ? (value as SideView) : 'right';
}

export function parseReviewCameraView(value: string | null | undefined): ReviewCameraView | null {
  return REVIEW_CAMERA_VIEWS.includes(value as ReviewCameraView)
    ? (value as ReviewCameraView)
    : null;
}

export function unityAxisVector(axis: FrontAxis): THREE.Vector3 {
  switch (axis) {
    case '+x':
      return new THREE.Vector3(1, 0, 0);
    case '-x':
      return new THREE.Vector3(-1, 0, 0);
    case '+z':
      return new THREE.Vector3(0, 0, 1);
    case '-z':
      return new THREE.Vector3(0, 0, -1);
  }
}

/** Unity LH direction → three.js RH after the viewport Z flip. */
export function unityToThreeDir(unity: THREE.Vector3): THREE.Vector3 {
  return new THREE.Vector3(unity.x, unity.y, -unity.z);
}

export function objectFrame(frontAxis: FrontAxis, sideView: SideView) {
  const frontUnity = unityAxisVector(frontAxis);
  const upUnity = new THREE.Vector3(0, 1, 0);
  const rightUnity = new THREE.Vector3().crossVectors(upUnity, frontUnity);
  if (rightUnity.lengthSq() < 1e-8) rightUnity.set(1, 0, 0);
  else rightUnity.normalize();
  const front = unityToThreeDir(frontUnity).normalize();
  const right = unityToThreeDir(rightUnity).normalize();
  const up = new THREE.Vector3(0, 1, 0);
  return {
    front,
    right: sideView === 'right' ? right : right.clone().negate(),
    up,
  };
}

export function viewOffsetAndUp(
  view: ReviewCameraView,
  frontAxis: FrontAxis,
  sideView: SideView,
): { offset: THREE.Vector3; up: THREE.Vector3 } {
  const frame = objectFrame(frontAxis, sideView);
  if (view === 'front') return { offset: frame.front.clone(), up: frame.up.clone() };
  if (view === 'side') return { offset: frame.right.clone(), up: frame.up.clone() };
  if (view === 'top') return { offset: frame.up.clone(), up: frame.front.clone().negate() };
  const offset = frame.front
    .clone()
    .multiplyScalar(0.7)
    .add(frame.up.clone().multiplyScalar(0.6))
    .add(frame.right.clone().multiplyScalar(0.7))
    .normalize();
  return { offset, up: frame.up.clone() };
}

export function positionsBoundingBox(positions: Float32Array): THREE.Box3 {
  const box = new THREE.Box3();
  const point = new THREE.Vector3();
  for (let i = 0; i + 2 < positions.length; i += 3) {
    box.expandByPoint(point.set(positions[i], positions[i + 1], positions[i + 2]));
  }
  return box;
}

export function computeReviewCameraPose(
  positions: Float32Array,
  request: ReviewCameraRequest,
  aspect = 1,
): ReviewCameraPose | null {
  if (positions.length < 3) return null;
  const frontAxis = request.frontAxis ?? '+z';
  const sideView = request.sideView ?? 'right';
  const margin = request.margin ?? 1.2;
  const box = positionsBoundingBox(positions);
  if (box.isEmpty()) return null;

  const target = new THREE.Vector3();
  box.getCenter(target);
  const size = new THREE.Vector3();
  box.getSize(size);
  const radius = Math.max(size.length() * 0.5, 0.001);

  const { offset, up } = viewOffsetAndUp(request.view, frontAxis, sideView);
  const look = offset.clone().negate().normalize();
  const right = new THREE.Vector3().crossVectors(look, up);
  if (right.lengthSq() < 1e-8) right.set(1, 0, 0);
  else right.normalize();
  const camUp = new THREE.Vector3().crossVectors(right, look).normalize();

  let maxRight = 0;
  let maxUp = 0;
  const point = new THREE.Vector3();
  for (let i = 0; i + 2 < positions.length; i += 3) {
    point.set(positions[i], positions[i + 1], positions[i + 2]).sub(target);
    maxRight = Math.max(maxRight, Math.abs(point.dot(right)));
    maxUp = Math.max(maxUp, Math.abs(point.dot(camUp)));
  }

  const safeAspect = Math.max(aspect, 0.01);
  const frustumHeight = 2 * Math.max(maxUp, maxRight / safeAspect, 0.001) * margin;
  const distance = Math.max(radius * 2.5, frustumHeight, 0.08);
  const position = target.clone().addScaledVector(offset, distance);
  const near = Math.max(distance / 1000, 0.0001);
  const far = Math.max(distance * 100, radius * 20, 10);

  return {
    view: request.view,
    projection: request.view === 'three-quarter' ? 'perspective' : 'orthographic',
    frontAxis,
    sideView,
    position: [position.x, position.y, position.z],
    target: [target.x, target.y, target.z],
    up: [camUp.x, camUp.y, camUp.z],
    near,
    far,
    fov: 50,
    frustumHeight,
  };
}

export function applyReviewCameraPose(
  camera: THREE.PerspectiveCamera | THREE.OrthographicCamera,
  pose: ReviewCameraPose,
  aspect: number,
): void {
  camera.up.set(pose.up[0], pose.up[1], pose.up[2]);
  camera.position.set(pose.position[0], pose.position[1], pose.position[2]);
  camera.near = pose.near;
  camera.far = pose.far;
  if (camera instanceof THREE.OrthographicCamera) {
    const halfH = pose.frustumHeight / 2;
    const halfW = halfH * Math.max(aspect, 0.01);
    camera.left = -halfW;
    camera.right = halfW;
    camera.top = halfH;
    camera.bottom = -halfH;
    camera.userData.frustumHeight = pose.frustumHeight;
  } else {
    camera.fov = pose.fov;
    camera.aspect = Math.max(aspect, 0.01);
  }
  camera.lookAt(pose.target[0], pose.target[1], pose.target[2]);
  camera.updateProjectionMatrix();
}
