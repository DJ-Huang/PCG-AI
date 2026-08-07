// previewSplineGizmo.ts — Screen-space control points + XYZ axis gizmo for spline edit.
// Mirrors Unity PcgCreateSplineSceneHandles manual position handle (axis pick + drag).

import * as THREE from 'three';
import type { Vec3 } from './splineControlPoints';

export type AxisDragMode = 'free' | 'x' | 'y' | 'z';

export const AXIS_COLORS = { x: 0xff5555, y: 0x55ff55, z: 0x5599ff } as const;

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

/** ~50px on screen — matches Unity HandleUtility.GetHandleSize feel. */
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

/** @deprecated Use gizmoLengthForCamera — kept for scene-radius fallback only. */
export function gizmoLengthForScene(sceneRadius: number): number {
  return Math.max(sceneRadius * 0.22, sceneRadius * 0.04);
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

/** RGB axis arrows at `position` (three.js world space). `length` = full arrow span. */
export function buildAxisGizmo(position: THREE.Vector3, length: number): THREE.Group {
  const group = new THREE.Group();
  group.position.copy(position);
  group.userData.kind = 'axis-gizmo';
  group.userData.baseLength = length;

  const axes: Array<{ mode: AxisDragMode; dir: THREE.Vector3; color: number }> = [
    { mode: 'x', dir: new THREE.Vector3(1, 0, 0), color: AXIS_COLORS.x },
    { mode: 'y', dir: new THREE.Vector3(0, 1, 0), color: AXIS_COLORS.y },
    { mode: 'z', dir: new THREE.Vector3(0, 0, 1), color: AXIS_COLORS.z },
  ];

  const headRadius = length * 0.028;
  const headHeight = length * 0.05;
  const shaftEnd = length * 0.82;

  for (const { mode, dir, color } of axes) {
    const end = dir.clone().multiplyScalar(shaftEnd);
    const geo = new THREE.BufferGeometry().setFromPoints([new THREE.Vector3(), end]);
    const line = new THREE.Line(geo, new THREE.LineBasicMaterial({ color }));
    line.userData = { kind: 'axis', mode, dir: dir.clone(), length };
    group.add(line);

    const tip = dir.clone().multiplyScalar(length);
    const head = new THREE.Mesh(
      new THREE.ConeGeometry(headRadius, headHeight, 6),
      new THREE.MeshBasicMaterial({ color }),
    );
    head.position.copy(tip).add(dir.clone().multiplyScalar(-headHeight * 0.5));
    head.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), dir.clone().normalize());
    head.userData = { kind: 'axis', mode, dir: dir.clone(), length };
    group.add(head);
  }

  const center = new THREE.Mesh(
    new THREE.SphereGeometry(length * 0.032, 8, 8),
    new THREE.MeshBasicMaterial({ color: 0xffaa44 }),
  );
  center.userData = { kind: 'axis-center' };
  group.add(center);

  return group;
}

/** Keep gizmo ~constant screen size when the camera moves/zooms. */
export function rescaleAxisGizmo(group: THREE.Group, length: number): void {
  const base = (group.userData.baseLength as number) || 1;
  group.scale.setScalar(length / base);
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
}

/** Pick XYZ axis arrow or center (free) on the active gizmo. */
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

  if (Math.hypot(mx - center.x, my - center.y) <= 10) {
    return { mode: 'free', axis: new THREE.Vector3(), screenAxis: new THREE.Vector2(), worldPerPixel: 0 };
  }

  const axes: Array<{ mode: AxisDragMode; dir: THREE.Vector3 }> = [
    { mode: 'x', dir: new THREE.Vector3(1, 0, 0) },
    { mode: 'y', dir: new THREE.Vector3(0, 1, 0) },
    { mode: 'z', dir: new THREE.Vector3(0, 0, 1) },
  ];

  let best: AxisPickResult | null = null;
  let bestDist = 10;
  for (const { mode, dir } of axes) {
    const endWorld = gizmoWorldPos.clone().add(dir.clone().multiplyScalar(gizmoLength));
    const end = toScreen(endWorld, camera, rect);
    if (!end.visible) continue;
    const projX = end.x - center.x;
    const projY = end.y - center.y;
    const projLen = Math.hypot(projX, projY);
    if (projLen < 8) continue;
    const ux = projX / projLen;
    const uy = projY / projLen;
    const d = distToSegment(mx, my, center.x + ux * 8, center.y + uy * 8, end.x, end.y);
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
): DragState {
  const startPointThree = unityToThree(points[index]);
  const normal = new THREE.Vector3();
  camera.getWorldDirection(normal);
  const plane = new THREE.Plane().setFromNormalAndCoplanarPoint(normal, startPointThree);
  const planeHit = startPointThree.clone();
  if (mode === 'free') {
    const ray = screenRay(camera, rect, clientX, clientY);
    const hit = new THREE.Vector3();
    if (ray.intersectPlane(plane, hit)) planeHit.copy(hit);
  }
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

  if (drag.mode === 'free') {
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
