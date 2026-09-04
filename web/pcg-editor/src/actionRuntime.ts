import * as THREE from 'three';
import type { ParsedMesh } from './cookResult';

export type ActionVec3 = [number, number, number];

export interface ActionBoneDefinition {
  id: string;
  parent: string | null;
  head: ActionVec3;
  tail: ActionVec3;
  radius: number;
}

export interface ActionComponentDefinition {
  id: string;
  bone: string;
  detachable: boolean;
  /** Logical component-tree parent. Geometry stays baked in root space. */
  parent: string | null;
  /** Semantic role; hair/detail/decal/panel default to rigid binding. */
  role: string;
  skin: 'smooth' | 'rigid';
  pivot?: ActionVec3;
  region?: ActionComponentRegion;
  ownershipPriority: number;
}

export type ActionComponentRegion =
  | { type: 'sphere'; center: ActionVec3; radius: number }
  | { type: 'box'; min: ActionVec3; max: ActionVec3 }
  | { type: 'capsule'; start: ActionVec3; end: ActionVec3; radius: number };

export interface ActionComponentTreeDefinition {
  id: string;
  parent: string | null;
  pivot: ActionVec3;
  tip?: ActionVec3;
  radius: number;
  role: string;
  joint: boolean;
  detachable: boolean;
  skin: 'smooth' | 'rigid';
  region?: ActionComponentRegion;
  ownershipPriority: number;
  primaryChild?: string;
}

export interface ActionTrackDefinition {
  bone: string;
  property: 'quaternion' | 'position' | 'scale';
  interpolation: 'LINEAR' | 'STEP';
  times: number[];
  values: number[];
}

export interface ActionClipDefinition {
  name: string;
  duration: number;
  /** null means the clip's pose return was not measured; never guess repeat. */
  loop: boolean | null;
  tracks: ActionTrackDefinition[];
}

export interface ActionSocketDefinition {
  id: string;
  bone: string;
  position?: ActionVec3;
  rotation?: [number, number, number, number];
}

export interface ActionRigDefinition {
  schemaVersion: 1 | 2;
  sourceRoute: 'explicit' | 'componentTree';
  bones: ActionBoneDefinition[];
  components: ActionComponentDefinition[];
  componentTree: ActionComponentTreeDefinition[];
  clips: ActionClipDefinition[];
  sockets: ActionSocketDefinition[];
  colliders: Record<string, unknown>[];
  destructionGroups: Record<string, unknown>[];
}

export interface ActionRuntimeMetadata {
  schemaVersion: 1;
  sourceNode: string;
  rig: ActionRigDefinition;
  skinMode: 'geodesic' | 'distance' | 'rigid';
  maxInfluences: number;
  falloff: number;
  geodesicResolution: number;
  componentMode: 'none' | 'dominantBone' | 'semanticRegion';
  splitComponents: boolean;
  autoplay: string;
  playbackSpeed: number;
}

export interface ActionSkinBinding {
  joints: Uint16Array;
  weights: Float32Array;
  dominantBones: Uint16Array;
  method: 'geodesic' | 'distance' | 'rigid' | 'preserved';
  unreachableVertexCount: number;
  voxelStep?: number;
  rigidVertexCount: number;
}

export interface ActionComponentIndexSet {
  component: ActionComponentDefinition;
  indices: Uint32Array;
  materialGroups: Array<{ start: number; count: number; materialIndex: number }>;
  sourceTriangles: Uint32Array;
}

export type ActionLoopSource = 'authored' | 'unmeasured' | 'previewOverride';

export interface ActionAnimationClipInfo {
  name: string;
  duration: number;
  loop: boolean;
  loopSource: ActionLoopSource;
}

/** Editor-facing bone metadata. tailOffset is expressed in the bone's local space. */
export interface ActionBoneInfo {
  id: string;
  name: string;
  parent: string | null;
  tailOffset: ActionVec3;
  radius: number;
}

/** A semantic component backed by its own visual when splitComponents is enabled. */
export interface ActionComponentInfo {
  id: string;
  parent: string | null;
  bone: string;
  role: string;
  skin: 'smooth' | 'rigid';
  detachable: boolean;
  visible: boolean;
  triangleCount: number;
}

export interface ActionPlaybackState {
  clipName: string | null;
  currentTime: number;
  duration: number;
  playing: boolean;
  paused: boolean;
  loop: boolean;
  loopSource: ActionAnimationClipInfo['loopSource'];
  speed: number;
}

export interface ActionRuntimeController {
  readonly animationNames: string[];
  readonly clips: ActionAnimationClipInfo[];
  readonly bones: ActionBoneInfo[];
  readonly components: ActionComponentInfo[];
  readonly componentNames: string[];
  readonly splitComponents: boolean;
  readonly currentAnimation: string | null;
  play(name: string): boolean;
  pause(): void;
  resume(): boolean;
  stop(): void;
  seek(name: string, timeSeconds: number): boolean;
  advance(deltaSeconds: number): void;
  setPlaybackSpeed(speed: number): void;
  setLoop(loop: boolean): void;
  getPlaybackState(): ActionPlaybackState;
  getBoneObject(id: string): THREE.Bone | null;
  resetPose(): void;
  setComponentVisible(id: string, visible: boolean): boolean;
  setExplode(amount: number): void;
  inspect(): ActionRuntimeDiagnostics;
  dispose(): void;
}

export interface ActionRuntimeDiagnostics {
  bindingMethod: ActionSkinBinding['method'];
  unreachableVertexCount: number;
  rigidVertexCount: number;
  visibleMeshCount: number;
  visibleSkinnedMeshCount: number;
  allVisibleMeshesBound: boolean;
  sampledVertexCount: number;
  nonFiniteSampleCount: number;
  maxSkinIndex: number;
  maxWeightError: number;
  atRestPose: boolean;
  sampledRestVertexMaxDelta: number | null;
  maxBoneScaleDelta: number;
}

export interface BuiltActionRuntime {
  root: THREE.Group;
  skeleton: THREE.Skeleton;
  bones: THREE.Bone[];
  componentPivots: Record<string, THREE.Group>;
  controller: ActionRuntimeController;
  binding: ActionSkinBinding;
}

function record(value: unknown, label: string): Record<string, unknown> {
  if (!value || typeof value !== 'object' || Array.isArray(value)) {
    throw new Error(`${label} must be an object`);
  }
  return value as Record<string, unknown>;
}

function stringValue(value: unknown, label: string): string {
  if (typeof value !== 'string' || value.length === 0) throw new Error(`${label} must be a string`);
  return value;
}

function numberValue(value: unknown, label: string): number {
  if (typeof value !== 'number' || !Number.isFinite(value)) throw new Error(`${label} must be finite`);
  return value;
}

function vec3(value: unknown, label: string): ActionVec3 {
  if (!Array.isArray(value) || value.length !== 3) throw new Error(`${label} must be a vec3`);
  return [
    numberValue(value[0], `${label}[0]`),
    numberValue(value[1], `${label}[1]`),
    numberValue(value[2], `${label}[2]`),
  ];
}

function numberArray(value: unknown, label: string): number[] {
  if (!Array.isArray(value) || value.length === 0) throw new Error(`${label} must be a non-empty array`);
  return value.map((entry, index) => numberValue(entry, `${label}[${index}]`));
}

function objectArray(value: unknown, label: string): Record<string, unknown>[] {
  if (value === undefined) return [];
  if (!Array.isArray(value)) throw new Error(`${label} must be an array`);
  return value.map((entry, index) => record(entry, `${label}[${index}]`));
}

const RIGID_COMPONENT_ROLES = new Set(['hair', 'detail', 'decal', 'panel']);

function componentSkin(value: unknown, role: string): 'smooth' | 'rigid' {
  if (value === undefined) return RIGID_COMPONENT_ROLES.has(role) ? 'rigid' : 'smooth';
  if (value !== 'smooth' && value !== 'rigid') throw new Error('component skin must be smooth or rigid');
  return value;
}

function parseComponentRegion(
  value: unknown,
  label: string,
  fallbackCenter?: ActionVec3,
): ActionComponentRegion | undefined {
  if (value === undefined) return undefined;
  const region = record(value, label);
  const type = stringValue(region.type, `${label}.type`);
  if (type === 'sphere') {
    const radius = numberValue(region.radius, `${label}.radius`);
    if (radius <= 0) throw new Error(`${label}.radius must be positive`);
    return {
      type,
      center: region.center === undefined
        ? (fallbackCenter ?? (() => { throw new Error(`${label}.center is required`); })())
        : vec3(region.center, `${label}.center`),
      radius,
    };
  }
  if (type === 'box') {
    const min = vec3(region.min, `${label}.min`);
    const max = vec3(region.max, `${label}.max`);
    if (min.some((entry, axis) => entry >= max[axis])) {
      throw new Error(`${label} box min must be smaller than max`);
    }
    return { type, min, max };
  }
  if (type === 'capsule') {
    const radius = numberValue(region.radius, `${label}.radius`);
    if (radius <= 0) throw new Error(`${label}.radius must be positive`);
    return {
      type,
      start: vec3(region.start, `${label}.start`),
      end: vec3(region.end, `${label}.end`),
      radius,
    };
  }
  throw new Error(`${label}.type must be sphere, box, or capsule`);
}

