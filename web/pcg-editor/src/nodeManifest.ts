// node-manifest.ts — Manifest-driven node definitions for PCG Graph editors.
// Source of truth: schema/node-manifest.json (shared with Unity GraphView).

import manifestJson from '../../../schema/node-manifest.json';

// ── Types ──────────────────────────────────────────────

export type PinType = 'SpatialPoint' | 'SpatialSpline' | 'SpatialMesh' | 'Param' | 'Any';
export type PropertyType = 'integer' | 'number' | 'boolean' | 'string' | 'enum';

export interface ManifestEnumOption {
  value: string;
  label: string;
}

export interface ManifestProperty {
  type: PropertyType;
  default: number | boolean | string;
  minimum?: number;
  maximum?: number;
  options?: ManifestEnumOption[];
}

export interface ManifestPin {
  id: string;
  label: string;
  pinType: PinType;
}

export interface ManifestNodeDef {
  type: string;
  displayName: string;
  category: string;
  inputs: ManifestPin[];
  outputs: ManifestPin[];
  properties: Record<string, ManifestProperty>;
}

export interface NodeManifest {
  version: string;
  description: string;
  nodes: ManifestNodeDef[];
}

// ── Parse ──────────────────────────────────────────────

const manifest = manifestJson as unknown as NodeManifest;

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

/**
 * Checks if two pins can connect based on pinType compatibility.
 * "Any" matches all pinTypes.
 */
export function pinTypesCompatible(sourcePin: PinType | undefined, targetPin: PinType | undefined): boolean {
  if (!sourcePin || !targetPin) return false;
  return sourcePin === targetPin || sourcePin === 'Any' || targetPin === 'Any';
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
  Output: '#6c757d',
};

export function getCategoryColor(category: string): string {
  return CATEGORY_COLORS[category] ?? '#495057';
}
