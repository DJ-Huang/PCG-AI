import * as THREE from 'three';

import { sampleShotCameraWorld } from '../cameraPath';
import type { PhysicalCameraState } from '../physicalCamera';
import type { ShotDocument, ShotMotionCurve } from '../shot';
import { cameraTrajectoryPoints } from '../cameraTrack';

const PATH_COLOR = 0x71b4f0;
const KEY_COLOR = 0xffc14d;
const SELECTED_COLOR = 0xff9d2e;
const TARGET_COLOR = 0x8be0a4;
const CURVE_COLOR = 0xe0a35a;
const CAMERA_BODY = 0x3d5a80;
const CAMERA_LENS = 0x1c1c20;
const CAMERA_ACTIVE = 0x6ec6ff;

export type CameraOverlayHit =
  | { kind: 'camera-key' | 'camera-target'; keyframeId: string }
  | { kind: 'camera-icon'; cameraId: string }
  | { kind: 'curve-point'; curveId: string; pointIndex: number };

function createCameraIcon(active: boolean): THREE.Group {
  const group = new THREE.Group();
  const color = active ? CAMERA_ACTIVE : CAMERA_BODY;
  const body = new THREE.Mesh(
    new THREE.BoxGeometry(0.18, 0.12, 0.22),
    new THREE.MeshBasicMaterial({ color }),
  );
  body.position.z = 0.14;
  group.add(body);

  const lens = new THREE.Mesh(
    new THREE.CylinderGeometry(0.04, 0.05, 0.07, 12),
    new THREE.MeshBasicMaterial({ color: CAMERA_LENS }),
  );
  lens.rotation.x = Math.PI / 2;
  lens.position.z = 0.01;
  group.add(lens);

  const hw = 0.2;
  const hh = 0.12;
  const depth = -0.48;
  const origin = new THREE.Vector3(0, 0, 0);
  const corners = [
    new THREE.Vector3(-hw, -hh, depth),
    new THREE.Vector3(hw, -hh, depth),
    new THREE.Vector3(hw, hh, depth),
    new THREE.Vector3(-hw, hh, depth),
  ];
  const frustumPoints: THREE.Vector3[] = [];
  for (let index = 0; index < 4; index += 1) {
    frustumPoints.push(origin, corners[index], corners[index], corners[(index + 1) % 4]);
  }
  group.add(new THREE.LineSegments(
    new THREE.BufferGeometry().setFromPoints(frustumPoints),
    new THREE.LineBasicMaterial({ color, transparent: true, opacity: 0.9 }),
  ));
  group.traverse((child) => {
    child.userData.kind = 'camera-icon';
  });
  return group;
}

function orientCameraIcon(group: THREE.Group, state: PhysicalCameraState): void {
  group.position.set(...state.position);
  const target = new THREE.Vector3(...state.target);
  if (target.distanceTo(group.position) < 1e-4) {
    group.lookAt(group.position.x, group.position.y, group.position.z - 1);
    return;
  }
  group.lookAt(target);
}

function addMotionCurve(
  group: THREE.Group,
  curve: ShotMotionCurve,
  selected: boolean,
): void {
  const points = curve.controlPoints.map((point) => new THREE.Vector3(...point));
  if (points.length >= 2) {
    const path = new THREE.CatmullRomCurve3(points, curve.closed === true, 'catmullrom', 0.5);
    const line = new THREE.Line(
      new THREE.BufferGeometry().setFromPoints(path.getPoints(32)),
      new THREE.LineBasicMaterial({
        color: selected ? SELECTED_COLOR : CURVE_COLOR,
        transparent: true,
        opacity: 0.95,
        depthTest: false,
      }),
    );
    line.renderOrder = 8;
    line.userData = { kind: 'motion-curve', curveId: curve.id };
    group.add(line);
  }
  points.forEach((point, pointIndex) => {
    const marker = new THREE.Mesh(
      new THREE.SphereGeometry(selected ? 0.16 : 0.12, 14, 10),
      new THREE.MeshBasicMaterial({
        color: selected ? SELECTED_COLOR : CURVE_COLOR,
        depthTest: false,
      }),
    );
    marker.renderOrder = 9;
    marker.position.copy(point);
    marker.userData = { kind: 'curve-point', curveId: curve.id, pointIndex };
    group.add(marker);
  });
}

