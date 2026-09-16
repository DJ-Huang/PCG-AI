import type { Connection, Edge, EdgeChange, Node, NodeChange } from '@xyflow/react';
import { applyEdgeChanges, applyNodeChanges } from '@xyflow/react';

import {
  applyCameraPreset,
  DEFAULT_CAMERA_PRESET_ID,
  getCameraPreset,
  inferCameraPresetId,
} from './cameraPresets';
import { createDefaultMotionCurvePoints } from './cameraPath';
import { mergeCameraCommand, type CameraCommand } from './physicalCamera';
import {
  createDefaultCamera,
  findShotCamera,
  findShotMotionCurve,
  SHOT_OUTPUT_ID,
  syncShotCameras,
  type ShotCamera,
  type ShotCameraEdge,
  type ShotDocument,
  type ShotMotionCurve,
} from './shot';

export const CAMERA_PIN_COLOR = '#6ec6ff';

export function nextCameraId(cameras: readonly ShotCamera[]): string {
  const used = new Set(cameras.map((camera) => camera.id));
  let index = cameras.length + 1;
  let candidate = `cam_${String.fromCharCode(96 + index)}`;
  while (used.has(candidate) || candidate === SHOT_OUTPUT_ID) {
    index += 1;
    candidate = `cam_${index}`;
  }
  return candidate;
}

export function nextMotionCurveId(curves: readonly ShotMotionCurve[]): string {
  const used = new Set(curves.map((curve) => curve.id));
  let index = curves.length + 1;
  let candidate = `path_${String.fromCharCode(96 + index)}`;
  while (used.has(candidate) || candidate === SHOT_OUTPUT_ID) {
    index += 1;
    candidate = `path_${index}`;
  }
  return candidate;
}

export function uniqueCameraName(named: readonly { name: string }[], base: string): string {
  const used = new Set(named.map((entry) => entry.name));
  if (!used.has(base)) return base;
  let index = 2;
  while (used.has(`${base} ${index}`)) index += 1;
  return `${base} ${index}`;
}

export function addShotMotionCurve(
  shot: ShotDocument,
  position?: { x: number; y: number },
): ShotDocument {
  const synced = syncShotCameras(shot);
  const id = nextMotionCurveId(synced.motionCurves);
  const driver = findShotCamera(synced, synced.activeCameraId)?.camera ?? synced.camera;
  const curve: ShotMotionCurve = {
    id,
    name: uniqueCameraName(synced.motionCurves, 'Motion Curve'),
    position: {
      x: position?.x ?? 80 + synced.motionCurves.length * 320,
      y: position?.y ?? 0,
    },
    controlPoints: createDefaultMotionCurvePoints(driver),
    closed: false,
    lookMode: 'tangent',
    cameraKeyframes: [],
  };
  return {
    ...synced,
    motionCurves: [...synced.motionCurves, curve],
    selectedNodeId: id,
  };
}

export interface AddShotCameraOptions {
  presetId?: string;
  name?: string;
}

export function addShotCamera(
  shot: ShotDocument,
  position?: { x: number; y: number },
  options?: AddShotCameraOptions | string,
): ShotDocument {
  const synced = syncShotCameras(shot);
  const id = nextCameraId(synced.cameras);
  const presetId = typeof options === 'string'
    ? options
    : (options?.presetId ?? DEFAULT_CAMERA_PRESET_ID);
  const preset = getCameraPreset(presetId) ?? getCameraPreset(DEFAULT_CAMERA_PRESET_ID)!;
  const requestedName = typeof options === 'string' ? undefined : options?.name;
  const camera = createDefaultCamera(
    id,
    uniqueCameraName(synced.cameras, requestedName?.trim() || preset.name),
    position?.x ?? 80 + synced.cameras.length * 320,
    position?.y ?? 160,
    preset.id,
  );
  return {
    ...synced,
    cameras: [...synced.cameras, camera],
    cameraEdges: [
      ...synced.cameraEdges,
      { id: `e_${id}_out`, source: id, target: SHOT_OUTPUT_ID },
    ],
    activeCameraId: id,
    selectedNodeId: id,
    camera: camera.camera,
    cameraKeyframes: camera.cameraKeyframes,
  };
}

