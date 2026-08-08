// previewSplineGizmo.ts — Screen-space control points + Unity-style position handle.
// Visual parity: thin RGB axes + cone caps, XY/XZ/YZ plane quads, center cube.

import * as THREE from 'three';
import type { Vec3 } from './splineControlPoints';

export type AxisDragMode = 'free' | 'x' | 'y' | 'z' | 'xy' | 'xz' | 'yz';

/** Unity Handles.x/y/zAxisColor approximations */
export const AXIS_COLORS = { x: 0xdc3838, y: 0x6bd968, z: 0x4fa8e0 } as const;

const UNSELECTED_POINT_PX = 4;
const SELECTED_POINT_PX = 7;
const POINT_COLOR = new THREE.Color(0x40d9ff);
const SELECTED_POINT_COLOR = new THREE.Color(0xff7333);

/** Unity LH → three.js RH */
export function unityToThree(v: Vec3): THREE.Vector3 {
  return new THREE.Vector3(v.x, v.y, -v.z);
}

/** three.js RH → Unity LH */
export function threeToUnity(v: THREE.Vector3): Vec3 {
  return { x: v.x, y: v.y, z: -v.z };
}

/** ~3.5px screen-space shaft radius (Unity DrawAAPolyLine ≈ 3px). */
export function gizmoLineRadiusForCamera(
  camera: THREE.PerspectiveCamera,
  worldPos: THREE.Vector3,
  canvasHeight: number,
  targetPx = 3.5,
): number {
  if (canvasHeight <= 0) return 0.001;
  const distance = Math.max(camera.position.distanceTo(worldPos), 1e-6);
  const fovRad = (camera.fov * Math.PI) / 180;
  const worldPerPixel = (2 * distance * Math.tan(fovRad / 2)) / canvasHeight;
  return targetPx * worldPerPixel * 0.5;
}

/** ~80% of handle size, same as Unity GetHandleSize * 0.8 target in screen px. */
export function gizmoLengthForCamera(
  camera: THREE.PerspectiveCamera,
  worldPos: THREE.Vector3,
  canvasHeight: number,
  targetPx = 50,
): number {
  if (canvasHeight <= 0) return 0.01;
  const distance = Math.max(camera.position.distanceTo(worldPos), 1e-6);
  const fovRad = (camera.fov * Math.PI) / 180;
  const worldPerPixel = (2 * distance * Math.tan(fovRad / 2)) / canvasHeight;
  return targetPx * worldPerPixel;
}

/** Screen-space control points — fixed pixel size, no world-scale overlap. */
export function buildControlPointCloud(
  points: readonly Vec3[],
  selectedIndex: number,
): THREE.Points {
  const positions = new Float32Array(points.length * 3);
  const colors = new Float32Array(points.length * 3);
  for (let i = 0; i < points.length; i++) {
    const v = unityToThree(points[i]);
    positions[i * 3] = v.x;
    positions[i * 3 + 1] = v.y;
    positions[i * 3 + 2] = v.z;
    const c = i === selectedIndex ? SELECTED_POINT_COLOR : POINT_COLOR;
    colors[i * 3] = c.r;
    colors[i * 3 + 1] = c.g;
    colors[i * 3 + 2] = c.b;
  }
  const geo = new THREE.BufferGeometry();
  geo.setAttribute('position', new THREE.BufferAttribute(positions, 3));
  geo.setAttribute('color', new THREE.BufferAttribute(colors, 3));
  const mat = new THREE.PointsMaterial({
    size: UNSELECTED_POINT_PX,
    sizeAttenuation: false,
    vertexColors: true,
    depthTest: true,
  });
  const cloud = new THREE.Points(geo, mat);
  cloud.userData.kind = 'control-points';
  return cloud;
}