function parseExplicitBones(source: Record<string, unknown>): ActionBoneDefinition[] {
  if (!Array.isArray(source.bones) || source.bones.length === 0) {
    throw new Error('rig.bones must contain at least one bone');
  }
  const boneIds = new Set<string>();
  const bones = source.bones.map((entry, index): ActionBoneDefinition => {
    const bone = record(entry, `rig.bones[${index}]`);
    const id = stringValue(bone.id, `rig.bones[${index}].id`);
    if (boneIds.has(id)) throw new Error(`duplicate bone id ${id}`);
    boneIds.add(id);
    const parent = bone.parent == null ? null : stringValue(bone.parent, `${id}.parent`);
    const radius = bone.radius === undefined ? 0.1 : numberValue(bone.radius, `${id}.radius`);
    if (radius <= 0) throw new Error(`${id}.radius must be positive`);
    return {
      id,
      parent,
      head: vec3(bone.head, `${id}.head`),
      tail: vec3(bone.tail, `${id}.tail`),
      radius,
    };
  });
  for (const bone of bones) {
    if (bone.parent && (!boneIds.has(bone.parent) || bone.parent === bone.id)) {
      throw new Error(`${bone.id}.parent must reference another bone`);
    }
  }
  const byId = new Map(bones.map((bone) => [bone.id, bone]));
  const ordered: ActionBoneDefinition[] = [];
  const visiting = new Set<string>();
  const visited = new Set<string>();
  const visit = (bone: ActionBoneDefinition): void => {
    if (visiting.has(bone.id)) throw new Error('rig.bones must not contain cycles');
    if (visited.has(bone.id)) return;
    visiting.add(bone.id);
    if (bone.parent) visit(byId.get(bone.parent)!);
    visiting.delete(bone.id);
    visited.add(bone.id);
    ordered.push(bone);
  };
  bones.forEach(visit);
  return ordered;
}

function parseComponentTree(source: Record<string, unknown>): {
  tree: ActionComponentTreeDefinition[];
  bones: ActionBoneDefinition[];
  components: ActionComponentDefinition[];
} {
  const raw = objectArray(source.componentTree, 'rig.componentTree');
  if (raw.length === 0) throw new Error('rig.componentTree must contain at least one component');
  const ids = new Set<string>();
  const parsed = raw.map((component, index): ActionComponentTreeDefinition => {
    const id = stringValue(component.id, `rig.componentTree[${index}].id`);
    if (ids.has(id)) throw new Error(`duplicate component id ${id}`);
    ids.add(id);
    const pivot = vec3(component.pivot, `${id}.pivot`);
    const role = typeof component.role === 'string' && component.role.length > 0
      ? component.role
      : 'structural';
    const radius = component.radius === undefined ? 0.1 : numberValue(component.radius, `${id}.radius`);
    if (radius <= 0) throw new Error(`${id}.radius must be positive`);
    const ownershipPriority = component.ownershipPriority === undefined
      ? 0
      : numberValue(component.ownershipPriority, `${id}.ownershipPriority`);
    return {
      id,
      parent: component.parent == null ? null : stringValue(component.parent, `${id}.parent`),
      pivot,
      ...(component.tip === undefined ? {} : { tip: vec3(component.tip, `${id}.tip`) }),
      radius,
      role,
      joint: component.joint === true || component.parent == null,
      detachable: component.detachable === true,
      skin: componentSkin(component.skin, role),
      ...(component.region === undefined ? {} : {
        region: parseComponentRegion(component.region, `${id}.region`, pivot),
      }),
      ownershipPriority,
      ...(component.primaryChild === undefined ? {} : {
        primaryChild: stringValue(component.primaryChild, `${id}.primaryChild`),
      }),
    };
  });
  const byId = new Map(parsed.map((component) => [component.id, component]));
  const roots = parsed.filter((component) => component.parent === null);
  if (roots.length !== 1) throw new Error('rig.componentTree must contain exactly one root');
  for (const component of parsed) {
    if (component.parent && (!byId.has(component.parent) || component.parent === component.id)) {
      throw new Error(`${component.id}.parent must reference another component`);
    }
    if (component.primaryChild && !byId.has(component.primaryChild)) {
      throw new Error(`${component.id}.primaryChild references a missing component`);
    }
  }
  const children = new Map<string, ActionComponentTreeDefinition[]>();
  for (const component of parsed) {
    if (!component.parent) continue;
    const siblings = children.get(component.parent) ?? [];
    siblings.push(component);
    children.set(component.parent, siblings);
  }
  const ordered: ActionComponentTreeDefinition[] = [];
  const visiting = new Set<string>();
  const visited = new Set<string>();
  const visit = (component: ActionComponentTreeDefinition): void => {
    if (visiting.has(component.id)) throw new Error('rig.componentTree must not contain cycles');
    if (visited.has(component.id)) return;
    visiting.add(component.id);
    ordered.push(component);
    for (const child of children.get(component.id) ?? []) visit(child);
    visiting.delete(component.id);
    visited.add(component.id);
  };
  visit(roots[0]);
  if (visited.size !== parsed.length) throw new Error('rig.componentTree contains unreachable components');

  const jointIds = new Set(ordered.filter((component) => component.joint).map((component) => component.id));
  const nearestJoint = (component: ActionComponentTreeDefinition | undefined): string => {
    let current = component;
    while (current && !jointIds.has(current.id)) current = current.parent ? byId.get(current.parent) : undefined;
    if (!current) throw new Error('every component must descend from the root joint');
    return current.id;
  };
  const jointChildren = new Map<string, ActionComponentTreeDefinition[]>();
  for (const component of ordered) {
    if (!component.joint || component.parent === null) continue;
    const parentJoint = nearestJoint(byId.get(component.parent));
    const entries = jointChildren.get(parentJoint) ?? [];
    entries.push(component);
    jointChildren.set(parentJoint, entries);
  }
  const bones = ordered.filter((component) => component.joint).map((component): ActionBoneDefinition => {
    const parent = component.parent ? nearestJoint(byId.get(component.parent)) : null;
    let tail = component.tip;
    if (!tail) {
      const candidates = jointChildren.get(component.id) ?? [];
      const primary = component.primaryChild
        ? candidates.find((candidate) => candidate.id === component.primaryChild)
        : candidates[0];
      if (primary) tail = primary.pivot;
    }
    if (!tail) {
      const parentHead = parent ? byId.get(parent)!.pivot : undefined;
      const direction: ActionVec3 = parentHead
        ? [
            component.pivot[0] - parentHead[0],
            component.pivot[1] - parentHead[1],
            component.pivot[2] - parentHead[2],
          ]
        : [0, 1, 0];
      const length = Math.hypot(...direction) || 1;
      const extent = component.radius * 2;
      tail = [
        component.pivot[0] + direction[0] / length * extent,
        component.pivot[1] + direction[1] / length * extent,
        component.pivot[2] + direction[2] / length * extent,
      ];
    }
    return { id: component.id, parent, head: component.pivot, tail, radius: component.radius };
  });
  const components = ordered.map((component): ActionComponentDefinition => ({
    id: component.id,
    bone: nearestJoint(component),
    parent: component.parent,
    role: component.role,
    skin: component.skin,
    detachable: component.detachable,
    pivot: component.pivot,
    ...(component.region ? { region: component.region } : {}),
    ownershipPriority: component.ownershipPriority,
  }));
  return { tree: ordered, bones, components };
}

