import type { SemanticBounds } from './graphSchema';
import {
  applyCameraPreset,
  DEFAULT_CAMERA_PRESET_ID,
  getCameraPreset,
  resolveCameraPresetId,
} from './cameraPresets';
import {
  defaultPhysicalCamera,
  mergeCameraCommand,
  type CameraCommand,
  type PhysicalCameraState,
} from './physicalCamera';
import type { PreviewParameterValue } from './previewParameters';

export type ShotInterpolation = 'linear' | 'ease-in' | 'ease-out' | 'ease-in-out';

export interface ShotTransform {
  position: [number, number, number];
  rotationEulerDeg: [number, number, number];
  scale: [number, number, number];
}

export interface ShotComponent {
  componentId: string;
  assetId?: string;
  role?: string;
  bounds?: SemanticBounds;
  anchors?: Record<string, [number, number, number]>;
  transform: ShotTransform;
}

export interface ShotObjectAnimation {
  componentId: string;
  clip: string;
  startSeconds: number;
  playbackRate: number;
  loop: boolean;
}

export interface ShotCameraKeyframe {
  id: string;
  timeSeconds: number;
  interpolation: ShotInterpolation;
  value: CameraCommand;
}

export interface ShotCamera {
  id: string;
  name: string;
  presetId: string;
  position: { x: number; y: number };
  camera: PhysicalCameraState;
  cameraKeyframes: ShotCameraKeyframe[];
}

export interface ShotCameraEdge {
  id: string;
  source: string;
  target: string;
}

export interface ShotMotionCurve {
  id: string;
  name: string;
  position: { x: number; y: number };
  controlPoints: [number, number, number][];
  closed: boolean;
}

export const SHOT_OUTPUT_ID = 'shot_output';

export interface ShotDocument {
  version: '1.0';
  name: string;
  graphPath: string;
  graphHash: string;
  parameterOverrides: Record<string, PreviewParameterValue>;
  durationSeconds: number;
  fps: number;
  width: number;
  height: number;
  components: ShotComponent[];
  objectAnimations: ShotObjectAnimation[];
  cameras: ShotCamera[];
  motionCurves: ShotMotionCurve[];
  cameraEdges: ShotCameraEdge[];
  activeCameraId: string;
  selectedNodeId: string;
  shotOutputPosition: { x: number; y: number };
  camera: PhysicalCameraState;
  cameraKeyframes: ShotCameraKeyframe[];
}

const DEFAULT_TRANSFORM: ShotTransform = {
  position: [0, 0, 0],
  rotationEulerDeg: [0, 0, 0],
  scale: [1, 1, 1],
};

const DEFAULT_OUTPUT_POSITION = { x: 420, y: 160 };

export function createDefaultCamera(
  id = 'cam_a',
  name = 'Camera A',
  x = 80,
  y = 160,
  presetId = DEFAULT_CAMERA_PRESET_ID,
): ShotCamera {
  const preset = getCameraPreset(presetId) ?? getCameraPreset(DEFAULT_CAMERA_PRESET_ID)!;
  return {
    id,
    name: name.trim() || preset.name,
    presetId: preset.id,
    position: { x, y },
    camera: applyCameraPreset(defaultPhysicalCamera(), preset.id),
    cameraKeyframes: [],
  };
}

export function createDefaultShot(name = 'Shot 01'): ShotDocument {
  const camera = createDefaultCamera();
  return {
    version: '1.0',
    name,
    graphPath: '',
    graphHash: '',
    parameterOverrides: {},
    durationSeconds: 5,
    fps: 24,
    width: 1920,
    height: 1080,
    components: [],
    objectAnimations: [],
    cameras: [camera],
    motionCurves: [],
    cameraEdges: [{ id: 'e_cam_a_out', source: camera.id, target: SHOT_OUTPUT_ID }],
    activeCameraId: camera.id,
    selectedNodeId: camera.id,
    shotOutputPosition: { ...DEFAULT_OUTPUT_POSITION },
    camera: camera.camera,
    cameraKeyframes: [],
  };
}

function finiteNumber(value: unknown, fallback: number, min: number, max: number): number {
  return typeof value === 'number' && Number.isFinite(value)
    ? Math.min(max, Math.max(min, value))
    : fallback;
}