export function updateControlPointCloud(
  cloud: THREE.Points,
  points: readonly Vec3[],
  selectedIndex: number,
): void {
  const pos = cloud.geometry.getAttribute('position') as THREE.BufferAttribute;
  const col = cloud.geometry.getAttribute('color') as THREE.BufferAttribute;
  for (let i = 0; i < points.length; i++) {
    const v = unityToThree(points[i]);
    pos.setXYZ(i, v.x, v.y, v.z);
    const c = i === selectedIndex ? SELECTED_POINT_COLOR : POINT_COLOR;
    col.setXYZ(i, c.r, c.g, c.b);
  }
  pos.needsUpdate = true;
  col.needsUpdate = true;
  const mat = cloud.material as THREE.PointsMaterial;
  mat.size = selectedIndex >= 0 ? SELECTED_POINT_PX : UNSELECTED_POINT_PX;
}

function alignCylinderY(mesh: THREE.Mesh, dir: THREE.Vector3, halfLen: number): void {
  mesh.position.copy(dir.clone().multiplyScalar(halfLen));
  mesh.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), dir.clone().normalize());
}

function axisMaterial(color: number): THREE.MeshBasicMaterial {
  return new THREE.MeshBasicMaterial({
    color,
    depthTest: false,
    depthWrite: false,
    transparent: true,
  });
}

function addAxis(
  group: THREE.Group,
  mode: AxisDragMode,
  dir: THREE.Vector3,
  color: number,
  length: number,
  lineRadius: number,
): void {
  const capH = length * 0.16;
  const capR = Math.max(capH * 0.38, lineRadius * 2.2);
  const shaftR = lineRadius;
  const shaftLen = Math.max(length - capH, length * 0.5);

  const shaft = new THREE.Mesh(
    new THREE.CylinderGeometry(shaftR, shaftR, shaftLen, 8),
    axisMaterial(color),
  );
  alignCylinderY(shaft, dir, shaftLen * 0.5);
  shaft.userData = { kind: 'axis', mode };
  group.add(shaft);

  const tip = dir.clone().multiplyScalar(length);
  const head = new THREE.Mesh(
    new THREE.ConeGeometry(capR, capH, 12),
    axisMaterial(color),
  );
  head.position.copy(tip).add(dir.clone().multiplyScalar(-capH * 0.5));
  head.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), dir.clone().normalize());
  head.userData = { kind: 'axis', mode };
  group.add(head);
}

function addPlaneHandle(
  group: THREE.Group,
  mode: AxisDragMode,
  normal: THREE.Vector3,
  tangentA: THREE.Vector3,
  tangentB: THREE.Vector3,
  color: number,
  length: number,
): void {
  const half = length * 0.1;
  const center = tangentA
    .clone()
    .multiplyScalar(half)
    .add(tangentB.clone().multiplyScalar(half));
  const geo = new THREE.PlaneGeometry(half * 2, half * 2);
  const mat = new THREE.MeshBasicMaterial({
    color,
    transparent: true,
    opacity: 0.55,
    side: THREE.DoubleSide,
    depthTest: false,
    depthWrite: false,
  });
  const mesh = new THREE.Mesh(geo, mat);
  mesh.position.copy(center);
  const basis = new THREE.Matrix4().makeBasis(
    tangentA.clone().normalize(),
    tangentB.clone().normalize(),
    normal.clone().normalize(),
  );
  mesh.quaternion.setFromRotationMatrix(basis);
  mesh.userData = { kind: 'plane', mode, normal: normal.clone() };
  group.add(mesh);
}

