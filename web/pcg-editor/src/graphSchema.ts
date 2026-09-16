// Graph JSON v1 types — shared contract between Web editor, C++ core, and Unity plugin.
// Node types are manifest-driven (schema/node-manifest.json); this file defines
// the wire format and parameter system types.

import { defaultDataFor } from './nodeManifest';

export type NodeType = string;
export type NodeData = Record<string, unknown>;

/** Story/layout metadata in Unity world space (+Y up), separate from cook inputs. */
export interface SemanticBounds {
  center: [number, number, number];
  size: [number, number, number];
}

export interface SemanticComponent {
  /** Stable agent query key, e.g. "doorway_opening". Same ids aggregate. */
  componentId: string;
  label?: string;
  role?: string;
  zone?: string;
  intent?: string;
  /** Stable shared structure knowledge id, not scene-instance prose. */
  recipeId?: string;
  /** Nodes in this scope that form the component, including the semantic owner. */
  memberNodeIds?: string[];
  /** Explicit AABB; required for negative space and recommended for aggregates. */
  bounds?: SemanticBounds;
  anchors?: Record<string, [number, number, number]>;
  camera?: { include?: boolean; occluder?: boolean; scaleRole?: string };
}

export interface Vec2 {
  x: number;
  y: number;
}

export interface GraphNode {
  id: string;
  type: NodeType;
  position: Vec2;
  data: NodeData;
}

export interface GraphEdge {
  id: string;
  source: string;
  target: string;
  sourceHandle?: string;
  targetHandle?: string;
  sourcePinType?: string;
  targetPinType?: string;
}

export interface GraphSubgraphPort {
  id: string;
  name: string;
  pinType: string;
  anchorPlaced?: boolean;
  anchorX?: number;
  anchorY?: number;
}

export interface GraphSubgraph {
  id: string;
  name: string;
  inputs: GraphSubgraphPort[];
  outputs: GraphSubgraphPort[];
  nodes: GraphNode[];
  edges: GraphEdge[];
  parameters?: GraphParameter[];
  /** Semantic aggregate owned by this reusable Subgraph definition. */
  semantic?: SemanticComponent;
}

// ── Parameters ─────────────────────────────────────────

export type ParameterType = 'integer' | 'number' | 'boolean' | 'string' | 'vector3';

export interface GraphParameter {
  id: string;
  name: string;
  type: ParameterType;
  default: number | boolean | string | [number, number, number];
  exposed: boolean;
  targetNode: string;
  targetProperty: string;
  hasRange: boolean;
  min: number;
  max: number;
}

// ── Graph Document ─────────────────────────────────────

export interface GraphJson {
  version: '1.0' | '2.0';
  nodes: GraphNode[];
  edges: GraphEdge[];
  parameters?: GraphParameter[];
  subgraphs?: GraphSubgraph[];
}

// ── Defaults ───────────────────────────────────────────

/** Returns default node data for a type, sourced from node-manifest.json. */
export function defaultData(type: NodeType): NodeData {
  return defaultDataFor(type);
}