export function removeShotCamera(shot: ShotDocument, cameraId: string): ShotDocument {
  if (shot.cameras.length <= 1 || cameraId === SHOT_OUTPUT_ID) return shot;
  const cameras = shot.cameras.filter((camera) => camera.id !== cameraId);
  const nextActive = shot.activeCameraId === cameraId ? cameras[0] : findShotCamera(shot, shot.activeCameraId);
  if (!nextActive) return shot;
  const removed = new Set([cameraId, ...shot.cameraTransforms.filter((rig) => rig.cameraId === cameraId).map((rig) => rig.id)]);
  return {
    ...shot,
    cameras,
    cameraTransforms: shot.cameraTransforms.filter((rig) => rig.cameraId !== cameraId),
    cameraEdges: shot.cameraEdges.filter((edge) => !removed.has(edge.source) && !removed.has(edge.target)),
    activeCameraId: nextActive.id,
    selectedNodeId: nextActive.id,
    camera: nextActive.camera,
    cameraKeyframes: nextActive.cameraKeyframes,
  };
}

export function renameShotCamera(shot: ShotDocument, cameraId: string, name: string): ShotDocument {
  const trimmed = name.trim();
  if (!trimmed) return shot;
  return {
    ...shot,
    cameras: shot.cameras.map((camera) => (camera.id === cameraId ? { ...camera, name: trimmed } : camera)),
  };
}

export function upsertShotCamera(
  shot: ShotDocument,
  patch: Partial<Omit<ShotCamera, 'camera'>> & { id?: string; camera?: CameraCommand },
): ShotDocument {
  const synced = syncShotCameras(shot);
  const id = typeof patch.id === 'string' && patch.id.trim() && patch.id !== SHOT_OUTPUT_ID
    ? patch.id.trim()
    : nextCameraId(synced.cameras);
  const existing = synced.cameras.find((camera) => camera.id === id);
  if (!existing) {
    const created = addShotCamera(synced, patch.position, {
      presetId: patch.presetId,
      name: patch.name,
    });
    const added = created.cameras.find((camera) => camera.id === created.activeCameraId);
    if (!added) return created;
    const cameras = created.cameras.map((camera) => (
      camera.id === added.id ? mergeShotCameraPatch(camera, patch) : camera
    ));
    const patched = cameras.find((camera) => camera.id === added.id) ?? added;
    return {
      ...created,
      cameras: cameras.map((camera) => camera.id === added.id ? { ...camera, id } : camera),
      cameraEdges: created.cameraEdges.map((edge) => ({ ...edge,
        id: edge.id.replace(added.id, id), source: edge.source === added.id ? id : edge.source,
        target: edge.target === added.id ? id : edge.target })),
      selectedNodeId: id,
      activeCameraId: id,
      camera: patched.camera,
      cameraKeyframes: patched.cameraKeyframes,
    };
  }
  const cameras = synced.cameras.map((camera) => (
    camera.id === id ? mergeShotCameraPatch(camera, patch) : camera
  ));
  const patched = cameras.find((camera) => camera.id === id);
  if (!patched) return synced;
  return {
    ...synced,
    cameras,
    activeCameraId: patched.id,
    camera: patched.camera,
    cameraKeyframes: patched.cameraKeyframes,
  };
}

function mergeShotCameraPatch(
  camera: ShotCamera,
  patch: Partial<Omit<ShotCamera, 'camera'>> & { camera?: CameraCommand },
): ShotCamera {
  let nextCamera = camera.camera;
  let presetId = camera.presetId;
  if (typeof patch.presetId === 'string' && getCameraPreset(patch.presetId)) {
    presetId = patch.presetId;
    nextCamera = applyCameraPreset(nextCamera, presetId);
  }
  if (patch.camera) nextCamera = mergeCameraCommand(nextCamera, patch.camera);
  if (typeof patch.presetId !== 'string') {
    presetId = inferCameraPresetId(nextCamera);
  }
  return {
    ...camera,
    name: typeof patch.name === 'string' && patch.name.trim() ? patch.name.trim() : camera.name,
    presetId,
    position: patch.position ?? camera.position,
    camera: nextCamera,
    cameraKeyframes: patch.cameraKeyframes ?? camera.cameraKeyframes,
  };
}