/** Unity-style position handle at `position` (three.js world). `length` = axis span. */
export function buildAxisGizmo(
  position: THREE.Vector3,
  length: number,
  lineRadius: number,
): THREE.Group {
  const group = new THREE.Group();
  group.position.copy(position);
  group.userData.kind = 'axis-gizmo';
  group.userData.baseLength = length;
  group.renderOrder = 999;

  addAxis(group, 'x', new THREE.Vector3(1, 0, 0), AXIS_COLORS.x, length, lineRadius);
  addAxis(group, 'y', new THREE.Vector3(0, 1, 0), AXIS_COLORS.y, length, lineRadius);
  addAxis(group, 'z', new THREE.Vector3(0, 0, 1), AXIS_COLORS.z, length, lineRadius);

  addPlaneHandle(
    group,
    'yz',
    new THREE.Vector3(1, 0, 0),
    new THREE.Vector3(0, 1, 0),
    new THREE.Vector3(0, 0, 1),
    AXIS_COLORS.x,
    length,
  );
  addPlaneHandle(
    group,
    'xz',
    new THREE.Vector3(0, 1, 0),
    new THREE.Vector3(1, 0, 0),
    new THREE.Vector3(0, 0, 1),
    AXIS_COLORS.y,
    length,
  );
  addPlaneHandle(
    group,
    'xy',
    new THREE.Vector3(0, 0, 1),
    new THREE.Vector3(1, 0, 0),
    new THREE.Vector3(0, 1, 0),
    AXIS_COLORS.z,
    length,
  );

  const cubeSize = length * 0.13;
  const center = new THREE.Mesh(
    new THREE.BoxGeometry(cubeSize, cubeSize, cubeSize),
    new THREE.MeshBasicMaterial({
      color: 0xd8d8d8,
      transparent: true,
      opacity: 0.75,
      depthTest: false,
      depthWrite: false,
    }),
  );
  center.userData = { kind: 'axis-center' };
  group.add(center);

  return group;
}

export function rescaleAxisGizmo(group: THREE.Group, length: number): void {
  const base = (group.userData.baseLength as number) || 1;
  const s = length / base;
  group.scale.set(s, s, s);
  group.userData.currentLength = length;
}

export function updateAxisGizmoPosition(group: THREE.Group, position: THREE.Vector3): void {
  group.position.copy(position);
}

function toScreen(
  v: THREE.Vector3,
  camera: THREE.PerspectiveCamera,
  rect: DOMRect,
): { x: number; y: number; visible: boolean } {
  const p = v.clone().project(camera);
  return {
    x: (p.x * 0.5 + 0.5) * rect.width,
    y: (-p.y * 0.5 + 0.5) * rect.height,
    visible: p.z < 1,
  };
}

function distToSegment(px: number, py: number, x1: number, y1: number, x2: number, y2: number): number {
  const dx = x2 - x1;
  const dy = y2 - y1;
  const len2 = dx * dx + dy * dy;
  if (len2 < 1e-6) return Math.hypot(px - x1, py - y1);
  const t = Math.max(0, Math.min(1, ((px - x1) * dx + (py - y1) * dy) / len2));
  return Math.hypot(px - (x1 + t * dx), py - (y1 + t * dy));
}

export function pickControlPoint(
  camera: THREE.PerspectiveCamera,
  rect: DOMRect,
  clientX: number,
  clientY: number,
  points: readonly Vec3[],
  pickRadiusPx = 12,
): number {
  const mx = clientX - rect.left;
  const my = clientY - rect.top;
  let best = -1;
  let bestDist = pickRadiusPx;
  const v = new THREE.Vector3();
  for (let i = 0; i < points.length; i++) {
    v.copy(unityToThree(points[i]));
    const s = toScreen(v, camera, rect);
    if (!s.visible) continue;
    const d = Math.hypot(s.x - mx, s.y - my);
    if (d < bestDist) {
      bestDist = d;
      best = i;
    }
  }
  return best;
}

export interface AxisPickResult {
  mode: AxisDragMode;
  axis: THREE.Vector3;
  screenAxis: THREE.Vector2;
  worldPerPixel: number;
  planeNormal?: THREE.Vector3;
}

const PLANE_SPECS: Array<{
  mode: AxisDragMode;
  normal: THREE.Vector3;
  tanA: THREE.Vector3;
  tanB: THREE.Vector3;
}> = [
  { mode: 'yz', normal: new THREE.Vector3(1, 0, 0), tanA: new THREE.Vector3(0, 1, 0), tanB: new THREE.Vector3(0, 0, 1) },
  { mode: 'xz', normal: new THREE.Vector3(0, 1, 0), tanA: new THREE.Vector3(1, 0, 0), tanB: new THREE.Vector3(0, 0, 1) },
  { mode: 'xy', normal: new THREE.Vector3(0, 0, 1), tanA: new THREE.Vector3(1, 0, 0), tanB: new THREE.Vector3(0, 1, 0) },
];

