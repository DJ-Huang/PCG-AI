import * as THREE from 'three';
import type { PhysicalCameraState } from './physicalCamera';
import { layoutShotGraph, SHOT_OUTPUT_ID, type ShotCameraTransform, type ShotDocument } from './shot';

export function findCameraTransform(shot: ShotDocument, cameraId: string): ShotCameraTransform | undefined {
  return shot.cameraTransforms?.find((rig) => rig.cameraId === cameraId
    && shot.cameraEdges.some((edge) => edge.source === cameraId && edge.target === rig.id));
}

export function cameraQuaternion(camera: PhysicalCameraState): THREE.Quaternion {
  return new THREE.Quaternion().setFromRotationMatrix(new THREE.Matrix4().lookAt(
    new THREE.Vector3(...camera.position), new THREE.Vector3(...camera.target), new THREE.Vector3(...camera.up),
  ));
}

export function applyCameraTransform(state: PhysicalCameraState, rig?: ShotCameraTransform): PhysicalCameraState {
  if (!rig) return state;
  const rotation = new THREE.Quaternion().setFromEuler(new THREE.Euler(
    ...rig.rotationEulerDeg.map(THREE.MathUtils.degToRad) as [number, number, number], 'YXZ',
  ));
  const position = new THREE.Vector3(...state.position).add(new THREE.Vector3(...rig.translation));
  const direction = new THREE.Vector3(...state.target).sub(new THREE.Vector3(...state.position)).applyQuaternion(rotation);
  return { ...state, position: position.toArray(), target: position.clone().add(direction).toArray(),
    up: new THREE.Vector3(...state.up).applyQuaternion(rotation).toArray() };
}

/** One rig per camera; first edit inserts Camera → Transform → Output atomically. */
export function setCameraTransform(
  shot: ShotDocument, cameraId: string,
  patch: Partial<Pick<ShotCameraTransform, 'translation' | 'rotationEulerDeg'>>,
): ShotDocument {
  const camera = shot.cameras.find((entry) => entry.id === cameraId);
  if (!camera) return shot;
  for (const value of Object.values(patch)) {
    if (!Array.isArray(value) || value.length !== 3 || !value.every(Number.isFinite)) return shot;
  }
  const existing = shot.cameraTransforms.find((rig) => rig.cameraId === cameraId);
  if (existing) {
    const connected = !!findCameraTransform(shot, cameraId);
    const next = { ...shot, cameraTransforms: shot.cameraTransforms.map((rig) => rig.id === existing.id ? { ...rig, ...patch } : rig) };
    if (connected) return next;
    // A disconnected rig remains editable. Reconnect it instead of creating a
    // second owner rig that normalization would silently discard.
    const edges = shot.cameraEdges.filter((edge) => !(edge.source === cameraId && edge.target === SHOT_OUTPUT_ID));
    const edgeIds = new Set(edges.map((edge) => edge.id));
    const edgeId = (base: string) => {
      let id = base;
      for (let i = 2; edgeIds.has(id); i++) id = `${base}_${i}`;
      edgeIds.add(id);
      return id;
    };
    edges.push({ id: edgeId(`e_${cameraId}_${existing.id}`), source: cameraId, target: existing.id });
    if (!edges.some((edge) => edge.source === existing.id && edge.target === SHOT_OUTPUT_ID)) {
      edges.push({ id: edgeId(`e_${existing.id}_out`), source: existing.id, target: SHOT_OUTPUT_ID });
    }
    return layoutShotGraph({ ...next, cameraEdges: edges });
  }
  const ids = new Set([SHOT_OUTPUT_ID, ...shot.cameras.map((c) => c.id), ...shot.motionCurves.map((c) => c.id),
    ...shot.cameraTransforms.map((c) => c.id), ...shot.cameraEdges.map((edge) => edge.id)]);
  const unique = (base: string) => {
    let id = base;
    for (let i = 2; ids.has(id); i++) id = `${base}_${i}`;
    ids.add(id);
    return id;
  };
  const id = unique(`${cameraId}_transform`);
  const rig: ShotCameraTransform = { id, cameraId, name: 'Transform',
    position: { x: camera.position.x, y: camera.position.y + 160 },
    translation: [0, 0, 0], rotationEulerDeg: [0, 0, 0], ...patch };
  const edges = shot.cameraEdges.filter((edge) => !(edge.source === cameraId && edge.target === SHOT_OUTPUT_ID));
  return layoutShotGraph({ ...shot, cameraTransforms: [...shot.cameraTransforms, rig],
    shotOutputPosition: { x: shot.shotOutputPosition.x, y: Math.max(shot.shotOutputPosition.y, rig.position.y + 160) },
    cameraEdges: [...edges,
      { id: unique(`e_${cameraId}_${id}`), source: cameraId, target: id },
      { id: unique(`e_${id}_out`), source: id, target: SHOT_OUTPUT_ID }],
  });
}

export function moveCameraRig(shot: ShotDocument, cameraId: string, from: PhysicalCameraState, to: PhysicalCameraState): ShotDocument {
  const rig = findCameraTransform(shot, cameraId);
  const translation = new THREE.Vector3(...(rig?.translation ?? [0, 0, 0]))
    .add(new THREE.Vector3(...to.position).sub(new THREE.Vector3(...from.position)));
  const oldRotation = new THREE.Quaternion().setFromEuler(new THREE.Euler(
    ...(rig?.rotationEulerDeg ?? [0, 0, 0]).map(THREE.MathUtils.degToRad) as [number, number, number], 'YXZ',
  ));
  const delta = cameraQuaternion(to).multiply(cameraQuaternion(from).invert()).multiply(oldRotation);
  const euler = new THREE.Euler().setFromQuaternion(delta, 'YXZ');
  return setCameraTransform(shot, cameraId, { translation: translation.toArray(),
    rotationEulerDeg: [euler.x, euler.y, euler.z].map(THREE.MathUtils.radToDeg) as [number, number, number] });
}
