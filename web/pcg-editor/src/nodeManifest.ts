// node-manifest.ts — Manifest-driven node definitions for PCG Graph editors.
// Source of truth: schema/node-manifest.json (shared with Unity GraphView).

import manifestJson from '../../../schema/node-manifest.json';

// ── Types ──────────────────────────────────────────────

export type PinType = 'SpatialPoint' | 'SpatialSpline' | 'SpatialSurface' | 'SpatialMesh' |
  'SpatialGeometry' | 'Texture' | 'HeightField' | 'Material' | 'Param' | 'Any';
export type PropertyType =
  | 'integer'
  | 'number'
  | 'boolean'
  | 'string'
  | 'enum'
  | 'groupSelect'
  | 'groupMultiSelect'
  | 'vector3'
  | 'texture2d';
export type GroupDomain = 'edge' | 'face' | 'point' | 'vertex';

export interface ManifestEnumOption {
  value: string;
  label: string;
}

export interface ManifestProperty {
  type: PropertyType;
  default: number | boolean | string | [number, number, number];
  /** Human-readable label (Houdini parm label); falls back to the property key */
  displayName?: string;
  minimum?: number;
  maximum?: number;
  options?: ManifestEnumOption[];
  /** Control flavor hint, e.g. "radio" renders an enum as Houdini-style tab buttons */
  uiHint?: string;
  /** Conditional visibility: show only when another property equals a value */
  visibleWhen?: {
    property?: string;
    equals?: number | boolean | string;
    oneOf?: Array<number | boolean | string>;
  };
  /** Boolean toggle companion: render the referenced property inline on the toggle row */
  companionField?: string;
  /** For groupSelect/groupMultiSelect: filter available groups by domain */
  groupDomain?: GroupDomain;
  /** True for outputGroup-style properties that define a new group name */
  isGroupOutput?: boolean;
}

export interface ManifestPin {
  id: string;
  label: string;
  pinType: PinType;
  variadic?: boolean;
}

export interface ManifestOutputGroup {
  name: string;
  domain: GroupDomain;
  label: string;
  /** Property key whose value determines if this group is produced (e.g. "capStart") */
  condition?: string;
  /** If true, the group name comes from the property named by `name` (dynamic, not static) */
  dynamic?: boolean;
}

export interface ManifestNodeDef {
  type: string;
  displayName: string;
  category: string;
  /** Whether the node can be cooked as an isolated viewport/scene preview target. */
  supportsPreview?: boolean;
  inputs: ManifestPin[];
  outputs: ManifestPin[];
  properties: Record<string, ManifestProperty>;
  /** Groups this node produces on its output */
  outputGroups?: ManifestOutputGroup[];
}

export interface NodeManifest {
  version: string;
  description: string;
  nodes: ManifestNodeDef[];
}

// ── Parse ──────────────────────────────────────────────

const KNOWN_PROPERTY_TYPES: ReadonlySet<string> = new Set([
  'integer',
  'number',
  'boolean',
  'string',
  'enum',
  'groupSelect',
  'groupMultiSelect',
  'vector3',
  'texture2d',
]);

function assertManifestContract(raw: typeof manifestJson): NodeManifest {
  for (const node of raw.nodes) {
    for (const [key, prop] of Object.entries(node.properties ?? {})) {
      const propType = (prop as { type?: string }).type;
      if (!propType || !KNOWN_PROPERTY_TYPES.has(propType)) {
        throw new Error(
          `node-manifest contract drift: ${node.type}.${key} has unsupported type '${propType}'`,
        );
      }
    }
  }
  return raw as NodeManifest;
}

const manifest = assertManifestContract(manifestJson);

const nodeMap: Map<string, ManifestNodeDef> = new Map();
for (const def of manifest.nodes) {
  nodeMap.set(def.type, def);
}

// ── Public API ──────────────────────────────────────────

export function getNodeManifest(): NodeManifest {
  return manifest;
}

export function getNodeTypeDefs(type: string): ManifestNodeDef | undefined {
  return nodeMap.get(type);
}

export function getAllNodeTypes(): ManifestNodeDef[] {
  return manifest.nodes;
}

export function getAllCategories(): string[] {
  const categories = new Set<string>();
  for (const def of manifest.nodes) {
    categories.add(def.category);
  }
  return Array.from(categories);
}

/** Validates external Agent writes against the same manifest used by Inspector. */
export function validateNodePropertyValue(
  nodeType: string,
  key: string,
  value: unknown,
): string | null {
  const property = nodeMap.get(nodeType)?.properties[key];
  if (!property) return `unknown property "${nodeType}.${key}"`;
  const isFiniteNumber = typeof value === 'number' && Number.isFinite(value);
  if (property.type === 'integer' && (!isFiniteNumber || !Number.isInteger(value))) {
    return `${nodeType}.${key} requires an integer`;
  }
  if (property.type === 'number' && !isFiniteNumber) return `${nodeType}.${key} requires a number`;
  if (property.type === 'boolean' && typeof value !== 'boolean') return `${nodeType}.${key} requires a boolean`;
  if (
    ['string', 'enum', 'groupSelect', 'groupMultiSelect', 'texture2d'].includes(property.type) &&
    typeof value !== 'string'
  ) {
    return `${nodeType}.${key} requires a string`;
  }
  if (
    property.type === 'vector3' &&
    (!Array.isArray(value) || value.length !== 3 || !value.every((item) => typeof item === 'number' && Number.isFinite(item)))
  ) {
    return `${nodeType}.${key} requires a three-number vector`;
  }
  if (isFiniteNumber && property.minimum !== undefined && value < property.minimum) {
    return `${nodeType}.${key} must be at least ${property.minimum}`;
  }
  if (isFiniteNumber && property.maximum !== undefined && value > property.maximum) {
    return `${nodeType}.${key} must be at most ${property.maximum}`;
  }
  if (property.options && !property.options.some((option) => option.value === value)) {
    return `${nodeType}.${key} is not a supported option`;
  }
  return null;
}