function vec3(value: unknown, fallback: [number, number, number]): [number, number, number] {
  return Array.isArray(value) && value.length === 3 && value.every((axis) => Number.isFinite(axis))
    ? [value[0] as number, value[1] as number, value[2] as number]
    : [...fallback];
}

function normalizeTransform(value: unknown): ShotTransform {
  const raw = value && typeof value === 'object' ? value as Partial<ShotTransform> : {};
  return {
    position: vec3(raw.position, DEFAULT_TRANSFORM.position),
    rotationEulerDeg: vec3(raw.rotationEulerDeg, DEFAULT_TRANSFORM.rotationEulerDeg),
    scale: vec3(raw.scale, DEFAULT_TRANSFORM.scale).map((axis) => Math.max(0.001, axis)) as [number, number, number],
  };
}

function normalizeComponents(value: unknown): ShotComponent[] {
  if (!Array.isArray(value)) return [];
  const ids = new Set<string>();
  const result: ShotComponent[] = [];
  for (const entry of value) {
    if (!entry || typeof entry !== 'object') continue;
    const raw = entry as Partial<ShotComponent>;
    const componentId = typeof raw.componentId === 'string' ? raw.componentId.trim() : '';
    if (!componentId || ids.has(componentId)) continue;
    ids.add(componentId);
    result.push({
      componentId,
      ...(typeof raw.assetId === 'string' ? { assetId: raw.assetId } : {}),
      ...(typeof raw.role === 'string' ? { role: raw.role } : {}),
      ...(raw.bounds ? { bounds: raw.bounds } : {}),
      ...(raw.anchors ? { anchors: raw.anchors } : {}),
      transform: normalizeTransform(raw.transform),
    });
  }
  return result;
}

function normalizeObjectAnimations(value: unknown): ShotObjectAnimation[] {
  if (!Array.isArray(value)) return [];
  return value.flatMap((entry): ShotObjectAnimation[] => {
    if (!entry || typeof entry !== 'object') return [];
    const raw = entry as Partial<ShotObjectAnimation>;
    if (typeof raw.componentId !== 'string' || !raw.componentId.trim()) return [];
    if (typeof raw.clip !== 'string' || !raw.clip.trim()) return [];
    return [{
      componentId: raw.componentId.trim(),
      clip: raw.clip.trim(),
      startSeconds: finiteNumber(raw.startSeconds, 0, 0, 3600),
      playbackRate: finiteNumber(raw.playbackRate, 1, 0.01, 100),
      loop: raw.loop === true,
    }];
  });
}

function normalizeGraphPosition(value: unknown, fallback: { x: number; y: number }): { x: number; y: number } {
  const raw = value && typeof value === 'object' ? value as { x?: unknown; y?: unknown } : {};
  return {
    x: typeof raw.x === 'number' && Number.isFinite(raw.x) ? raw.x : fallback.x,
    y: typeof raw.y === 'number' && Number.isFinite(raw.y) ? raw.y : fallback.y,
  };
}

function normalizeCameraKeyframes(value: unknown, durationSeconds: number): ShotCameraKeyframe[] {
  if (!Array.isArray(value)) return [];
  return value.flatMap((entry, index): ShotCameraKeyframe[] => {
    if (!entry || typeof entry !== 'object') return [];
    const key = entry as Partial<ShotCameraKeyframe>;
    if (!key.value || typeof key.value !== 'object') return [];
    const interpolation: ShotInterpolation = ['linear', 'ease-in', 'ease-out', 'ease-in-out']
      .includes(key.interpolation ?? '') ? key.interpolation as ShotInterpolation : 'linear';
    return [{
      id: typeof key.id === 'string' && key.id ? key.id : `camera_key_${index + 1}`,
      timeSeconds: finiteNumber(key.timeSeconds, 0, 0, durationSeconds),
      interpolation,
      value: key.value,
    }];
  }).sort((a, b) => a.timeSeconds - b.timeSeconds);
}

