// importGraph.ts — Parse Graph JSON v1 (with parameters) into React Flow nodes/edges.
// Node types validated against node-manifest.json (no hardcoded type set).

import type { Node, Edge } from '@xyflow/react';
import type { GraphNode, GraphEdge, NodeData, GraphParameter } from './graphSchema';
import { defaultData } from './graphSchema';
import { getNodeTypeDefs } from './nodeManifest';

export type ImportResult =
  | { ok: true; nodes: Node[]; edges: Edge[]; parameters: GraphParameter[]; filename?: string }
  | { ok: false; error: string };

/**
 * Parses Graph JSON v1 text into React Flow nodes/edges + parameters.
 */
export function parseGraphJson(text: string): ImportResult {
  let parsed: unknown;
  try {
    parsed = JSON.parse(text);
  } catch {
    return { ok: false, error: 'Invalid JSON.' };
  }

  if (!parsed || typeof parsed !== 'object' || Array.isArray(parsed)) {
    return { ok: false, error: 'Root is not a JSON object.' };
  }

  const root = parsed as Record<string, unknown>;
  if (root.version !== '1.0') {
    return { ok: false, error: 'Unsupported or missing version (expected 1.0).' };
  }

  if (!Array.isArray(root.nodes)) {
    return { ok: false, error: 'Missing or invalid "nodes" array.' };
  }

  if (!Array.isArray(root.edges)) {
    return { ok: false, error: 'Missing or invalid "edges" array.' };
  }

  const nodes: Node[] = [];
  const nodeIds = new Set<string>();

  for (let i = 0; i < root.nodes.length; i++) {
    const item = root.nodes[i];
    const nodeResult = toGraphNode(item, i);
    if (!nodeResult.ok) return nodeResult;
    if (nodeIds.has(nodeResult.node.id)) {
      return { ok: false, error: `Duplicate node id "${nodeResult.node.id}".` };
    }
    nodeIds.add(nodeResult.node.id);
    nodes.push(toFlowNode(nodeResult.node));
  }

  const edges: Edge[] = [];
  const edgeIds = new Set<string>();

  for (let i = 0; i < root.edges.length; i++) {
    const item = root.edges[i];
    const edgeResult = toGraphEdge(item, i);
    if (!edgeResult.ok) return edgeResult;
    if (edgeIds.has(edgeResult.edge.id)) {
      return { ok: false, error: `Duplicate edge id "${edgeResult.edge.id}".` };
    }
    if (!nodeIds.has(edgeResult.edge.source)) {
      return { ok: false, error: `Edge "${edgeResult.edge.id}" references unknown source "${edgeResult.edge.source}".` };
    }
    if (!nodeIds.has(edgeResult.edge.target)) {
      return { ok: false, error: `Edge "${edgeResult.edge.id}" references unknown target "${edgeResult.edge.target}".` };
    }
    edgeIds.add(edgeResult.edge.id);
    edges.push(toFlowEdge(edgeResult.edge));
  }

  // Parse parameters (optional)
  const parameters: GraphParameter[] = [];
  if (Array.isArray(root.parameters)) {
    for (let i = 0; i < root.parameters.length; i++) {
      const paramResult = toGraphParameter(root.parameters[i], i);
      if (!paramResult.ok) return paramResult;
      parameters.push(paramResult.param);
    }
  }

  return { ok: true, nodes, edges, parameters };
}

export function importGraphFromFile(file: File): Promise<ImportResult> {
  return new Promise((resolve) => {
    const reader = new FileReader();
    reader.onload = () => {
      const text = typeof reader.result === 'string' ? reader.result : '';
      const result = parseGraphJson(text);
      resolve(result.ok ? { ...result, filename: file.name } : result);
    };
    reader.onerror = () => resolve({ ok: false, error: 'Failed to read file.' });
    reader.readAsText(file);
  });
}

/** Keeps auto-generated node ids above any numeric suffix in imported ids (e.g. n105). */
export function syncNodeCounterFromNodes(nodes: Node[]): number {
  let max = 100;
  for (const node of nodes) {
    const match = /^n(\d+)$/.exec(node.id);
    if (match) {
      max = Math.max(max, Number.parseInt(match[1], 10));
    }
  }
  return max;
}

type NodeParse = { ok: true; node: GraphNode } | { ok: false; error: string };
type EdgeParse = { ok: true; edge: GraphEdge } | { ok: false; error: string };
type ParamParse = { ok: true; param: GraphParameter } | { ok: false; error: string };

