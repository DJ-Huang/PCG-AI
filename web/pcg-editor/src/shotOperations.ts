import { addShotMotionCurve, applyCameraGraphNodeChanges, connectShotCameras, disconnectShotCameras, upsertShotCamera } from './cameraGraph';
import { replaceCameraKeyframes, upsertCameraKeyframe } from './cameraTrack';
import { setCameraTransform } from './cameraTransform';
import { getCameraPreset } from './cameraPresets';
import { defaultPhysicalCamera, type CameraCommand } from './physicalCamera';
import { layoutShotGraph, normalizeShotDocument, selectShotNode, syncShotCameras, SHOT_OUTPUT_ID, type ShotCameraKeyframe, type ShotDocument, type ShotObjectKeyframe, type ShotTransform } from './shot';

const record = (value: unknown): value is Record<string, unknown> => !!value && typeof value === 'object' && !Array.isArray(value);
const vec3 = (value: unknown) => Array.isArray(value) && value.length === 3 && value.every(Number.isFinite);
const id = (value: unknown): string => { if (typeof value !== 'string' || !value.trim() || value.length > 120) throw new Error('A non-empty node id is required'); return value; };
const numbers = new Set(Object.entries(defaultPhysicalCamera()).filter(([, value]) => typeof value === 'number').map(([key]) => key));

export function validateCameraCommand(value: unknown): asserts value is CameraCommand {
  if (!record(value)) throw new Error('camera value must be an object');
  for (const [field, entry] of Object.entries(value)) {
    if (['position', 'target', 'up'].includes(field) && vec3(entry)) continue;
    if ((numbers.has(field) || ['pathProgress', 'azimuth', 'elevation', 'distance', 'fov'].includes(field)) && typeof entry === 'number' && Number.isFinite(entry)) continue;
    if (['focusOnTarget', 'dofEnabled'].includes(field) && typeof entry === 'boolean') continue;
    if (field === 'projection' && ['perspective', 'orthographic'].includes(String(entry))) continue;
    if (field === 'sensorFit' && ['auto', 'horizontal', 'vertical'].includes(String(entry))) continue;
    throw new Error(`Invalid camera channel: ${field}`);
  }
}

function validateKeys(value: unknown): asserts value is ShotCameraKeyframe[] {
  if (!Array.isArray(value) || value.length > 500) throw new Error('keyframes must contain 0–500 keys');
  for (const key of value) {
    if (!record(key) || typeof key.timeSeconds !== 'number' || !Number.isFinite(key.timeSeconds) || key.timeSeconds < 0) throw new Error('Key time must be finite and non-negative');
    if (key.interpolation && !['linear', 'ease-in', 'ease-out', 'ease-in-out', 'step'].includes(String(key.interpolation))) throw new Error('Unknown interpolation');
    validateCameraCommand(key.value);
  }
}

function transformPatch(value: unknown, label = 'transform'): Partial<ShotTransform> {
  if (!record(value)) throw new Error(`${label} must be an object`);
  const result: Partial<ShotTransform> = {};
  for (const field of ['position', 'rotationEulerDeg', 'scale'] as const) {
    if (value[field] === undefined) continue;
    if (!vec3(value[field])) throw new Error(`${label}.${field} requires three finite numbers`);
    result[field] = value[field] as [number, number, number];
  }
  return result;
}

function validateObjectKeys(value: unknown, durationSeconds: number): asserts value is ShotObjectKeyframe[] {
  if (!Array.isArray(value) || value.length > 500) throw new Error('keyframes must contain 0–500 keys');
  for (const key of value) {
    if (!record(key) || typeof key.timeSeconds !== 'number' || !Number.isFinite(key.timeSeconds) || key.timeSeconds < 0 || key.timeSeconds > durationSeconds) throw new Error('Object key time must be within the shot');
    if (key.interpolation && !['linear', 'ease-in', 'ease-out', 'ease-in-out', 'step'].includes(String(key.interpolation))) throw new Error('Unknown interpolation');
    transformPatch(key.transform, 'keyframe.transform');
  }
}