export function applyShotCameraPreset(
  shot: ShotDocument,
  cameraId: string,
  presetId: string,
): ShotDocument {
  const preset = getCameraPreset(presetId);
  if (!preset) return shot;
  const synced = syncShotCameras(shot);
  const cameras = synced.cameras.map((camera) => {
    if (camera.id !== cameraId) return camera;
    return {
      ...camera,
      presetId: preset.id,
      camera: applyCameraPreset(camera.camera, preset.id),
      cameraKeyframes: camera.cameraKeyframes.map((keyframe) => ({
        ...keyframe,
        value: {
          ...keyframe.value,
          sensorWidthMm: preset.sensorWidthMm,
          sensorHeightMm: preset.sensorHeightMm,
          sensorFit: 'auto' as const,
        },
      })),
    };
  });
  const patched = cameras.find((camera) => camera.id === cameraId);
  if (!patched) return shot;
  return {
    ...synced,
    cameras,
    activeCameraId: patched.id,
    camera: patched.camera,
    cameraKeyframes: patched.cameraKeyframes,
  };
}

export function removeShotMotionCurve(shot: ShotDocument, curveId: string): ShotDocument {
  if (!findShotMotionCurve(shot, curveId)) return shot;
  const selectedNodeId = shot.selectedNodeId === curveId ? shot.activeCameraId : shot.selectedNodeId;
  return {
    ...shot,
    motionCurves: shot.motionCurves.filter((curve) => curve.id !== curveId),
    cameraEdges: shot.cameraEdges.filter((edge) => edge.source !== curveId && edge.target !== curveId),
    selectedNodeId,
  };
}

export function renameShotMotionCurve(shot: ShotDocument, curveId: string, name: string): ShotDocument {
  const trimmed = name.trim();
  if (!trimmed) return shot;
  return {
    ...shot,
    motionCurves: shot.motionCurves.map((curve) => (curve.id === curveId ? { ...curve, name: trimmed } : curve)),
  };
}

export function setMotionCurvePoints(
  shot: ShotDocument,
  curveId: string,
  controlPoints: [number, number, number][],
): ShotDocument {
  if (controlPoints.length < 2 || controlPoints.some((point) => point.length !== 3 || !point.every(Number.isFinite))) return shot;
  return {
    ...shot,
    motionCurves: shot.motionCurves.map((curve) => (
      curve.id === curveId ? { ...curve, controlPoints: controlPoints.map((point) => [...point] as [number, number, number]) } : curve
    )),
  };
}

export function setMotionCurvePoint(
  shot: ShotDocument,
  curveId: string,
  pointIndex: number,
  point: [number, number, number],
): ShotDocument {
  const curve = findShotMotionCurve(shot, curveId);
  if (!curve || pointIndex < 0 || pointIndex >= curve.controlPoints.length) return shot;
  const controlPoints = curve.controlPoints.map((entry, index) => (
    index === pointIndex ? point : entry
  ));
  return setMotionCurvePoints(shot, curveId, controlPoints);
}

export function addMotionCurvePoint(shot: ShotDocument, curveId: string): ShotDocument {
  const curve = findShotMotionCurve(shot, curveId);
  if (!curve) return shot;
  const points = curve.controlPoints;
  const last = points[points.length - 1] ?? [0, 1.5, 0];
  const prev = points[points.length - 2] ?? last;
  let dx = last[0] - prev[0];
  let dy = last[1] - prev[1];
  let dz = last[2] - prev[2];
  if (dx * dx + dy * dy + dz * dz < 1e-6) {
    dx = 0.8;
    dy = 0;
    dz = 0;
  }
  return setMotionCurvePoints(shot, curveId, [...points, [last[0] + dx, last[1] + dy, last[2] + dz]]);
}