function toGraphNode(item: unknown, index: number): NodeParse {
  if (!item || typeof item !== 'object' || Array.isArray(item)) {
    return { ok: false, error: `Node at index ${index} is not an object.` };
  }

  const raw = item as Record<string, unknown>;
  const id = typeof raw.id === 'string' ? raw.id : '';
  if (!id) {
    return { ok: false, error: `Node at index ${index} is missing "id".` };
  }

  const type = raw.type;
  if (typeof type !== 'string') {
    return { ok: false, error: `Node "${id}" is missing "type".` };
  }

  // Validate against manifest (no hardcoded type set)
  if (!getNodeTypeDefs(type)) {
    return { ok: false, error: `Node "${id}" has unknown type "${type}".` };
  }

  const position = parsePosition(raw.position, id);
  if (!position.ok) return position;

  const data = mergeNodeData(type, raw.data);

  return {
    ok: true,
    node: {
      id,
      type,
      position: position.value,
      data,
    },
  };
}

function parsePosition(
  value: unknown,
  nodeId: string,
): { ok: true; value: { x: number; y: number } } | { ok: false; error: string } {
  if (!value || typeof value !== 'object' || Array.isArray(value)) {
    return { ok: false, error: `Node "${nodeId}" is missing "position".` };
  }
  const pos = value as Record<string, unknown>;
  const x = typeof pos.x === 'number' ? pos.x : 0;
  const y = typeof pos.y === 'number' ? pos.y : 0;
  return { ok: true, value: { x, y } };
}

function mergeNodeData(type: string, value: unknown): NodeData {
  const defaults = defaultData(type);
  if (!value || typeof value !== 'object' || Array.isArray(value)) {
    return { ...defaults };
  }
  return { ...defaults, ...(value as NodeData) };
}

function toGraphEdge(item: unknown, index: number): EdgeParse {
  if (!item || typeof item !== 'object' || Array.isArray(item)) {
    return { ok: false, error: `Edge at index ${index} is not an object.` };
  }

  const raw = item as Record<string, unknown>;
  const id = typeof raw.id === 'string' ? raw.id : '';
  const source = typeof raw.source === 'string' ? raw.source : '';
  const target = typeof raw.target === 'string' ? raw.target : '';

  if (!id) return { ok: false, error: `Edge at index ${index} is missing "id".` };
  if (!source) return { ok: false, error: `Edge "${id}" is missing "source".` };
  if (!target) return { ok: false, error: `Edge "${id}" is missing "target".` };

  return {
    ok: true,
    edge: {
      id,
      source,
      target,
      sourceHandle: typeof raw.sourceHandle === 'string' ? raw.sourceHandle : undefined,
      targetHandle: typeof raw.targetHandle === 'string' ? raw.targetHandle : undefined,
    },
  };
}

function toGraphParameter(item: unknown, index: number): ParamParse {
  if (!item || typeof item !== 'object' || Array.isArray(item)) {
    return { ok: false, error: `Parameter at index ${index} is not an object.` };
  }

  const raw = item as Record<string, unknown>;
  const id = typeof raw.id === 'string' ? raw.id : '';
  if (!id) {
    return { ok: false, error: `Parameter at index ${index} is missing "id".` };
  }

  const name = typeof raw.name === 'string' ? raw.name : '';
  const type = typeof raw.type === 'string' ? (raw.type as GraphParameter['type']) : 'number';
  const exposed = typeof raw.exposed === 'boolean' ? raw.exposed : true;
  const targetNode = typeof raw.targetNode === 'string' ? raw.targetNode : '';
  const targetProperty = typeof raw.targetProperty === 'string' ? raw.targetProperty : '';
  const hasRange = typeof raw.hasRange === 'boolean' ? raw.hasRange : false;
  const min = typeof raw.min === 'number' ? raw.min : 0;
  const max = typeof raw.max === 'number' ? raw.max : 1;

  // Infer default value type
  let defaultValue: number | boolean | string = 0;
  if (type === 'boolean') {
    defaultValue = typeof raw.default === 'boolean' ? raw.default : false;
  } else if (type === 'string') {
    defaultValue = typeof raw.default === 'string' ? raw.default : '';
  } else {
    defaultValue = typeof raw.default === 'number' ? raw.default : 0;
  }

  return {
    ok: true,
    param: { id, name, type, default: defaultValue, exposed, targetNode, targetProperty, hasRange, min, max },
  };
}

function toFlowNode(graphNode: GraphNode): Node {
  return {
    id: graphNode.id,
    type: graphNode.type,
    position: { x: graphNode.position.x, y: graphNode.position.y },
    data: { ...graphNode.data },
  };
}

function toFlowEdge(graphEdge: GraphEdge): Edge {
  return {
    id: graphEdge.id,
    source: graphEdge.source,
    target: graphEdge.target,
    sourceHandle: graphEdge.sourceHandle ?? 'out',
    targetHandle: graphEdge.targetHandle ?? 'in',
  };
}