function parseRig(value: unknown): ActionRigDefinition {
  const source = record(value, 'rig');
  if (source.schemaVersion !== 1 && source.schemaVersion !== 2) {
    throw new Error('rig.schemaVersion must be 1 or 2');
  }
  const derived = source.schemaVersion === 2 ? parseComponentTree(source) : null;
  const bones = derived?.bones ?? parseExplicitBones(source);
  const boneIds = new Set(bones.map((bone) => bone.id));
  const componentIds = new Set<string>();
  const components = derived?.components ?? objectArray(source.components, 'rig.components').map((component, index) => {
    const id = stringValue(component.id, `rig.components[${index}].id`);
    const bone = stringValue(component.bone, `${id}.bone`);
    if (componentIds.has(id)) throw new Error(`duplicate component id ${id}`);
    if (!boneIds.has(bone)) throw new Error(`${id}.bone references missing bone ${bone}`);
    componentIds.add(id);
    const pivot = component.pivot === undefined ? undefined : vec3(component.pivot, `${id}.pivot`);
    const role = typeof component.role === 'string' && component.role.length > 0
      ? component.role
      : 'structural';
    return {
      id,
      bone,
      parent: component.parent == null ? null : stringValue(component.parent, `${id}.parent`),
      role,
      skin: componentSkin(component.skin, role),
      detachable: component.detachable === true,
      ...(pivot ? { pivot } : {}),
      ...(component.region === undefined ? {} : {
        region: parseComponentRegion(component.region, `${id}.region`, pivot),
      }),
      ownershipPriority: component.ownershipPriority === undefined
        ? 0
        : numberValue(component.ownershipPriority, `${id}.ownershipPriority`),
    } satisfies ActionComponentDefinition;
  });

  const clips = objectArray(source.clips, 'rig.clips').map((clip, clipIndex) => {
    const name = stringValue(clip.name, `rig.clips[${clipIndex}].name`);
    const duration = numberValue(clip.duration, `${name}.duration`);
    if (duration <= 0) throw new Error(`${name}.duration must be positive`);
    const tracks = objectArray(clip.tracks, `${name}.tracks`).map((track, trackIndex) => {
      const bone = stringValue(track.bone, `${name}.tracks[${trackIndex}].bone`);
      if (!boneIds.has(bone)) throw new Error(`${name} track references missing bone ${bone}`);
      const property = stringValue(track.property, `${name}.tracks[${trackIndex}].property`);
      if (property !== 'quaternion' && property !== 'position' && property !== 'scale') {
        throw new Error(`${name} track property ${property} is unsupported`);
      }
      const times = numberArray(track.times, `${name}.${bone}.times`);
      const values = numberArray(track.values, `${name}.${bone}.values`);
      const tupleSize = property === 'quaternion' ? 4 : 3;
      if (values.length !== times.length * tupleSize) {
        throw new Error(`${name}.${bone} values do not match keyframe count`);
      }
      const interpolation = track.interpolation === 'STEP' ? 'STEP' : 'LINEAR';
      return { bone, property, interpolation, times, values } satisfies ActionTrackDefinition;
    });
    const loop = typeof clip.loop === 'boolean' ? clip.loop : null;
    return { name, duration, loop, tracks } satisfies ActionClipDefinition;
  });

  const sockets = objectArray(source.sockets, 'rig.sockets').map((socket, index) => {
    const id = stringValue(socket.id, `rig.sockets[${index}].id`);
    const bone = stringValue(socket.bone, `${id}.bone`);
    if (!boneIds.has(bone)) throw new Error(`${id}.bone references missing bone ${bone}`);
    let rotation: [number, number, number, number] | undefined;
    if (socket.rotation !== undefined) {
      const values = numberArray(socket.rotation, `${id}.rotation`);
      if (values.length !== 4) throw new Error(`${id}.rotation must be a quaternion`);
      rotation = [values[0], values[1], values[2], values[3]];
    }
    return {
      id,
      bone,
      ...(socket.position === undefined ? {} : { position: vec3(socket.position, `${id}.position`) }),
      ...(rotation ? { rotation } : {}),
    } satisfies ActionSocketDefinition;
  });

  return {
    schemaVersion: source.schemaVersion,
    sourceRoute: derived ? 'componentTree' : 'explicit',
    bones,
    components,
    componentTree: derived?.tree ?? [],
    clips,
    sockets,
    colliders: objectArray(source.colliders, 'rig.colliders'),
    destructionGroups: objectArray(source.destructionGroups, 'rig.destructionGroups'),
  };
}

export function parseActionRuntimeFromCookJson(cookJson: string): ActionRuntimeMetadata | null {
  if (!cookJson) return null;
  const root = record(JSON.parse(cookJson), 'cook result');
  const meshMetadata = root.mesh_metadata;
  if (!meshMetadata || typeof meshMetadata !== 'object' || Array.isArray(meshMetadata)) return null;
  const raw = (meshMetadata as Record<string, unknown>).pcg_action_runtime;
  if (raw === undefined) return null;
  const source = record(raw, 'pcg_action_runtime');
  if (source.schemaVersion !== 1) throw new Error('pcg_action_runtime.schemaVersion must be 1');
  const maxInfluences = Math.trunc(numberValue(source.maxInfluences, 'maxInfluences'));
  const falloff = numberValue(source.falloff, 'falloff');
  const playbackSpeed = numberValue(source.playbackSpeed, 'playbackSpeed');
  const geodesicResolution = Math.trunc(
    source.geodesicResolution === undefined
      ? 40
      : numberValue(source.geodesicResolution, 'geodesicResolution'),
  );
  if (
    maxInfluences < 1 || maxInfluences > 4 || falloff <= 0 ||
    geodesicResolution < 16 || geodesicResolution > 96
  ) {
    throw new Error('ActionRig binding options are out of range');
  }
  return {
    schemaVersion: 1,
    sourceNode: typeof source.sourceNode === 'string' ? source.sourceNode : '',
    rig: parseRig(source.rig),
    skinMode: source.skinMode === 'rigid'
      ? 'rigid'
      : source.skinMode === 'geodesic'
        ? 'geodesic'
        : 'distance',
    maxInfluences,
    falloff,
    geodesicResolution,
    componentMode: source.componentMode === 'none'
      ? 'none'
      : source.componentMode === 'semanticRegion'
        ? 'semanticRegion'
        : 'dominantBone',
    splitComponents: source.splitComponents !== false,
    autoplay: typeof source.autoplay === 'string' ? source.autoplay : '',
    playbackSpeed,
  };
}

export function pcgPositionsToThree(source: Float32Array): Float32Array {
  const result = new Float32Array(source.length);
  for (let i = 0; i < source.length; i += 3) {
    result[i] = source[i];
    result[i + 1] = source[i + 1];
    result[i + 2] = -source[i + 2];
  }
  return result;
}

function pcgPointToThree(value: ActionVec3): THREE.Vector3 {
  return new THREE.Vector3(value[0], value[1], -value[2]);
}

interface GeodesicGrid {
  step: number;
  low: [number, number, number];
  dimensions: [number, number, number];
  solid: Uint8Array;
}

class IndexedCellMinHeap {
  private readonly cells: Int32Array;
  private readonly values: Float32Array;
  private readonly positions: Int32Array;
  private length = 0;

  constructor(capacity: number) {
    this.cells = new Int32Array(capacity);
    this.values = new Float32Array(capacity);
    this.positions = new Int32Array(capacity);
    this.positions.fill(-1);
  }

  get size(): number { return this.length; }

  pushOrDecrease(cell: number, value: number): void {
    value = Math.fround(value);
    let index = this.positions[cell];
    if (index < 0) {
      index = this.length++;
      this.cells[index] = cell;
      this.values[index] = value;
      this.positions[cell] = index;
    } else if (value < this.values[index]) {
      this.values[index] = value;
    } else {
      return;
    }
    while (index > 0) {
      const parent = (index - 1) >> 1;
      if (this.values[parent] <= value) break;
      this.cells[index] = this.cells[parent];
      this.values[index] = this.values[parent];
      this.positions[this.cells[index]] = index;
      index = parent;
    }
    this.cells[index] = cell;
    this.values[index] = value;
    this.positions[cell] = index;
  }

  pop(): [number, number] | null {
    if (this.length === 0) return null;
    const resultCell = this.cells[0];
    const resultValue = this.values[0];
    this.positions[resultCell] = -1;
    this.length--;
    if (this.length === 0) return [resultCell, resultValue];
    const lastCell = this.cells[this.length];
    const lastValue = this.values[this.length];
    let index = 0;
    while (true) {
      const left = index * 2 + 1;
      if (left >= this.length) break;
      const right = left + 1;
      const child = right < this.length && this.values[right] < this.values[left]
        ? right
        : left;
      if (this.values[child] >= lastValue) break;
      this.cells[index] = this.cells[child];
      this.values[index] = this.values[child];
      this.positions[this.cells[index]] = index;
      index = child;
    }
    this.cells[index] = lastCell;
    this.values[index] = lastValue;
    this.positions[lastCell] = index;
    return [resultCell, resultValue];
  }
}

function buildGeodesicGrid(
  positions: Float32Array,
  indices: Uint32Array | null,
  resolution: number,
): GeodesicGrid {
  const low: [number, number, number] = [Infinity, Infinity, Infinity];
  const high: [number, number, number] = [-Infinity, -Infinity, -Infinity];
  for (let offset = 0; offset < positions.length; offset += 3) {
    for (let axis = 0; axis < 3; axis++) {
      low[axis] = Math.min(low[axis], positions[offset + axis]);
      high[axis] = Math.max(high[axis], positions[offset + axis]);
    }
  }
  const extent = Math.max(high[0] - low[0], high[1] - low[1], high[2] - low[2], 1e-6);
  const step = extent / resolution;
  const paddedLow: [number, number, number] = [low[0] - step, low[1] - step, low[2] - step];
  const dimensions: [number, number, number] = [
    Math.ceil((high[0] - low[0]) / step) + 3,
    Math.ceil((high[1] - low[1]) / step) + 3,
    Math.ceil((high[2] - low[2]) / step) + 3,
  ];
  const [width, height, depth] = dimensions;
  const cellCount = width * height * depth;
  const surface = new Uint8Array(cellCount);
  const cellIndex = (x: number, y: number, z: number) => x + width * (y + height * z);
  const mark = (x: number, y: number, z: number) => {
    const ix = THREE.MathUtils.clamp(Math.floor((x - paddedLow[0]) / step), 0, width - 1);
    const iy = THREE.MathUtils.clamp(Math.floor((y - paddedLow[1]) / step), 0, height - 1);
    const iz = THREE.MathUtils.clamp(Math.floor((z - paddedLow[2]) / step), 0, depth - 1);
    surface[cellIndex(ix, iy, iz)] = 1;
  };

  for (let offset = 0; offset < positions.length; offset += 3) {
    mark(positions[offset], positions[offset + 1], positions[offset + 2]);
  }
  // Sparse meshes need their triangle interiors rasterized to seal the shell.
  // Dense marching-cubes meshes already place many vertices in every touched voxel;
  // skipping barycentric sampling there keeps million-vertex binding interactive.
  if (indices && positions.length / 3 <= 250_000) {
    for (let offset = 0; offset + 2 < indices.length; offset += 3) {
      const ia = indices[offset] * 3;
      const ib = indices[offset + 1] * 3;
      const ic = indices[offset + 2] * 3;
      const ax = positions[ia]; const ay = positions[ia + 1]; const az = positions[ia + 2];
      const bx = positions[ib]; const by = positions[ib + 1]; const bz = positions[ib + 2];
      const cx = positions[ic]; const cy = positions[ic + 1]; const cz = positions[ic + 2];
      const ab = Math.hypot(ax - bx, ay - by, az - bz);
      const bc = Math.hypot(bx - cx, by - cy, bz - cz);
      const ca = Math.hypot(cx - ax, cy - ay, cz - az);
      const subdivisions = Math.min(24, Math.max(1, Math.ceil(Math.max(ab, bc, ca) / (step * 0.55))));
      for (let row = 0; row <= subdivisions; row++) {
        for (let column = 0; column <= subdivisions - row; column++) {
          const u = row / subdivisions;
          const v = column / subdivisions;
          const w = 1 - u - v;
          mark(ax * w + bx * u + cx * v, ay * w + by * u + cy * v, az * w + bz * u + cz * v);
        }
      }
    }
  }

  const outside = new Uint8Array(cellCount);
  const queue = new Int32Array(cellCount);
  let read = 0;
  let write = 0;
  if (!surface[0]) {
    outside[0] = 1;
    queue[write++] = 0;
  }
  const neighbours = [[-1, 0, 0], [1, 0, 0], [0, -1, 0], [0, 1, 0], [0, 0, -1], [0, 0, 1]];
  while (read < write) {
    const cell = queue[read++];
    const z = Math.floor(cell / (width * height));
    const rem = cell - z * width * height;
    const y = Math.floor(rem / width);
    const x = rem - y * width;
    for (const [dx, dy, dz] of neighbours) {
      const nx = x + dx; const ny = y + dy; const nz = z + dz;
      if (nx < 0 || nx >= width || ny < 0 || ny >= height || nz < 0 || nz >= depth) continue;
      const next = cellIndex(nx, ny, nz);
      if (surface[next] || outside[next]) continue;
      outside[next] = 1;
      queue[write++] = next;
    }
  }
  const solid = new Uint8Array(cellCount);
  for (let cell = 0; cell < cellCount; cell++) solid[cell] = surface[cell] || !outside[cell] ? 1 : 0;
  return { step, low: paddedLow, dimensions, solid };
}