export function removeMotionCurvePoint(
  shot: ShotDocument,
  curveId: string,
  pointIndex: number,
): ShotDocument {
  const curve = findShotMotionCurve(shot, curveId);
  if (!curve || curve.controlPoints.length <= 2) return shot;
  if (pointIndex < 0 || pointIndex >= curve.controlPoints.length) return shot;
  return setMotionCurvePoints(
    shot,
    curveId,
    curve.controlPoints.filter((_, index) => index !== pointIndex),
  );
}

export function setMotionCurveClosed(
  shot: ShotDocument,
  curveId: string,
  closed: boolean,
): ShotDocument {
  return {
    ...shot,
    motionCurves: shot.motionCurves.map((curve) => (
      curve.id === curveId ? { ...curve, closed } : curve
    )),
  };
}

export function connectShotCameras(shot: ShotDocument, source: string, target: string): ShotDocument {
  if (!source || !target || source === target) return shot;
  const sourceIsCurve = shot.motionCurves.some((curve) => curve.id === source);
  const sourceIsCamera = shot.cameras.some((camera) => camera.id === source);
  const targetIsCamera = shot.cameras.some((camera) => camera.id === target);
  const targetIsOutput = target === SHOT_OUTPUT_ID;
  const sourceRig = shot.cameraTransforms.find((entry) => entry.id === source);
  const targetRig = shot.cameraTransforms.find((entry) => entry.id === target);
  if (sourceRig || targetRig) {
    if (!((sourceRig && targetIsOutput) || (targetRig?.cameraId === source))) return shot;
    if (shot.cameraEdges.some((edge) => edge.source === source && edge.target === target)) return shot;
    return { ...shot, cameraEdges: [...shot.cameraEdges, { id: `e_${source}_${target}`, source, target }] };
  }
  if (sourceIsCurve && !targetIsCamera) return shot;
  if (sourceIsCamera && !targetIsCamera && !targetIsOutput) return shot;
  if (!sourceIsCurve && !sourceIsCamera) return shot;
  if (shot.cameraEdges.some((edge) => edge.source === source && edge.target === target)) return shot;
  let cameraEdges = shot.cameraEdges;
  if (sourceIsCurve && targetIsCamera) {
    cameraEdges = cameraEdges.filter((edge) => !(
      edge.target === target && shot.motionCurves.some((curve) => curve.id === edge.source)
    ));
  }
  return {
    ...shot,
    cameraEdges: [...cameraEdges, { id: `e_${source}_${target}`, source, target }],
  };
}

export function disconnectShotCameras(shot: ShotDocument, edgeId: string): ShotDocument {
  return {
    ...shot,
    cameraEdges: shot.cameraEdges.filter((edge) => edge.id !== edgeId),
  };
}

export function preserveFlowNodeLayout(nodes: Node[], previous: readonly Node[]): Node[] {
  if (previous.length === 0) return nodes;
  const prev = new Map(previous.map((node) => [node.id, node]));
  return nodes.map((node) => {
    const old = prev.get(node.id);
    if (!old) return node;
    const measured = old.measured ?? (
      old.width != null && old.height != null
        ? { width: old.width, height: old.height }
        : undefined
    );
    if (!measured && old.width == null && old.height == null) return node;
    return {
      ...node,
      width: old.width,
      height: old.height,
      measured,
    };
  });
}