/** Pick plane quad, center cube, or XYZ axis (Unity position-handle order). */
export function pickAxisGizmo(
  camera: THREE.PerspectiveCamera,
  rect: DOMRect,
  clientX: number,
  clientY: number,
  gizmoWorldPos: THREE.Vector3,
  gizmoLength: number,
): AxisPickResult | null {
  const mx = clientX - rect.left;
  const my = clientY - rect.top;
  const center = toScreen(gizmoWorldPos, camera, rect);
  if (!center.visible) return null;

  const planeHalf = gizmoLength * 0.1;
  for (const spec of PLANE_SPECS) {
    const worldCenter = gizmoWorldPos
      .clone()
      .add(spec.tanA.clone().multiplyScalar(planeHalf))
      .add(spec.tanB.clone().multiplyScalar(planeHalf));
    const sc = toScreen(worldCenter, camera, rect);
    if (!sc.visible) continue;
    const cornerA = toScreen(
      worldCenter.clone().add(spec.tanA.clone().multiplyScalar(planeHalf)),
      camera,
      rect,
    );
    const cornerB = toScreen(
      worldCenter.clone().add(spec.tanB.clone().multiplyScalar(planeHalf)),
      camera,
      rect,
    );
    const pickR = Math.max(
      8,
      Math.hypot(cornerA.x - sc.x, cornerA.y - sc.y),
      Math.hypot(cornerB.x - sc.x, cornerB.y - sc.y),
    );
    if (Math.hypot(mx - sc.x, my - sc.y) <= pickR) {
      return {
        mode: spec.mode,
        axis: new THREE.Vector3(),
        screenAxis: new THREE.Vector2(),
        worldPerPixel: 0,
        planeNormal: spec.normal.clone(),
      };
    }
  }

  const cubePickR = Math.max(10, gizmoLength * 0.13 * 0.6 * (rect.width / 400));
  if (Math.hypot(mx - center.x, my - center.y) <= cubePickR) {
    return { mode: 'free', axis: new THREE.Vector3(), screenAxis: new THREE.Vector2(), worldPerPixel: 0 };
  }

  const axes: Array<{ mode: AxisDragMode; dir: THREE.Vector3 }> = [
    { mode: 'x', dir: new THREE.Vector3(1, 0, 0) },
    { mode: 'y', dir: new THREE.Vector3(0, 1, 0) },
    { mode: 'z', dir: new THREE.Vector3(0, 0, 1) },
  ];

  let best: AxisPickResult | null = null;
  let bestDist = 9;
  const skip = gizmoLength * 0.12;
  for (const { mode, dir } of axes) {
    const endWorld = gizmoWorldPos.clone().add(dir.clone().multiplyScalar(gizmoLength));
    const end = toScreen(endWorld, camera, rect);
    if (!end.visible) continue;
    const projX = end.x - center.x;
    const projY = end.y - center.y;
    const projLen = Math.hypot(projX, projY);
    if (projLen < 10) continue;
    const ux = projX / projLen;
    const uy = projY / projLen;
    const d = distToSegment(
      mx,
      my,
      center.x + ux * skip,
      center.y + uy * skip,
      end.x,
      end.y,
    );
    if (d < bestDist) {
      bestDist = d;
      best = {
        mode,
        axis: dir.clone(),
        screenAxis: new THREE.Vector2(ux, uy),
        worldPerPixel: gizmoLength / projLen,
      };
    }
  }
  return best;
}

export interface DragState {
  index: number;
  mode: AxisDragMode;
  axis: THREE.Vector3;
  screenAxis: THREE.Vector2;
  worldPerPixel: number;
  startMouse: THREE.Vector2;
  plane: THREE.Plane;
  planeHit: THREE.Vector3;
  startPointThree: THREE.Vector3;
  livePoints: Vec3[];
}