export function syncCameraTrajectoryOverlay(
  group: THREE.Group,
  shot: ShotDocument | undefined,
  selectedKeyframeId: string | null,
  timeSeconds = 0,
  selectedNodeId?: string | null,
): void {
  group.traverse((object) => {
    if (object instanceof THREE.Mesh || object instanceof THREE.Line) {
      object.geometry.dispose();
      const materials = Array.isArray(object.material) ? object.material : [object.material];
      materials.forEach((material) => material.dispose());
    }
  });
  group.clear();
  if (!shot) {
    group.visible = false;
    return;
  }
  group.visible = true;

  const points = cameraTrajectoryPoints(shot, 10);
  if (points.length >= 2) {
    const curve = new THREE.BufferGeometry().setFromPoints(
      points.map((point) => new THREE.Vector3(...point.position)),
    );
    const path = new THREE.Line(curve, new THREE.LineBasicMaterial({
      color: PATH_COLOR,
      transparent: true,
      opacity: 0.92,
    }));
    path.userData.kind = 'camera-path';
    group.add(path);
  }

  for (const point of points) {
    if (!point.keyframeId) continue;
    const selected = point.keyframeId === selectedKeyframeId;
    const marker = new THREE.Mesh(
      new THREE.SphereGeometry(selected ? 0.085 : 0.06, 16, 12),
      new THREE.MeshBasicMaterial({ color: selected ? SELECTED_COLOR : KEY_COLOR }),
    );
    marker.position.set(...point.position);
    marker.userData = { kind: 'camera-key', keyframeId: point.keyframeId };
    group.add(marker);

    const target = new THREE.Mesh(
      new THREE.SphereGeometry(0.035, 12, 10),
      new THREE.MeshBasicMaterial({ color: TARGET_COLOR, transparent: true, opacity: 0.8 }),
    );
    target.position.set(...point.target);
    target.userData = { kind: 'camera-target', keyframeId: point.keyframeId };
    group.add(target);

    const look = new THREE.Line(
      new THREE.BufferGeometry().setFromPoints([
        new THREE.Vector3(...point.position),
        new THREE.Vector3(...point.target),
      ]),
      new THREE.LineBasicMaterial({ color: TARGET_COLOR, transparent: true, opacity: 0.35 }),
    );
    look.userData.kind = 'camera-look';
    group.add(look);
  }

  for (const curve of shot.motionCurves) {
    addMotionCurve(group, curve, curve.id === selectedNodeId);
  }

  for (const camera of shot.cameras) {
    const state = sampleShotCameraWorld(shot, camera.id, timeSeconds);
    const icon = createCameraIcon(camera.id === shot.activeCameraId);
    orientCameraIcon(icon, state);
    icon.userData = { kind: 'camera-icon', cameraId: camera.id };
    icon.traverse((child) => {
      child.userData = { ...child.userData, kind: 'camera-icon', cameraId: camera.id };
    });
    group.add(icon);
  }
}

function overlayHitFromObject(object: THREE.Object3D): CameraOverlayHit | null {
  let current: THREE.Object3D | null = object;
  while (current) {
    const kind = current.userData.kind;
    if ((kind === 'camera-key' || kind === 'camera-target') && typeof current.userData.keyframeId === 'string') {
      return { kind, keyframeId: current.userData.keyframeId };
    }
    if (kind === 'camera-icon' && typeof current.userData.cameraId === 'string') {
      return { kind: 'camera-icon', cameraId: current.userData.cameraId };
    }
    if (kind === 'curve-point' && typeof current.userData.curveId === 'string' && Number.isInteger(current.userData.pointIndex)) {
      return {
        kind: 'curve-point',
        curveId: current.userData.curveId,
        pointIndex: current.userData.pointIndex as number,
      };
    }
    current = current.parent;
  }
  return null;
}

export function pickCameraKeyframe(
  group: THREE.Group,
  raycaster: THREE.Raycaster,
): CameraOverlayHit | null {
  const hits = raycaster.intersectObjects(group.children, true);
  for (const hit of hits) {
    const overlayHit = overlayHitFromObject(hit.object);
    if (overlayHit) return overlayHit;
  }
  return null;
}