function normalizeCameras(
  value: unknown,
  fallbackCamera: PhysicalCameraState,
  fallbackKeyframes: ShotCameraKeyframe[],
  durationSeconds: number,
): ShotCamera[] {
  if (Array.isArray(value) && value.length > 0) {
    const ids = new Set<string>();
    const cameras: ShotCamera[] = [];
    value.forEach((entry, index) => {
      if (!entry || typeof entry !== 'object') return;
      const raw = entry as Partial<ShotCamera>;
      const id = typeof raw.id === 'string' && raw.id.trim() && raw.id !== SHOT_OUTPUT_ID
        ? raw.id.trim()
        : `cam_${index + 1}`;
      if (ids.has(id)) return;
      ids.add(id);
      const camera = mergeCameraCommand(fallbackCamera, raw.camera && typeof raw.camera === 'object' ? raw.camera : {});
      cameras.push({
        id,
        name: typeof raw.name === 'string' && raw.name.trim() ? raw.name.trim() : `Camera ${cameras.length + 1}`,
        presetId: resolveCameraPresetId(raw.presetId, camera),
        position: normalizeGraphPosition(raw.position, { x: 80 + cameras.length * 320, y: 160 }),
        camera,
        cameraKeyframes: normalizeCameraKeyframes(raw.cameraKeyframes, durationSeconds),
      });
    });
    if (cameras.length > 0) return cameras;
  }
  return [{
    id: 'cam_a',
    name: 'Camera A',
    presetId: resolveCameraPresetId(undefined, fallbackCamera),
    position: { x: 80, y: 160 },
    camera: fallbackCamera,
    cameraKeyframes: fallbackKeyframes,
  }];
}

function normalizeCameraEdges(
  value: unknown,
  cameraIds: Set<string>,
  curveIds: Set<string>,
): ShotCameraEdge[] {
  const validSource = (id: string) => cameraIds.has(id) || curveIds.has(id);
  const validTarget = (id: string) => id === SHOT_OUTPUT_ID || cameraIds.has(id);
  if (Array.isArray(value) && value.length > 0) {
    const ids = new Set<string>();
    const edges: ShotCameraEdge[] = [];
    value.forEach((entry, index) => {
      if (!entry || typeof entry !== 'object') return;
      const raw = entry as Partial<ShotCameraEdge>;
      const source = typeof raw.source === 'string' ? raw.source.trim() : '';
      const target = typeof raw.target === 'string' ? raw.target.trim() : '';
      if (!validSource(source) || !validTarget(target) || source === target) return;
      if (curveIds.has(source) && !cameraIds.has(target)) return;
      if (cameraIds.has(source) && curveIds.has(target)) return;
      const id = typeof raw.id === 'string' && raw.id.trim() ? raw.id.trim() : `e_${source}_${target}_${index + 1}`;
      if (ids.has(id)) return;
      ids.add(id);
      edges.push({ id, source, target });
    });
    if (edges.length > 0) return edges;
  }
  const first = [...cameraIds][0];
  return first ? [{ id: `e_${first}_out`, source: first, target: SHOT_OUTPUT_ID }] : [];
}

function normalizeMotionCurves(value: unknown): ShotMotionCurve[] {
  if (!Array.isArray(value)) return [];
  const ids = new Set<string>();
  const curves: ShotMotionCurve[] = [];
  value.forEach((entry, index) => {
    if (!entry || typeof entry !== 'object') return;
    const raw = entry as Partial<ShotMotionCurve>;
    const id = typeof raw.id === 'string' && raw.id.trim() && raw.id !== SHOT_OUTPUT_ID
      ? raw.id.trim()
      : `path_${index + 1}`;
    if (ids.has(id)) return;
    ids.add(id);
    const controlPoints = Array.isArray(raw.controlPoints)
      ? raw.controlPoints.flatMap((point): [number, number, number][] => (
        Array.isArray(point) && point.length === 3 && point.every((axis) => Number.isFinite(axis))
          ? [[point[0] as number, point[1] as number, point[2] as number]]
          : []
      ))
      : [];
    curves.push({
      id,
      name: typeof raw.name === 'string' && raw.name.trim() ? raw.name.trim() : `Motion Curve ${curves.length + 1}`,
      position: normalizeGraphPosition(raw.position, { x: 80 + curves.length * 320, y: 0 }),
      controlPoints: controlPoints.length >= 2 ? controlPoints : [[-1.5, 1.5, 4], [0, 1.5, 3], [1.8, 1.2, 1.2]],
      closed: raw.closed === true,
    });
  });
  return curves;
}

export function findShotCamera(shot: ShotDocument, cameraId?: string | null): ShotCamera | null {
  const id = cameraId || shot.activeCameraId;
  return shot.cameras.find((camera) => camera.id === id) ?? shot.cameras[0] ?? null;
}

