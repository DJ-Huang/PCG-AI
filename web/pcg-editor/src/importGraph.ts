import type { Node, Edge } from '@xyflow/react';
import type { GraphNode, GraphEdge, NodeType, NodeData } from './graphSchema';
import { defaultData } from './graphSchema';

const NODE_TYPES = new Set<NodeType>(['SpawnPoints', 'PlaceInScene']);

export type ImportResult =
  | { ok: true; nodes: Node[]; edges: Edge[]; filename?: string }
  | { ok: false; error: string };

/**
 * Parses Graph JSON v1 text into React Flow nodes/edges.
 * Validation mirrors Unity PcgGraphSerializer.TryFromJson + schema/graph-schema.json.
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

  return { ok: true, nodes, edges };
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
  if (typeof type !== 'string' || !NODE_TYPES.has(type as NodeType)) {
    return { ok: false, error: `Node "${id}" has unsupported type "${String(type)}".` };
  }

  const nodeType = type as NodeType;
  const position = parsePosition(raw.position, id);
  if (!position.ok) return position;

  const data = mergeNodeData(nodeType, raw.data);

  return {
    ok: true,
    node: {
      id,
      type: nodeType,
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

function mergeNodeData(type: NodeType, value: unknown): NodeData {
  const defaults = defaultData[type];
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