function computeGeodesicActionSkinBinding(
  positions: Float32Array,
  indices: Uint32Array | null,
  runtime: ActionRuntimeMetadata,
): ActionSkinBinding {
  const vertexCount = Math.trunc(positions.length / 3);
  const joints = new Uint16Array(vertexCount * 4);
  const weights = new Float32Array(vertexCount * 4);
  const dominantBones = new Uint16Array(vertexCount);
  const influenceCount = runtime.maxInfluences;
  const grid = buildGeodesicGrid(positions, indices, runtime.geodesicResolution);
  const [width, height, depth] = grid.dimensions;
  const cellCount = width * height * depth;
  const cellIndex = (x: number, y: number, z: number) => x + width * (y + height * z);
  const pointCell = (x: number, y: number, z: number) => {
    const ix = THREE.MathUtils.clamp(Math.floor((x - grid.low[0]) / grid.step), 0, width - 1);
    const iy = THREE.MathUtils.clamp(Math.floor((y - grid.low[1]) / grid.step), 0, height - 1);
    const iz = THREE.MathUtils.clamp(Math.floor((z - grid.low[2]) / grid.step), 0, depth - 1);
    return cellIndex(ix, iy, iz);
  };
  const labels = new Int32Array(cellCount * influenceCount);
  labels.fill(-1);
  const distances = new Float32Array(cellCount * influenceCount);
  distances.fill(Infinity);
  const insertCellCandidate = (cell: number, bone: number, distance: number): void => {
    const base = cell * influenceCount;
    let slot = influenceCount - 1;
    if (distance >= distances[base + slot]) return;
    labels[base + slot] = bone;
    distances[base + slot] = distance;
    while (slot > 0 && distances[base + slot] < distances[base + slot - 1]) {
      const previousDistance = distances[base + slot - 1];
      const previousLabel = labels[base + slot - 1];
      distances[base + slot - 1] = distances[base + slot];
      labels[base + slot - 1] = labels[base + slot];
      distances[base + slot] = previousDistance;
      labels[base + slot] = previousLabel;
      slot--;
    }
  };

  const nearestSolidCell = (source: number): number | null => {
    if (grid.solid[source]) return source;
    const z = Math.floor(source / (width * height));
    const rem = source - z * width * height;
    const y = Math.floor(rem / width);
    const x = rem - y * width;
    for (let radius = 1; radius <= 4; radius++) {
      let nearest: number | null = null;
      let nearestDistance = Infinity;
      for (let dz = -radius; dz <= radius; dz++) {
        for (let dy = -radius; dy <= radius; dy++) {
          for (let dx = -radius; dx <= radius; dx++) {
            const nx = x + dx; const ny = y + dy; const nz = z + dz;
            if (nx < 0 || nx >= width || ny < 0 || ny >= height || nz < 0 || nz >= depth) continue;
            const candidate = cellIndex(nx, ny, nz);
            if (!grid.solid[candidate]) continue;
            const squared = dx * dx + dy * dy + dz * dz;
            if (squared < nearestDistance) {
              nearestDistance = squared;
              nearest = candidate;
            }
          }
        }
      }
      if (nearest != null) return nearest;
    }
    return null;
  };

  const neighbours: Array<[number, number, number, number]> = [];
  for (let dz = -1; dz <= 1; dz++) {
    for (let dy = -1; dy <= 1; dy++) {
      for (let dx = -1; dx <= 1; dx++) {
        if (dx === 0 && dy === 0 && dz === 0) continue;
        neighbours.push([dx, dy, dz, Math.hypot(dx, dy, dz)]);
      }
    }
  }

  runtime.rig.bones.forEach((definition, boneIndex) => {
    const field = new Float32Array(cellCount);
    field.fill(Infinity);
    const heap = new IndexedCellMinHeap(cellCount);
    const start = pcgPointToThree(definition.head);
    const end = pcgPointToThree(definition.tail);
    const length = start.distanceTo(end);
    const samples = Math.max(1, Math.ceil(length / (grid.step * 0.5)));
    const seeded = new Set<number>();
    for (let sample = 0; sample <= samples; sample++) {
      const t = sample / samples;
      const source = pointCell(
        THREE.MathUtils.lerp(start.x, end.x, t),
        THREE.MathUtils.lerp(start.y, end.y, t),
        THREE.MathUtils.lerp(start.z, end.z, t),
      );
      const cell = nearestSolidCell(source);
      if (cell != null && !seeded.has(cell)) {
        seeded.add(cell);
        field[cell] = 0;
        heap.pushOrDecrease(cell, 0);
      }
    }

    while (heap.size > 0) {
      const [cell, distance] = heap.pop()!;
      if (distance !== field[cell]) continue;
      const z = Math.floor(cell / (width * height));
      const rem = cell - z * width * height;
      const y = Math.floor(rem / width);
      const x = rem - y * width;
      for (const [dx, dy, dz, cost] of neighbours) {
        const nx = x + dx; const ny = y + dy; const nz = z + dz;
        if (nx < 0 || nx >= width || ny < 0 || ny >= height || nz < 0 || nz >= depth) continue;
        const next = cellIndex(nx, ny, nz);
        if (!grid.solid[next]) continue;
        const candidate = Math.fround(distance + cost);
        if (candidate >= field[next]) continue;
        field[next] = candidate;
        heap.pushOrDecrease(next, candidate);
      }
    }
    for (let cell = 0; cell < cellCount; cell++) {
      if (Number.isFinite(field[cell])) insertCellCandidate(cell, boneIndex, field[cell]);
    }
  });

  let unreachableVertexCount = 0;
  for (let vertex = 0; vertex < vertexCount; vertex++) {
    const cell = pointCell(
      positions[vertex * 3], positions[vertex * 3 + 1], positions[vertex * 3 + 2],
    );
    const base = cell * influenceCount;
    let total = 0;
    for (let influence = 0; influence < influenceCount; influence++) {
      const distance = distances[base + influence];
      if (!Number.isFinite(distance) || labels[base + influence] < 0) continue;
      const score = 1 / Math.pow(Math.max(distance, 0.5), runtime.falloff);
      joints[vertex * 4 + influence] = labels[base + influence];
      weights[vertex * 4 + influence] = score;
      total += score;
    }
    if (!(total > 0)) {
      unreachableVertexCount++;
      const point = new THREE.Vector3(
        positions[vertex * 3], positions[vertex * 3 + 1], positions[vertex * 3 + 2],
      );
      let nearestBone = 0;
      let nearestDistance = Infinity;
      runtime.rig.bones.forEach((bone, boneIndex) => {
        const start = pcgPointToThree(bone.head);
        const end = pcgPointToThree(bone.tail);
        const direction = end.clone().sub(start);
        const lengthSquared = direction.lengthSq();
        const t = lengthSquared <= 1e-16
          ? 0
          : THREE.MathUtils.clamp(point.clone().sub(start).dot(direction) / lengthSquared, 0, 1);
        const distance = point.distanceTo(start.addScaledVector(direction, t));
        if (distance < nearestDistance) {
          nearestDistance = distance;
          nearestBone = boneIndex;
        }
      });
      joints[vertex * 4] = nearestBone;
      dominantBones[vertex] = nearestBone;
      weights[vertex * 4] = 1;
      continue;
    }
    dominantBones[vertex] = joints[vertex * 4];
    for (let influence = 0; influence < influenceCount; influence++) {
      weights[vertex * 4 + influence] /= total;
    }
  }
  return {
    joints,
    weights,
    dominantBones,
    method: 'geodesic',
    unreachableVertexCount,
    voxelStep: grid.step,
    rigidVertexCount: 0,
  };
}

