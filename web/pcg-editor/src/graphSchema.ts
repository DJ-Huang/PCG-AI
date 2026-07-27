// Graph JSON v1 types — shared contract between Web editor, C++ core, and Unity plugin.
// Node types are manifest-driven (schema/node-manifest.json); this file defines
// the wire format and parameter system types.

import { defaultDataFor } from './nodeManifest';

export type NodeType = string;
export type NodeData = Record<string, unknown>;

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
}

export interface GraphSubgraph {
  id: string;
  name: string;
  inputs: GraphSubgraphPort[];
  outputs: GraphSubgraphPort[];
  nodes: GraphNode[];
  edges: GraphEdge[];
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