export function syncShotCameras(shot: ShotDocument): ShotDocument {
  const active = findShotCamera(shot, shot.activeCameraId);
  if (!active) return shot;
  return {
    ...shot,
    motionCurves: shot.motionCurves ?? [],
    selectedNodeId: shot.selectedNodeId || active.id,
    cameras: shot.cameras.map((camera) => (
      camera.id === active.id
        ? { ...camera, camera: shot.camera, cameraKeyframes: shot.cameraKeyframes }
        : camera
    )),
    activeCameraId: active.id,
  };
}

export function findShotMotionCurve(shot: ShotDocument, curveId?: string | null): ShotMotionCurve | null {
  if (!curveId) return null;
  return shot.motionCurves.find((curve) => curve.id === curveId) ?? null;
}

export function selectShotCamera(shot: ShotDocument, cameraId: string): ShotDocument {
  const synced = syncShotCameras(shot);
  const camera = findShotCamera(synced, cameraId);
  if (!camera) return synced;
  return {
    ...synced,
    activeCameraId: camera.id,
    selectedNodeId: camera.id,
    camera: camera.camera,
    cameraKeyframes: camera.cameraKeyframes,
  };
}

export function selectShotNode(shot: ShotDocument, nodeId: string): ShotDocument {
  if (nodeId === SHOT_OUTPUT_ID) return { ...shot, selectedNodeId: nodeId };
  if (findShotMotionCurve(shot, nodeId)) {
    return { ...syncShotCameras(shot), selectedNodeId: nodeId };
  }
  return selectShotCamera(shot, nodeId);
}

export function normalizeShotDocument(value: unknown): ShotDocument {
  const fallback = createDefaultShot();
  const raw = value && typeof value === 'object' ? value as Partial<ShotDocument> : {};
  const durationSeconds = finiteNumber(raw.durationSeconds, fallback.durationSeconds, 0.1, 3600);
  const camera = mergeCameraCommand(
    fallback.camera,
    raw.camera && typeof raw.camera === 'object' ? raw.camera : {},
  );
  const cameraKeyframes = normalizeCameraKeyframes(raw.cameraKeyframes, durationSeconds);
  const cameras = normalizeCameras(raw.cameras, camera, cameraKeyframes, durationSeconds);
  const motionCurves = normalizeMotionCurves(raw.motionCurves);
  const cameraIds = new Set(cameras.map((entry) => entry.id));
  const curveIds = new Set(motionCurves.map((entry) => entry.id));
  const requestedActive = typeof raw.activeCameraId === 'string' ? raw.activeCameraId : cameras[0].id;
  const active = cameras.find((entry) => entry.id === requestedActive) ?? cameras[0];
  const hasCameraList = Array.isArray(raw.cameras) && raw.cameras.length > 0;
  const requestedSelected = typeof raw.selectedNodeId === 'string' ? raw.selectedNodeId : active.id;
  const selectedNodeId = cameraIds.has(requestedSelected) || curveIds.has(requestedSelected) || requestedSelected === SHOT_OUTPUT_ID
    ? requestedSelected
    : active.id;
  return {
    ...fallback,
    name: typeof raw.name === 'string' && raw.name.trim() ? raw.name.trim() : fallback.name,
    graphPath: typeof raw.graphPath === 'string' ? raw.graphPath : '',
    graphHash: typeof raw.graphHash === 'string' ? raw.graphHash : '',
    parameterOverrides: raw.parameterOverrides && typeof raw.parameterOverrides === 'object'
      ? raw.parameterOverrides
      : {},
    durationSeconds,
    fps: finiteNumber(raw.fps, fallback.fps, 1, 120),
    width: Math.round(finiteNumber(raw.width, fallback.width, 16, 8192)),
    height: Math.round(finiteNumber(raw.height, fallback.height, 16, 8192)),
    components: normalizeComponents(raw.components),
    objectAnimations: normalizeObjectAnimations(raw.objectAnimations),
    cameras,
    motionCurves,
    cameraEdges: normalizeCameraEdges(raw.cameraEdges, cameraIds, curveIds),
    activeCameraId: active.id,
    selectedNodeId,
    shotOutputPosition: normalizeGraphPosition(raw.shotOutputPosition, DEFAULT_OUTPUT_POSITION),
    camera: hasCameraList ? active.camera : camera,
    cameraKeyframes: hasCameraList ? active.cameraKeyframes : cameraKeyframes,
  };
}