/** Apply to a copy and publish once: a failed operation never leaves half a rig. */
export function applyShotOperations(shot: ShotDocument, operations: unknown): ShotDocument {
  if (!Array.isArray(operations) || operations.length === 0 || operations.length > 100) throw new Error('operations must contain 1–100 operations');
  let next = structuredClone(syncShotCameras(shot));
  const exists = (nodeId: string) => nodeId === SHOT_OUTPUT_ID || [...next.cameras, ...next.motionCurves, ...next.cameraTransforms].some((entry) => entry.id === nodeId);
  const requireCamera = (cameraId: unknown) => { const value = id(cameraId); if (!next.cameras.some((camera) => camera.id === value)) throw new Error(`Unknown camera: ${value}`); return value; };
  for (const operation of operations) {
    if (!record(operation)) throw new Error('Operation must be an object');
    const op = operation;
    switch (op.op) {
      case 'upsert_component': {
        const componentId = id(op.componentId);
        const current = next.components.find((component) => component.componentId === componentId);
        const base = current ?? { componentId, transform: { position: [0, 0, 0], rotationEulerDeg: [0, 0, 0], scale: [1, 1, 1] } };
        const patch = op.transform === undefined ? {} : transformPatch(op.transform);
        const component = {
          ...base,
          ...(typeof op.assetId === 'string' ? { assetId: op.assetId } : {}),
          ...(typeof op.role === 'string' ? { role: op.role } : {}),
          ...(record(op.bounds) ? { bounds: op.bounds as never } : {}),
          ...(record(op.anchors) ? { anchors: op.anchors as never } : {}),
          transform: { ...base.transform, ...patch },
        };
        next.components = current
          ? next.components.map((entry) => entry.componentId === componentId ? component : entry)
          : [...next.components, component];
        break;
      }
      case 'set_object_keyframes': {
        const componentId = id(op.componentId);
        if (!next.components.some((component) => component.componentId === componentId)) throw new Error(`Unknown component: ${componentId}`);
        validateObjectKeys(op.keyframes, next.durationSeconds);
        const existing = next.objectAnimations.find((entry) => entry.componentId === componentId);
        const track = existing ?? { componentId, clip: '__transform__', startSeconds: 0, playbackRate: 1, loop: false };
        const keys = (op.keyframes as ShotObjectKeyframe[]).map((key, index) => ({ ...key, id: typeof key.id === 'string' && key.id ? key.id : `${componentId}_key_${index + 1}`, interpolation: key.interpolation ?? 'linear' }));
        const keyframes = op.mode === 'upsert'
          ? [...(track.keyframes ?? []).filter((old) => !keys.some((key) => key.id === old.id)), ...keys].sort((a, b) => a.timeSeconds - b.timeSeconds)
          : keys;
        const updated = { ...track, keyframes };
        next.objectAnimations = existing ? next.objectAnimations.map((entry) => entry === existing ? updated : entry) : [...next.objectAnimations, updated];
        break;
      }
      case 'set_action_clip': {
        const componentId = id(op.componentId);
        if (!next.components.some((component) => component.componentId === componentId)) throw new Error(`Unknown component: ${componentId}`);
        const clip = id(op.clip);
        const current = next.objectAnimations.find((entry) => entry.componentId === componentId);
        const updated = { componentId, clip,
          startSeconds: typeof op.startSeconds === 'number' && Number.isFinite(op.startSeconds) ? op.startSeconds : current?.startSeconds ?? 0,
          playbackRate: typeof op.playbackRate === 'number' && Number.isFinite(op.playbackRate) && op.playbackRate > 0 ? op.playbackRate : current?.playbackRate ?? 1,
          loop: typeof op.loop === 'boolean' ? op.loop : current?.loop ?? false,
          ...(current?.keyframes ? { keyframes: current.keyframes } : {}),
        };
        next.objectAnimations = current ? next.objectAnimations.map((entry) => entry === current ? updated : entry) : [...next.objectAnimations, updated];
        break;
      }
      case 'set_visibility_range': {
        const componentId = id(op.componentId);
        const current = next.components.find((component) => component.componentId === componentId);
        if (!current) throw new Error(`Unknown component: ${componentId}`);
        const from = op.fromSeconds === undefined ? 0 : op.fromSeconds;
        const until = op.untilSeconds === undefined ? next.durationSeconds : op.untilSeconds;
        if (typeof from !== 'number' || typeof until !== 'number' || !Number.isFinite(from) || !Number.isFinite(until) || from < 0 || until < from || until > next.durationSeconds) throw new Error('Visibility range must be inside the shot');
        next.components = next.components.map((entry) => entry.componentId === componentId ? { ...entry, visibleFromSeconds: from, visibleUntilSeconds: until } : entry);
        break;
      }
      case 'upsert_camera': {
        const cameraId = id(op.id);
        if (exists(cameraId) && !next.cameras.some((camera) => camera.id === cameraId)) throw new Error('Node id already belongs to another node');
        if (op.camera) validateCameraCommand(op.camera);
        if (op.presetId && !getCameraPreset(String(op.presetId))) throw new Error('Unknown camera preset');
        next = upsertShotCamera(next, { id: cameraId, name: typeof op.name === 'string' ? op.name : undefined,
          presetId: typeof op.presetId === 'string' ? op.presetId : undefined, camera: op.camera as CameraCommand | undefined });
        break;
      }
      case 'set_transform': {
        const cameraId = requireCamera(op.cameraId);
        const patch: { translation?: [number, number, number]; rotationEulerDeg?: [number, number, number] } = {};
        for (const field of ['translation', 'rotationEulerDeg'] as const) if (op[field] !== undefined) {
          if (!vec3(op[field])) throw new Error(`${field} requires three finite numbers`);
          patch[field] = op[field] as [number, number, number];
        }
        next = setCameraTransform(next, cameraId, patch);
        break;
      }
      case 'upsert_motion_curve': {
        const curveId = id(op.id);
        if (exists(curveId) && !next.motionCurves.some((curve) => curve.id === curveId)) throw new Error('Node id already belongs to another node');
        if (op.controlPoints !== undefined && (!Array.isArray(op.controlPoints) || op.controlPoints.length < 2 || op.controlPoints.length > 500 || !op.controlPoints.every(vec3))) throw new Error('controlPoints requires 2–500 finite XYZ points');
        if (op.lookMode !== undefined && !['tangent', 'target'].includes(String(op.lookMode))) throw new Error('lookMode must be tangent or target');
        if (op.closed !== undefined && typeof op.closed !== 'boolean') throw new Error('closed must be boolean');
        if (!exists(curveId)) {
          next = addShotMotionCurve(next);
          const generated = next.selectedNodeId;
          next = { ...next, selectedNodeId: curveId, motionCurves: next.motionCurves.map((curve) => curve.id === generated ? { ...curve, id: curveId } : curve) };
        }
        next = { ...next, motionCurves: next.motionCurves.map((curve) => curve.id !== curveId ? curve : { ...curve,
          ...(typeof op.name === 'string' ? { name: op.name } : {}),
          ...(op.controlPoints ? { controlPoints: op.controlPoints as [number, number, number][] } : {}),
          ...(typeof op.closed === 'boolean' ? { closed: op.closed } : {}),
          ...(op.lookMode ? { lookMode: op.lookMode as 'tangent' | 'target' } : {}),
        }) };
        if (op.cameraId) next = connectShotCameras(next, curveId, requireCamera(op.cameraId));
        break;
      }
      case 'set_keyframes': {
        const nodeId = id(op.nodeId);
        if (![...next.cameras, ...next.motionCurves].some((node) => node.id === nodeId)) throw new Error(`Unknown animation node: ${nodeId}`);
        validateKeys(op.keyframes);
        if (op.keyframes.some((key) => key.timeSeconds > next.durationSeconds)) throw new Error('Key exceeds shot duration; set duration first');
        if (op.mode !== undefined && !['replace', 'upsert'].includes(String(op.mode))) throw new Error('mode must be replace or upsert');
        next = selectShotNode(next, nodeId);
        next = op.mode === 'upsert' ? op.keyframes.reduce((doc, key) => upsertCameraKeyframe(doc, key), next) : replaceCameraKeyframes(next, op.keyframes);
        break;
      }
      case 'connect': {
        const source = id(op.source), target = id(op.target);
        if (!exists(source) || !exists(target)) throw new Error('Unknown connection node');
        next = connectShotCameras(next, source, target);
        if (!next.cameraEdges.some((edge) => edge.source === source && edge.target === target)) throw new Error('Incompatible camera graph connection');
        break;
      }
      case 'disconnect': next = disconnectShotCameras(next, id(op.edgeId)); break;
      case 'remove_node': {
        const nodeId = id(op.nodeId);
        if (!exists(nodeId) || nodeId === SHOT_OUTPUT_ID || (next.cameras.length === 1 && next.cameras[0].id === nodeId)) throw new Error('Cannot remove this node');
        next = applyCameraGraphNodeChanges(next, [{ type: 'remove', id: nodeId }]);
        break;
      }
      case 'select': {
        const nodeId = id(op.nodeId);
        if (!exists(nodeId)) throw new Error('Unknown selection');
        next = selectShotNode(next, nodeId);
        break;
      }
      case 'set_shot': {
        const ranges = { durationSeconds: [0.1, 3600], fps: [1, 120], width: [16, 8192], height: [16, 8192] };
        for (const [field, [min, max]] of Object.entries(ranges)) if (op[field] !== undefined) {
          const value = op[field];
          if (typeof value !== 'number' || !Number.isFinite(value) || value < min || value > max) throw new Error(`Invalid ${field}`);
          Object.assign(next, { [field]: value });
        }
        if (typeof op.name === 'string' && op.name.trim()) next.name = op.name.trim();
        const allKeys = [...next.cameras.flatMap((camera) => camera.cameraKeyframes), ...next.motionCurves.flatMap((curve) => curve.cameraKeyframes ?? [])];
        if (allKeys.some((key) => key.timeSeconds > next.durationSeconds)) throw new Error('Duration would truncate existing keys');
        break;
      }
      default: throw new Error(`Unknown shot operation: ${String(op.op)}`);
    }
    next = syncShotCameras(next);
  }
  return layoutShotGraph(normalizeShotDocument(next));
}