function computeDistanceActionSkinBinding(
  positions: Float32Array,
  runtime: ActionRuntimeMetadata,
): ActionSkinBinding {
  const vertexCount = Math.trunc(positions.length / 3);
  const joints = new Uint16Array(vertexCount * 4);
  const weights = new Float32Array(vertexCount * 4);
  const dominantBones = new Uint16Array(vertexCount);
  const segments = runtime.rig.bones.map((bone) => {
    const start = pcgPointToThree(bone.head);
    const end = pcgPointToThree(bone.tail);
    const dx = end.x - start.x;
    const dy = end.y - start.y;
    const dz = end.z - start.z;
    return { start, dx, dy, dz, lengthSquared: dx * dx + dy * dy + dz * dz };
  });
  const influenceCount = runtime.skinMode === 'rigid' ? 1 : runtime.maxInfluences;
  const bestIndices = new Int32Array(4);
  const bestScores = new Float64Array(4);

  for (let vertex = 0; vertex < vertexCount; vertex++) {
    bestIndices.fill(0);
    bestScores.fill(-1);
    const px = positions[vertex * 3];
    const py = positions[vertex * 3 + 1];
    const pz = positions[vertex * 3 + 2];
    for (let boneIndex = 0; boneIndex < runtime.rig.bones.length; boneIndex++) {
      const segment = segments[boneIndex];
      const fromStartX = px - segment.start.x;
      const fromStartY = py - segment.start.y;
      const fromStartZ = pz - segment.start.z;
      const t = segment.lengthSquared <= 1e-16
        ? 0
        : THREE.MathUtils.clamp(
            (fromStartX * segment.dx + fromStartY * segment.dy + fromStartZ * segment.dz) /
              segment.lengthSquared,
            0,
            1,
          );
      const distanceX = fromStartX - segment.dx * t;
      const distanceY = fromStartY - segment.dy * t;
      const distanceZ = fromStartZ - segment.dz * t;
      const distance = Math.sqrt(
        distanceX * distanceX + distanceY * distanceY + distanceZ * distanceZ,
      );
      const normalizedDistance = distance / Math.max(runtime.rig.bones[boneIndex].radius, 1e-6);
      const score = 1 / (1 + Math.pow(Math.max(normalizedDistance, 0), runtime.falloff));
      for (let slot = 0; slot < influenceCount; slot++) {
        if (score <= bestScores[slot]) continue;
        for (let shift = influenceCount - 1; shift > slot; shift--) {
          bestScores[shift] = bestScores[shift - 1];
          bestIndices[shift] = bestIndices[shift - 1];
        }
        bestScores[slot] = score;
        bestIndices[slot] = boneIndex;
        break;
      }
    }
    let total = 0;
    for (let influence = 0; influence < influenceCount; influence++) {
      total += Math.max(bestScores[influence], 0);
    }
    total ||= 1;
    dominantBones[vertex] = bestIndices[0];
    for (let influence = 0; influence < influenceCount; influence++) {
      joints[vertex * 4 + influence] = bestIndices[influence];
      weights[vertex * 4 + influence] = Math.max(bestScores[influence], 0) / total;
    }
  }
  return {
    joints,
    weights,
    dominantBones,
    method: runtime.skinMode === 'rigid' ? 'rigid' : 'distance',
    unreachableVertexCount: 0,
    rigidVertexCount: runtime.skinMode === 'rigid' ? vertexCount : 0,
  };
}

export function computeActionSkinBinding(
  positions: Float32Array,
  runtime: ActionRuntimeMetadata,
  indices: Uint32Array | null = null,
): ActionSkinBinding {
  const binding = runtime.skinMode === 'geodesic'
    ? computeGeodesicActionSkinBinding(positions, indices, runtime)
    : computeDistanceActionSkinBinding(positions, runtime);
  if (runtime.skinMode === 'rigid' || !indices || indices.length === 0) return binding;
  const owners = assignTriangleOwners(positions, indices, runtime, binding);
  const boneIndex = new Map(runtime.rig.bones.map((bone, index) => [bone.id, index]));
  const rigidVertices = new Uint8Array(Math.trunc(positions.length / 3));
  for (let triangle = 0; triangle < owners.length; triangle++) {
    const component = runtime.rig.components[owners[triangle]];
    if (!component || component.skin !== 'rigid') continue;
    const joint = boneIndex.get(component.bone) ?? 0;
    for (let corner = 0; corner < 3; corner++) {
      const vertex = indices[triangle * 3 + corner];
      const offset = vertex * 4;
      binding.joints.fill(0, offset, offset + 4);
      binding.weights.fill(0, offset, offset + 4);
      binding.joints[offset] = joint;
      binding.weights[offset] = 1;
      binding.dominantBones[vertex] = joint;
      rigidVertices[vertex] = 1;
    }
  }
  binding.rigidVertexCount = rigidVertices.reduce((sum, value) => sum + value, 0);
  return binding;
}

function defaultComponent(runtime: ActionRuntimeMetadata): ActionComponentDefinition {
  return runtime.rig.components[0] ?? {
    id: 'body',
    bone: runtime.rig.bones[0].id,
    detachable: false,
    parent: null,
    role: 'structural',
    skin: 'smooth',
    ownershipPriority: 0,
  };
}

function componentRegionScore(
  component: ActionComponentDefinition,
  x: number,
  y: number,
  z: number,
): number {
  const region = component.region;
  if (!region) return Number.POSITIVE_INFINITY;
  if (region.type === 'sphere') {
    const center = pcgPointToThree(region.center);
    return Math.hypot(x - center.x, y - center.y, z - center.z) / region.radius;
  }
  if (region.type === 'capsule') {
    const start = pcgPointToThree(region.start);
    const end = pcgPointToThree(region.end);
    const dx = end.x - start.x;
    const dy = end.y - start.y;
    const dz = end.z - start.z;
    const lengthSquared = dx * dx + dy * dy + dz * dz;
    const t = lengthSquared <= 1e-16
      ? 0
      : THREE.MathUtils.clamp(
          ((x - start.x) * dx + (y - start.y) * dy + (z - start.z) * dz) / lengthSquared,
          0,
          1,
        );
    return Math.hypot(x - start.x - dx * t, y - start.y - dy * t, z - start.z - dz * t) /
      region.radius;
  }
  const first = pcgPointToThree(region.min);
  const second = pcgPointToThree(region.max);
  const lowX = Math.min(first.x, second.x); const highX = Math.max(first.x, second.x);
  const lowY = Math.min(first.y, second.y); const highY = Math.max(first.y, second.y);
  const lowZ = Math.min(first.z, second.z); const highZ = Math.max(first.z, second.z);
  const centerX = (lowX + highX) * 0.5; const halfX = Math.max((highX - lowX) * 0.5, 1e-6);
  const centerY = (lowY + highY) * 0.5; const halfY = Math.max((highY - lowY) * 0.5, 1e-6);
  const centerZ = (lowZ + highZ) * 0.5; const halfZ = Math.max((highZ - lowZ) * 0.5, 1e-6);
  return Math.max(
    Math.abs(x - centerX) / halfX,
    Math.abs(y - centerY) / halfY,
    Math.abs(z - centerZ) / halfZ,
  );
}

function assignTriangleOwners(
  positions: Float32Array,
  indices: Uint32Array,
  runtime: ActionRuntimeMetadata,
  binding: ActionSkinBinding,
): Uint16Array {
  const components = runtime.rig.components.length > 0
    ? runtime.rig.components
    : [defaultComponent(runtime)];
  const boneIndex = new Map(runtime.rig.bones.map((bone, index) => [bone.id, index]));
  const boneComponents = new Uint16Array(runtime.rig.bones.length);
  components.forEach((component, componentIndex) => {
    boneComponents[boneIndex.get(component.bone) ?? 0] = componentIndex;
  });
  const triangleCount = Math.trunc(indices.length / 3);
  const owners = new Uint16Array(triangleCount);
  for (let triangle = 0; triangle < triangleCount; triangle++) {
    const offset = triangle * 3;
    const a = boneComponents[binding.dominantBones[indices[offset]]];
    const b = boneComponents[binding.dominantBones[indices[offset + 1]]];
    const c = boneComponents[binding.dominantBones[indices[offset + 2]]];
    let owner = a === b || a === c ? a : b === c ? b : a;
    if (runtime.componentMode === 'semanticRegion') {
      const ia = indices[offset] * 3;
      const ib = indices[offset + 1] * 3;
      const ic = indices[offset + 2] * 3;
      const x = (positions[ia] + positions[ib] + positions[ic]) / 3;
      const y = (positions[ia + 1] + positions[ib + 1] + positions[ic + 1]) / 3;
      const z = (positions[ia + 2] + positions[ib + 2] + positions[ic + 2]) / 3;
      let bestPriority = Number.NEGATIVE_INFINITY;
      let bestScore = Number.POSITIVE_INFINITY;
      components.forEach((component, componentIndex) => {
        const score = componentRegionScore(component, x, y, z);
        if (score > 1) return;
        if (component.ownershipPriority > bestPriority ||
            (component.ownershipPriority === bestPriority && score < bestScore)) {
          bestPriority = component.ownershipPriority;
          bestScore = score;
          owner = componentIndex;
        }
      });
    }
    owners[triangle] = owner;
  }
  return owners;
}