function dragPlaneForMode(mode: AxisDragMode, point: THREE.Vector3): THREE.Plane {
  if (mode === 'xy') return new THREE.Plane(new THREE.Vector3(0, 0, 1), -point.z);
  if (mode === 'xz') return new THREE.Plane(new THREE.Vector3(0, 1, 0), -point.y);
  if (mode === 'yz') return new THREE.Plane(new THREE.Vector3(1, 0, 0), -point.x);
  const normal = new THREE.Vector3();
  return new THREE.Plane(normal, 0);
}

export function beginDrag(
  index: number,
  points: readonly Vec3[],
  mode: AxisDragMode,
  axis: THREE.Vector3,
  screenAxis: THREE.Vector2,
  worldPerPixel: number,
  camera: THREE.PerspectiveCamera,
  clientX: number,
  clientY: number,
  rect: DOMRect,
  planeNormal?: THREE.Vector3,
): DragState {
  const startPointThree = unityToThree(points[index]);
  let plane: THREE.Plane;
  if (mode === 'xy' || mode === 'xz' || mode === 'yz') {
    plane = dragPlaneForMode(mode, startPointThree);
  } else if (mode === 'free') {
    const normal = new THREE.Vector3();
    camera.getWorldDirection(normal);
    plane = new THREE.Plane().setFromNormalAndCoplanarPoint(normal, startPointThree);
  } else {
    plane = new THREE.Plane();
  }

  const planeHit = startPointThree.clone();
  if (mode === 'free' || mode === 'xy' || mode === 'xz' || mode === 'yz') {
    const ray = screenRay(camera, rect, clientX, clientY);
    const hit = new THREE.Vector3();
    if (ray.intersectPlane(plane, hit)) planeHit.copy(hit);
  }
  void planeNormal;

  return {
    index,
    mode,
    axis,
    screenAxis,
    worldPerPixel,
    startMouse: new THREE.Vector2(clientX - rect.left, clientY - rect.top),
    plane,
    planeHit,
    startPointThree,
    livePoints: points.map((p) => ({ ...p })),
  };
}

export function dragPoint(
  drag: DragState,
  camera: THREE.PerspectiveCamera,
  clientX: number,
  clientY: number,
  rect: DOMRect,
): Vec3 {
  const mx = clientX - rect.left;
  const my = clientY - rect.top;

  if (drag.mode === 'free' || drag.mode === 'xy' || drag.mode === 'xz' || drag.mode === 'yz') {
    const ray = screenRay(camera, rect, clientX, clientY);
    const hit = new THREE.Vector3();
    if (!ray.intersectPlane(drag.plane, hit)) return drag.livePoints[drag.index];
    const delta = hit.sub(drag.planeHit);
    const next = drag.startPointThree.clone().add(delta);
    return threeToUnity(next);
  }

  const pixels = (mx - drag.startMouse.x) * drag.screenAxis.x + (my - drag.startMouse.y) * drag.screenAxis.y;
  const amount = pixels * drag.worldPerPixel;
  const next = drag.startPointThree.clone().add(drag.axis.clone().multiplyScalar(amount));
  return threeToUnity(next);
}

function screenRay(
  camera: THREE.PerspectiveCamera,
  rect: DOMRect,
  clientX: number,
  clientY: number,
): THREE.Ray {
  const ndc = new THREE.Vector2(
    ((clientX - rect.left) / rect.width) * 2 - 1,
    -((clientY - rect.top) / rect.height) * 2 + 1,
  );
  const raycaster = new THREE.Raycaster();
  raycaster.setFromCamera(ndc, camera);
  return raycaster.ray;
}

export function disposeObject3D(obj: THREE.Object3D): void {
  obj.traverse((child) => {
    if (child instanceof THREE.Mesh || child instanceof THREE.Line || child instanceof THREE.Points) {
      child.geometry.dispose();
      const mat = child.material as THREE.Material | THREE.Material[];
      if (Array.isArray(mat)) mat.forEach((m) => m.dispose());
      else mat.dispose();
    }
  });
}