export function shotCamerasToFlow(shot: ShotDocument): { nodes: Node[]; edges: Edge[] } {
  const selectedId = shot.selectedNodeId || shot.activeCameraId;
  const nodes: Node[] = [
    ...shot.cameraTransforms.map((rig) => ({
      id: rig.id, type: 'CameraTransform', position: rig.position, selected: rig.id === selectedId,
      data: { kind: 'transform', name: rig.name, bodyName: 'Transform', active: rig.id === selectedId },
    })),
    ...shot.motionCurves.map((curve) => ({
      id: curve.id,
      type: 'MotionCurve',
      position: curve.position,
      selected: curve.id === selectedId,
      data: {
        kind: 'curve',
        name: curve.name,
        bodyName: 'Motion Curve',
        keyCount: curve.controlPoints.length,
        active: curve.id === selectedId,
      },
    })),
    ...shot.cameras.map((camera) => ({
      id: camera.id,
      type: 'Camera',
      position: camera.position,
      selected: camera.id === selectedId,
      data: {
        kind: 'camera',
        name: camera.name,
        bodyName: getCameraPreset(camera.presetId)?.name ?? 'Camera',
        presetId: camera.presetId,
        keyCount: camera.cameraKeyframes.length,
        active: camera.id === shot.activeCameraId,
      },
    })),
    {
      id: SHOT_OUTPUT_ID,
      type: 'ShotOutput',
      position: shot.shotOutputPosition,
      selected: selectedId === SHOT_OUTPUT_ID,
      data: { kind: 'output', name: 'Shot Output', keyCount: 0, active: false },
    },
  ];
  const edges: Edge[] = shot.cameraEdges.map((edge) => ({
    id: edge.id,
    source: edge.source,
    target: edge.target,
    sourceHandle: 'out',
    targetHandle: 'in',
  }));
  return { nodes, edges };
}

export function applyCameraGraphNodeChanges(shot: ShotDocument, changes: NodeChange[]): ShotDocument {
  const { nodes } = shotCamerasToFlow(shot);
  const nextNodes = applyNodeChanges(changes, nodes);
  const output = nextNodes.find((node) => node.id === SHOT_OUTPUT_ID);
  const remaining = new Set(nextNodes.map((node) => node.id));
  let next = shot;
  for (const camera of shot.cameras) {
    if (!remaining.has(camera.id)) next = removeShotCamera(next, camera.id);
  }
  for (const curve of shot.motionCurves) {
    if (!remaining.has(curve.id)) next = removeShotMotionCurve(next, curve.id);
  }
  for (const rig of shot.cameraTransforms) {
    if (!remaining.has(rig.id)) {
      const cameraEdges = next.cameraEdges.filter((edge) => edge.source !== rig.id && edge.target !== rig.id);
      if (!cameraEdges.some((edge) => edge.source === rig.cameraId && edge.target === SHOT_OUTPUT_ID)) {
        cameraEdges.push({ id: `e_${rig.cameraId}_out`, source: rig.cameraId, target: SHOT_OUTPUT_ID });
      }
      next = { ...next, cameraTransforms: next.cameraTransforms.filter((entry) => entry.id !== rig.id), cameraEdges };
    }
  }
  return {
    ...next,
    cameraTransforms: next.cameraTransforms.map((rig) => ({ ...rig, position: nextNodes.find((node) => node.id === rig.id)?.position ?? rig.position })),
    cameras: next.cameras.map((camera) => {
      const node = nextNodes.find((entry) => entry.id === camera.id);
      return node ? { ...camera, position: node.position } : camera;
    }),
    motionCurves: next.motionCurves.map((curve) => {
      const node = nextNodes.find((entry) => entry.id === curve.id);
      return node ? { ...curve, position: node.position } : curve;
    }),
    shotOutputPosition: output?.position ?? next.shotOutputPosition,
  };
}

export function applyCameraGraphEdgeChanges(shot: ShotDocument, changes: EdgeChange[]): ShotDocument {
  const { edges } = shotCamerasToFlow(shot);
  const nextEdges = applyEdgeChanges(changes, edges);
  return {
    ...shot,
    cameraEdges: nextEdges.flatMap((edge): ShotCameraEdge[] => (
      edge.source && edge.target
        ? [{ id: edge.id, source: edge.source, target: edge.target }]
        : []
    )),
  };
}

export function connectCameraGraph(shot: ShotDocument, connection: Connection): ShotDocument {
  if (!connection.source || !connection.target) return shot;
  return connectShotCameras(shot, connection.source, connection.target);
}