export function partitionActionComponents(
  mesh: ParsedMesh,
  runtime: ActionRuntimeMetadata,
  binding: ActionSkinBinding,
): ActionComponentIndexSet[] {
  const configured = runtime.splitComponents && runtime.componentMode !== 'none'
    ? runtime.rig.components
    : [defaultComponent(runtime)];
  const components = configured.length > 0 ? configured : [defaultComponent(runtime)];
  const triangleCount = Math.trunc(mesh.indices.length / 3);
  const owners = runtime.splitComponents && runtime.componentMode !== 'none'
    ? assignTriangleOwners(pcgPositionsToThree(mesh.positions), mesh.indices, runtime, binding)
    : new Uint16Array(triangleCount);
  const counts = new Uint32Array(components.length);
  for (let triangle = 0; triangle < triangleCount; triangle++) {
    counts[owners[triangle]]++;
  }
  const buckets = components.map((_, index) => ({
    indices: new Uint32Array(counts[index] * 3),
    materials: new Uint32Array(counts[index]),
    triangles: new Uint32Array(counts[index]),
    offset: 0,
  }));
  for (let triangle = 0; triangle < triangleCount; triangle++) {
    const bucket = buckets[owners[triangle]];
    const localTriangle = bucket.offset++;
    const sourceOffset = triangle * 3;
    const targetOffset = localTriangle * 3;
    bucket.indices[targetOffset] = mesh.indices[sourceOffset];
    bucket.indices[targetOffset + 1] = mesh.indices[sourceOffset + 2];
    bucket.indices[targetOffset + 2] = mesh.indices[sourceOffset + 1];
    bucket.materials[localTriangle] = mesh.triangleMaterials?.[triangle] ?? 0;
    bucket.triangles[localTriangle] = triangle;
  }

  return buckets.map((bucket, index) => {
    const materialGroups: ActionComponentIndexSet['materialGroups'] = [];
    let groupStart = 0;
    for (let triangle = 1; triangle <= bucket.materials.length; triangle++) {
      if (triangle < bucket.materials.length && bucket.materials[triangle] === bucket.materials[groupStart]) continue;
      if (triangle > groupStart) {
        materialGroups.push({
          start: groupStart * 3,
          count: (triangle - groupStart) * 3,
          materialIndex: bucket.materials[groupStart] ?? 0,
        });
      }
      groupStart = triangle;
    }
    return {
      component: components[index],
      indices: bucket.indices,
      materialGroups,
      sourceTriangles: bucket.triangles,
    };
  }).filter((entry) => entry.indices.length > 0);
}

function safeNodeName(prefix: string, id: string, index: number): string {
  const clean = id.replace(/[^A-Za-z0-9_-]/g, '_');
  return `${prefix}_${clean || index}`;
}

function threeQuaternionValues(values: readonly number[]): number[] {
  const result = [...values];
  for (let i = 0; i < result.length; i += 4) {
    result[i] = -result[i];
    result[i + 1] = -result[i + 1];
  }
  return result;
}

function threeVectorValues(values: readonly number[]): number[] {
  const result = [...values];
  for (let i = 2; i < result.length; i += 3) result[i] = -result[i];
  return result;
}

function buildAnimationClips(
  runtime: ActionRuntimeMetadata,
  boneNames: ReadonlyMap<string, string>,
): THREE.AnimationClip[] {
  return runtime.rig.clips.map((definition) => {
    const tracks = definition.tracks.map((track) => {
      const interpolation = track.interpolation === 'STEP'
        ? THREE.InterpolateDiscrete
        : THREE.InterpolateLinear;
      const nodeName = boneNames.get(track.bone)!;
      const values = track.property === 'quaternion'
        ? threeQuaternionValues(track.values)
        : track.property === 'position'
          ? threeVectorValues(track.values)
          : [...track.values];
      if (track.property === 'quaternion') {
        return new THREE.QuaternionKeyframeTrack(
          `${nodeName}.quaternion`, track.times, values, interpolation,
        );
      }
      return new THREE.VectorKeyframeTrack(
        `${nodeName}.${track.property}`, track.times, values, interpolation,
      );
    });
    return new THREE.AnimationClip(definition.name, definition.duration, tracks);
  });
}

function setCompactMeshAttributes(
  geometry: THREE.BufferGeometry,
  mesh: ParsedMesh,
  positions: Float32Array,
  binding: ActionSkinBinding,
  sourceIndices: Uint32Array,
): void {
  const remap = new Int32Array(mesh.vertexCount);
  remap.fill(-1);
  let localVertexCount = 0;
  for (const sourceIndex of sourceIndices) {
    if (remap[sourceIndex] >= 0) continue;
    remap[sourceIndex] = localVertexCount++;
  }
  const localPositions = new Float32Array(localVertexCount * 3);
  const localNormals = mesh.normals ? new Float32Array(localVertexCount * 3) : null;
  const localColors = mesh.colors && mesh.colors.length === mesh.vertexCount * 4
    ? new Float32Array(localVertexCount * 4)
    : null;
  const localUvs = mesh.uvs && mesh.uvs.length === mesh.vertexCount * 2
    ? new Float32Array(localVertexCount * 2)
    : null;
  const localJoints = new Uint16Array(localVertexCount * 4);
  const localWeights = new Float32Array(localVertexCount * 4);
  for (let sourceIndex = 0; sourceIndex < mesh.vertexCount; sourceIndex++) {
    const targetIndex = remap[sourceIndex];
    if (targetIndex < 0) continue;
    localPositions[targetIndex * 3] = positions[sourceIndex * 3];
    localPositions[targetIndex * 3 + 1] = positions[sourceIndex * 3 + 1];
    localPositions[targetIndex * 3 + 2] = positions[sourceIndex * 3 + 2];
    if (localNormals && mesh.normals) {
      localNormals[targetIndex * 3] = mesh.normals[sourceIndex * 3];
      localNormals[targetIndex * 3 + 1] = mesh.normals[sourceIndex * 3 + 1];
      localNormals[targetIndex * 3 + 2] = -mesh.normals[sourceIndex * 3 + 2];
    }
    if (localColors && mesh.colors) {
      for (let component = 0; component < 4; component++) {
        localColors[targetIndex * 4 + component] = mesh.colors[sourceIndex * 4 + component];
      }
    }
    if (localUvs && mesh.uvs) {
      localUvs[targetIndex * 2] = mesh.uvs[sourceIndex * 2];
      localUvs[targetIndex * 2 + 1] = mesh.uvs[sourceIndex * 2 + 1];
    }
    for (let influence = 0; influence < 4; influence++) {
      localJoints[targetIndex * 4 + influence] = binding.joints[sourceIndex * 4 + influence];
      localWeights[targetIndex * 4 + influence] = binding.weights[sourceIndex * 4 + influence];
    }
  }
  const localIndices = new Uint32Array(sourceIndices.length);
  for (let index = 0; index < sourceIndices.length; index++) {
    localIndices[index] = remap[sourceIndices[index]];
  }

  geometry.setAttribute('position', new THREE.BufferAttribute(localPositions, 3));
  if (localNormals) geometry.setAttribute('normal', new THREE.BufferAttribute(localNormals, 3));
  if (localColors) geometry.setAttribute('color', new THREE.BufferAttribute(localColors, 4));
  if (localUvs) {
    const uv = new THREE.BufferAttribute(localUvs, 2);
    geometry.setAttribute('uv', uv);
    geometry.setAttribute('uv1', uv);
  }
  geometry.setAttribute('skinIndex', new THREE.Uint16BufferAttribute(localJoints, 4));
  geometry.setAttribute('skinWeight', new THREE.Float32BufferAttribute(localWeights, 4));
  geometry.setIndex(new THREE.BufferAttribute(localIndices, 1));
  if (!localNormals) geometry.computeVertexNormals();
}

function setFullMeshAttributes(
  geometry: THREE.BufferGeometry,
  mesh: ParsedMesh,
  positions: Float32Array,
  binding: ActionSkinBinding,
): void {
  geometry.setAttribute('position', new THREE.BufferAttribute(positions, 3));
  if (mesh.normals) {
    geometry.setAttribute('normal', new THREE.BufferAttribute(pcgPositionsToThree(mesh.normals), 3));
  }
  if (mesh.colors && mesh.colors.length === mesh.vertexCount * 4) {
    geometry.setAttribute('color', new THREE.BufferAttribute(mesh.colors, 4));
  }
  if (mesh.uvs && mesh.uvs.length === mesh.vertexCount * 2) {
    const uv = new THREE.BufferAttribute(mesh.uvs, 2);
    geometry.setAttribute('uv', uv);
    geometry.setAttribute('uv1', uv);
  }
  geometry.setAttribute('skinIndex', new THREE.Uint16BufferAttribute(binding.joints, 4));
  geometry.setAttribute('skinWeight', new THREE.Float32BufferAttribute(binding.weights, 4));
  const indices = new Uint32Array(mesh.indices.length);
  for (let index = 0; index < mesh.indices.length; index += 3) {
    indices[index] = mesh.indices[index];
    indices[index + 1] = mesh.indices[index + 2];
    indices[index + 2] = mesh.indices[index + 1];
  }
  geometry.setIndex(new THREE.BufferAttribute(indices, 1));
  if (mesh.triangleMaterials && mesh.materialSlots.length > 0) {
    let groupStart = 0;
    for (let triangle = 1; triangle <= mesh.triangleMaterials.length; triangle++) {
      if (triangle < mesh.triangleMaterials.length &&
          mesh.triangleMaterials[triangle] === mesh.triangleMaterials[groupStart]) continue;
      geometry.addGroup(
        groupStart * 3,
        (triangle - groupStart) * 3,
        mesh.triangleMaterials[groupStart] ?? 0,
      );
      groupStart = triangle;
    }
  }
  if (!mesh.normals) geometry.computeVertexNormals();
}