export function getNodesByCategory(): Map<string, ManifestNodeDef[]> {
  const map = new Map<string, ManifestNodeDef[]>();
  for (const def of manifest.nodes) {
    const list = map.get(def.category);
    if (list) {
      list.push(def);
    } else {
      map.set(def.category, [def]);
    }
  }
  return map;
}

/** Returns the pinType of a node's output port, or undefined if not found. */
export function getOutputPinType(nodeType: string, handle: string): PinType | undefined {
  const def = nodeMap.get(nodeType);
  if (!def) return undefined;
  const pin = def.outputs.find((p) => p.id === handle);
  return pin?.pinType;
}

/** Returns the pinType of a node's input port, or undefined if not found. */
export function getInputPinType(nodeType: string, handle: string): PinType | undefined {
  const def = nodeMap.get(nodeType);
  if (!def) return undefined;
  const pin = def.inputs.find((p) => p.id === handle);
  return pin?.pinType;
}

function isSpatialGeometryFamily(pinType: string): boolean {
  return pinType === 'SpatialGeometry' || pinType === 'SpatialMesh' || pinType === 'SpatialSpline';
}

/**
 * Checks if two pins can connect based on pinType compatibility.
 * "Any" matches all pinTypes.
 */
export function pinTypesCompatible(sourcePin: PinType | undefined, targetPin: PinType | undefined): boolean {
  if (!sourcePin || !targetPin) return false;
  if (sourcePin === targetPin || sourcePin === 'Any' || targetPin === 'Any') return true;
  if (sourcePin === 'SpatialGeometry' && isSpatialGeometryFamily(targetPin)) return true;
  if (targetPin === 'SpatialGeometry' && isSpatialGeometryFamily(sourcePin)) return true;
  return false;
}

/**
 * Validates whether a connection from source port to target port is allowed.
 * Uses manifest pinType matching with "Any" wildcard.
 */
export function canConnect(
  sourceNodeType: string,
  sourceHandle: string,
  targetType: string,
  targetHandle: string,
): boolean {
  const sourcePinType = getOutputPinType(sourceNodeType, sourceHandle);
  const targetPinType = getInputPinType(targetType, targetHandle);
  return pinTypesCompatible(sourcePinType, targetPinType);
}

/** True if nodeType has at least one input pin compatible with the given pinType. */
export function hasCompatibleInputPin(nodeType: string, pinType: PinType): boolean {
  const def = nodeMap.get(nodeType);
  if (!def) return false;
  return def.inputs.some((pin) => pinTypesCompatible(pinType, pin.pinType));
}

/** True if nodeType has at least one output pin compatible with the given pinType. */
export function hasCompatibleOutputPin(nodeType: string, pinType: PinType): boolean {
  const def = nodeMap.get(nodeType);
  if (!def) return false;
  return def.outputs.some((pin) => pinTypesCompatible(pinType, pin.pinType));
}

/** Creates a default data object for a node type from manifest property defaults. */
export function defaultDataFor(type: string): Record<string, unknown> {
  const def = nodeMap.get(type);
  if (!def) return {};
  const data: Record<string, unknown> = {};
  for (const [key, prop] of Object.entries(def.properties)) {
    data[key] = prop.default;
  }
  return data;
}

/** Category → CSS color mapping (aligned with Unity GraphView node colors). */
export const CATEGORY_COLORS: Record<string, string> = {
  Generation: '#f77f00',
  Spawner: '#06a77d',
  Metadata: '#7b2cbf',
  Filter: '#0a9396',
  Transform: '#4361ee',
  Sampler: '#9c6644',
  Structural: '#5a189a',
  Mesh: '#e07ba0',
  Spline: '#1d7874',
  Geometry: '#d4a373',
  Output: '#6c757d',
};

export function getCategoryColor(category: string): string {
  return CATEGORY_COLORS[category] ?? '#495057';
}

/** Pin type → CSS color (Houdini-style port coloring, aligned with Unity). */
export const PIN_TYPE_COLORS: Record<string, string> = {
  SpatialPoint: '#00ccff',
  SpatialSpline: '#4de66a',
  SpatialMesh: '#ff9900',
  SpatialGeometry: '#ffaa33',
  Param: '#ffd700',
  Texture: '#b34dd9',
  Material: '#e85d9e',
  Any: '#a6a6a6',
};

export function getPinTypeColor(pinType: string | undefined): string {
  return (pinType && PIN_TYPE_COLORS[pinType]) ?? '#a6a6a6';
}