function defaultSkinnedMaterial(mesh: ParsedMesh): THREE.MeshStandardMaterial {
  return new THREE.MeshStandardMaterial({
    color: mesh.colors ? 0xffffff : 0x9aa4ae,
    vertexColors: mesh.colors != null,
    roughness: 0.85,
    metalness: 0.05,
    side: THREE.DoubleSide,
  });
}

export function buildActionRigObject(
  mesh: ParsedMesh,
  runtime: ActionRuntimeMetadata,
): BuiltActionRuntime {
  const root = new THREE.Group();
  root.name = 'PCG_Action_Root';
  root.userData.kind = 'action-root';
  const positions = pcgPositionsToThree(mesh.positions);
  const binding = computeActionSkinBinding(positions, runtime, mesh.indices);
  // splitComponents is a semantic/runtime contract, not a preview-quality hint.
  // Keep real per-component SkinnedMeshes even for dense reconstructions so
  // detach/explode operations move closed component index sets instead of
  // translating bones inside one continuous visual (which stretches boundary
  // triangles into spikes). Compact remapping keeps the aggregate vertex data
  // close to the source size; the trade-off is one draw call per component.
  const useSplitVisuals = runtime.splitComponents;
  const componentSets = useSplitVisuals
    ? partitionActionComponents(mesh, runtime, binding)
    : [];
  const boneNames = new Map<string, string>();
  const bones = runtime.rig.bones.map((definition, index) => {
    const bone = new THREE.Bone();
    bone.name = safeNodeName('PCG_Bone', definition.id, index);
    bone.userData.pcgBoneId = definition.id;
    boneNames.set(definition.id, bone.name);
    return bone;
  });
  const boneById = new Map(runtime.rig.bones.map((definition, index) => [definition.id, bones[index]]));
  runtime.rig.bones.forEach((definition, index) => {
    const bone = bones[index];
    const head = pcgPointToThree(definition.head);
    if (definition.parent) {
      const parentIndex = runtime.rig.bones.findIndex((candidate) => candidate.id === definition.parent);
      bone.position.copy(head.sub(pcgPointToThree(runtime.rig.bones[parentIndex].head)));
      bones[parentIndex].add(bone);
    } else {
      bone.position.copy(head);
      root.add(bone);
    }
  });
  root.updateMatrixWorld(true);
  const skeleton = new THREE.Skeleton(bones);
  skeleton.calculateInverses();

  const componentPivots: Record<string, THREE.Group> = {};
  runtime.rig.components.forEach((component, index) => {
    const pivot = new THREE.Group();
    pivot.name = safeNodeName('PCG_Component', component.id, index);
    pivot.userData.kind = 'component';
    pivot.userData.componentId = component.id;
    pivot.userData.detachable = component.detachable;
    pivot.userData.pivot = component.pivot ?? null;
    componentPivots[component.id] = pivot;
  });
  runtime.rig.components.forEach((component) => {
    const pivot = componentPivots[component.id];
    const parent = component.parent ? componentPivots[component.parent] : null;
    (parent ?? root).add(pivot);
  });
  const componentVisuals: Record<string, THREE.SkinnedMesh> = {};
  for (const entry of componentSets) {
    const pivot = componentPivots[entry.component.id];
    const geometry = new THREE.BufferGeometry();
    setCompactMeshAttributes(geometry, mesh, positions, binding, entry.indices);
    for (const group of entry.materialGroups) {
      geometry.addGroup(group.start, group.count, group.materialIndex);
    }
    const skinnedMesh = new THREE.SkinnedMesh(
      geometry,
      defaultSkinnedMaterial(mesh),
    );
    skinnedMesh.name = `${pivot.name}_Visual`;
    skinnedMesh.userData.kind = 'mesh';
    skinnedMesh.userData.componentId = entry.component.id;
    skinnedMesh.userData.sourceTriangles = entry.sourceTriangles;
    skinnedMesh.userData.materialSlots = [...mesh.materialSlots];
    skinnedMesh.frustumCulled = false;
    // Vertex buffers are already baked in root/model space. Keep every
    // SkinnedMesh under the rig root so a logical component Pivot can never
    // apply a second transform on top of Skeleton deformation.
    root.add(skinnedMesh);
    skinnedMesh.position.set(0, 0, 0);
    skinnedMesh.quaternion.identity();
    skinnedMesh.scale.set(1, 1, 1);
    skinnedMesh.updateMatrixWorld(true);
    skinnedMesh.bind(skeleton, new THREE.Matrix4());
    skinnedMesh.normalizeSkinWeights();
    componentVisuals[entry.component.id] = skinnedMesh;
  }
  if (!useSplitVisuals) {
    const geometry = new THREE.BufferGeometry();
    setFullMeshAttributes(geometry, mesh, positions, binding);
    const skinnedMesh = new THREE.SkinnedMesh(geometry, defaultSkinnedMaterial(mesh));
    skinnedMesh.name = 'PCG_Combined_Animated_Visual';
    skinnedMesh.userData.kind = 'mesh';
    skinnedMesh.userData.componentIds = runtime.rig.components.map((component) => component.id);
    skinnedMesh.userData.materialSlots = [...mesh.materialSlots];
    skinnedMesh.userData.denseSharedVisual = true;
    skinnedMesh.frustumCulled = false;
    root.add(skinnedMesh);
    skinnedMesh.updateMatrixWorld(true);
    skinnedMesh.bind(skeleton, new THREE.Matrix4());
    skinnedMesh.normalizeSkinWeights();
  }

  const socketNodes: Record<string, THREE.Object3D> = {};
  for (const socket of runtime.rig.sockets) {
    const node = new THREE.Object3D();
    node.name = safeNodeName('PCG_Socket', socket.id, Object.keys(socketNodes).length);
    node.userData.kind = 'socket';
    node.userData.socketId = socket.id;
    if (socket.position) node.position.copy(pcgPointToThree(socket.position));
    if (socket.rotation) {
      node.quaternion.set(-socket.rotation[0], -socket.rotation[1], socket.rotation[2], socket.rotation[3]);
    }
    boneById.get(socket.bone)?.add(node);
    socketNodes[socket.id] = node;
  }

  const clips = buildAnimationClips(runtime, boneNames);
  const clipDefinitions = new Map(runtime.rig.clips.map((clip) => [clip.name, clip]));
  const clipLoops = new Map(runtime.rig.clips.map((clip) => [clip.name, clip.loop === true]));
  const clipLoopSources = new Map<string, ActionLoopSource>(runtime.rig.clips.map((clip) => [
    clip.name,
    clip.loop === null ? 'unmeasured' as const : 'authored' as const,
  ]));
  const mixer = new THREE.AnimationMixer(root);
  const restPose = bones.map((bone) => ({
    position: bone.position.clone(),
    quaternion: bone.quaternion.clone(),
    scale: bone.scale.clone(),
  }));
  let currentAnimation: string | null = null;
  let playbackSpeed = runtime.playbackSpeed;
  let activeAction: THREE.AnimationAction | null = null;
  const restoreRestPose = () => {
    bones.forEach((bone, index) => {
      bone.position.copy(restPose[index].position);
      bone.quaternion.copy(restPose[index].quaternion);
      bone.scale.copy(restPose[index].scale);
    });
    root.updateMatrixWorld(true);
  };
  const play = (name: string): boolean => {
    const clip = THREE.AnimationClip.findByName(clips, name);
    const definition = clipDefinitions.get(name);
    if (!clip || !definition) return false;
    mixer.stopAllAction();
    restoreRestPose();
    activeAction = mixer.clipAction(clip).reset();
    activeAction.enabled = true;
    activeAction.paused = false;
    const loop = clipLoops.get(name) ?? definition.loop === true;
    activeAction.clampWhenFinished = !loop;
    activeAction.setLoop(loop ? THREE.LoopRepeat : THREE.LoopOnce, loop ? Infinity : 1);
    if (playbackSpeed < 0) activeAction.time = clip.duration;
    activeAction.play();
    mixer.timeScale = playbackSpeed;
    currentAnimation = name;
    return true;
  };
  const stop = () => {
    mixer.stopAllAction();
    activeAction = null;
    currentAnimation = null;
    restoreRestPose();
  };
  const componentTriangleCounts = new Map(
    componentSets.map((entry) => [entry.component.id, entry.indices.length / 3]),
  );
  const controller: ActionRuntimeController = {
    animationNames: clips.map((clip) => clip.name),
    get clips() {
      return clips.map((clip) => ({
        name: clip.name,
        duration: clip.duration,
        loop: clipLoops.get(clip.name) ?? false,
        loopSource: clipLoopSources.get(clip.name) ?? 'unmeasured',
      }));
    },
    bones: runtime.rig.bones.map((definition, index) => {
      const tailOffset = pcgPointToThree(definition.tail).sub(pcgPointToThree(definition.head));
      return {
        id: definition.id,
        name: bones[index].name,
        parent: definition.parent,
        tailOffset: [tailOffset.x, tailOffset.y, tailOffset.z],
        radius: definition.radius,
      };
    }),
    get components() {
      return runtime.rig.components.map((component) => ({
        id: component.id,
        parent: component.parent,
        bone: component.bone,
        role: component.role,
        skin: component.skin,
        detachable: component.detachable,
        visible: componentVisuals[component.id]?.visible ?? true,
        triangleCount: componentTriangleCounts.get(component.id) ?? 0,
      }));
    },
    componentNames: Object.keys(componentPivots),
    splitComponents: useSplitVisuals,
    get currentAnimation() { return currentAnimation; },
    play,
    pause: () => {
      if (activeAction) activeAction.paused = true;
    },
    resume: () => {
      if (!activeAction || !currentAnimation) return false;
      const clip = THREE.AnimationClip.findByName(clips, currentAnimation);
      if (!clip) return false;
      if (
        activeAction.time >= clip.duration - 1e-6 ||
        (playbackSpeed < 0 && activeAction.time <= 1e-6)
      ) return play(currentAnimation);
      activeAction.paused = false;
      activeAction.enabled = true;
      activeAction.play();
      return true;
    },
    stop,
    seek: (name, timeSeconds) => {
      if (currentAnimation !== name || !activeAction) {
        if (!play(name)) return false;
      }
      const clip = THREE.AnimationClip.findByName(clips, name)!;
      activeAction!.paused = true;
      activeAction!.time = THREE.MathUtils.clamp(timeSeconds, 0, clip.duration);
      mixer.update(0);
      root.updateMatrixWorld(true);
      return true;
    },
    advance: (deltaSeconds) => {
      if (Number.isFinite(deltaSeconds) && deltaSeconds > 0) mixer.update(deltaSeconds);
    },
    setPlaybackSpeed: (speed) => {
      if (!Number.isFinite(speed)) return;
      playbackSpeed = speed;
      mixer.timeScale = speed;
    },
    setLoop: (loop) => {
      if (!activeAction || !currentAnimation) return;
      clipLoops.set(currentAnimation, loop);
      clipLoopSources.set(currentAnimation, 'previewOverride');
      activeAction.clampWhenFinished = !loop;
      activeAction.setLoop(loop ? THREE.LoopRepeat : THREE.LoopOnce, loop ? Infinity : 1);
    },
    getPlaybackState: () => {
      const clip = currentAnimation ? THREE.AnimationClip.findByName(clips, currentAnimation) : null;
      return {
        clipName: currentAnimation,
        currentTime: activeAction?.time ?? 0,
        duration: clip?.duration ?? 0,
        playing: activeAction?.isRunning() ?? false,
        paused: activeAction?.paused ?? false,
        loop: currentAnimation ? (clipLoops.get(currentAnimation) ?? false) : false,
        loopSource: currentAnimation
          ? (clipLoopSources.get(currentAnimation) ?? 'unmeasured')
          : 'unmeasured',
        speed: playbackSpeed,
      };
    },
    getBoneObject: (id) => boneById.get(id) ?? null,
    resetPose: stop,
    setComponentVisible: (id, visible) => {
      const visual = componentVisuals[id];
      if (!visual) return false;
      visual.visible = visible;
      return true;
    },
    setExplode: (amount) => {
      const safeAmount = Number.isFinite(amount) ? amount : 0;
      const center = runtime.rig.bones.reduce(
        (sum, bone) => sum.add(pcgPointToThree(bone.head)), new THREE.Vector3(),
      ).multiplyScalar(1 / runtime.rig.bones.length);
      runtime.rig.components.forEach((component, index) => {
        const pivot = componentPivots[component.id];
        if (!pivot) return;
        if (!component.detachable || safeAmount === 0) {
          pivot.position.set(0, 0, 0);
          componentVisuals[component.id]?.position.set(0, 0, 0);
          if (!useSplitVisuals) {
            const boneIndex = runtime.rig.bones.findIndex((bone) => bone.id === component.bone);
            if (boneIndex >= 0) bones[boneIndex].position.copy(restPose[boneIndex].position);
          }
          return;
        }
        const origin = component.pivot
          ? pcgPointToThree(component.pivot)
          : pcgPointToThree(runtime.rig.bones.find((bone) => bone.id === component.bone)!.head);
        const direction = origin.sub(center);
        if (direction.lengthSq() < 1e-8) {
          direction.set(Math.cos(index * 2.399), 0.35, Math.sin(index * 2.399));
        }
        const offset = direction.normalize().multiplyScalar(safeAmount);
        if (useSplitVisuals) {
          pivot.position.copy(offset);
          componentVisuals[component.id]?.position.copy(offset);
        } else {
          const boneIndex = runtime.rig.bones.findIndex((bone) => bone.id === component.bone);
          if (boneIndex >= 0) bones[boneIndex].position.copy(restPose[boneIndex].position).add(offset);
        }
      });
    },
    inspect: () => {
      root.updateMatrixWorld(true);
      let visibleMeshCount = 0;
      let visibleSkinnedMeshCount = 0;
      let sampledVertexCount = 0;
      let nonFiniteSampleCount = 0;
      let maxSkinIndex = 0;
      let maxWeightError = 0;
      let sampledRestVertexMaxDelta = 0;
      const source = new THREE.Vector3();
      const deformed = new THREE.Vector3();
      let maxBoneScaleDelta = 0;
      let maxRestTransformDelta = 0;
      bones.forEach((bone, index) => {
        maxRestTransformDelta = Math.max(
          maxRestTransformDelta,
          bone.position.distanceTo(restPose[index].position),
          1 - Math.abs(bone.quaternion.dot(restPose[index].quaternion)),
          bone.scale.distanceTo(restPose[index].scale),
        );
        maxBoneScaleDelta = Math.max(maxBoneScaleDelta, bone.scale.distanceTo(restPose[index].scale));
      });
      const atRestPose = maxRestTransformDelta <= 1e-10;
      root.traverse((child) => {
        if (child.userData.kind !== 'mesh' || !child.visible || !(child instanceof THREE.Mesh)) return;
        visibleMeshCount++;
        if (!(child instanceof THREE.SkinnedMesh)) return;
        visibleSkinnedMeshCount++;
        const skinIndex = child.geometry.getAttribute('skinIndex');
        const skinWeight = child.geometry.getAttribute('skinWeight');
        const position = child.geometry.getAttribute('position');
        for (let vertex = 0; vertex < skinWeight.count; vertex++) {
          let sum = 0;
          for (let slot = 0; slot < 4; slot++) {
            const weight = skinWeight.getComponent(vertex, slot);
            const joint = skinIndex.getComponent(vertex, slot);
            sum += weight;
            if (weight > 0) maxSkinIndex = Math.max(maxSkinIndex, joint);
          }
          maxWeightError = Math.max(maxWeightError, Math.abs(1 - sum));
        }
        const step = Math.max(1, Math.floor(position.count / 256));
        for (let vertex = 0; vertex < position.count; vertex += step) {
          source.fromBufferAttribute(position, vertex);
          child.getVertexPosition(vertex, deformed);
          sampledVertexCount++;
          if (![deformed.x, deformed.y, deformed.z].every(Number.isFinite)) {
            nonFiniteSampleCount++;
          } else if (atRestPose) {
            sampledRestVertexMaxDelta = Math.max(
              sampledRestVertexMaxDelta,
              deformed.distanceTo(source),
            );
          }
        }
      });
      return {
        bindingMethod: binding.method,
        unreachableVertexCount: binding.unreachableVertexCount,
        rigidVertexCount: binding.rigidVertexCount,
        visibleMeshCount,
        visibleSkinnedMeshCount,
        allVisibleMeshesBound: visibleMeshCount === visibleSkinnedMeshCount,
        sampledVertexCount,
        nonFiniteSampleCount,
        maxSkinIndex,
        maxWeightError,
        atRestPose,
        sampledRestVertexMaxDelta: atRestPose ? sampledRestVertexMaxDelta : null,
        maxBoneScaleDelta,
      };
    },
    dispose: () => {
      mixer.stopAllAction();
      mixer.uncacheRoot(root);
    },
  };

  root.userData.sculptRuntime = {
    nodes: {
      ...Object.fromEntries(boneById),
      ...componentPivots,
      ...socketNodes,
    },
    components: componentPivots,
    visuals: componentVisuals,
    sockets: socketNodes,
    colliders: runtime.rig.colliders,
    destructionGroups: runtime.rig.destructionGroups,
  };
  root.userData.rig = {
    bones,
    skeleton,
    boneOrder: runtime.rig.bones.map((bone) => bone.id),
    boneMap: Object.fromEntries(runtime.rig.bones.map((bone, index) => [bone.id, bones[index]])),
    bound: true,
    splitVisuals: useSplitVisuals,
    binding: {
      method: binding.method,
      unreachableVertexCount: binding.unreachableVertexCount,
      voxelStep: binding.voxelStep ?? null,
      rigidVertexCount: binding.rigidVertexCount,
      sourceRoute: runtime.rig.sourceRoute,
    },
  };
  root.userData.actionController = controller;
  root.animations = clips;
  if (runtime.autoplay) play(runtime.autoplay);
  return { root, skeleton, bones, componentPivots, controller, binding };
}
