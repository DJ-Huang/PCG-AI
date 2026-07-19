// importGraph.ts — Parse Graph JSON v1/v2 into React Flow state without dropping subgraphs.
// Node types validated against node-manifest.json (no hardcoded type set).

import type { Node, Edge } from '@xyflow/react';
import type {
  GraphNode,
  GraphEdge,
  NodeData,
  GraphParameter,
  GraphSubgraph,
  GraphSubgraphPort,
} from './graphSchema';
import { defaultData } from './graphSchema';
import { getNodeTypeDefs } from './nodeManifest';

const PIN_TYPES = new Set([
  'Any', 'Param', 'SpatialPoint', 'SpatialSpline', 'SpatialSurface',
  'SpatialMesh', 'Texture', 'HeightField',
]);

function canonicalPinType(value: unknown): string | undefined {
  if (value === 'Mesh') return 'SpatialMesh'; // Legacy subgraph alias.
  return typeof value === 'string' && PIN_TYPES.has(value) ? value : undefined;
}

export type ImportResult =
  | {
      ok: true;
      nodes: Node[];
      edges: Edge[];
      parameters: GraphParameter[];
      subgraphs: GraphSubgraph[];
      filename?: string;
    }
  | { ok: false; error: string };

/**
 * Parses Graph JSON v1/v2 into React Flow nodes/edges plus document-level data.
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
  if (root.version !== '1.0' && root.version !== '2.0') {
    return { ok: false, error: 'Unsupported or missing version (expected 1.0 or 2.0).' };
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

  // Subgraph definitions are preserved even though this editor does not yet
  // expose an authoring UI for their nested contents.
  const subgraphs: GraphSubgraph[] = [];
  const subgraphIds = new Set<string>();
  if (Array.isArray(root.subgraphs)) {
    for (let i = 0; i < root.subgraphs.length; i++) {
      const subgraphResult = toGraphSubgraph(root.subgraphs[i], i);
      if (!subgraphResult.ok) return subgraphResult;
      if (subgraphIds.has(subgraphResult.subgraph.id)) {
        return {
          ok: false,
          error: `Duplicate subgraph id "${subgraphResult.subgraph.id}".`,
        };
      }
      subgraphIds.add(subgraphResult.subgraph.id);
      subgraphs.push(subgraphResult.subgraph);
    }
  }

  return { ok: true, nodes, edges, parameters, subgraphs };
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
type SubgraphParse = { ok: true; subgraph: GraphSubgraph } | { ok: false; error: string };

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

  // Subgraph interface nodes are structural parser primitives and therefore
  // intentionally absent from the executable-node manifest.
  const isStructuralSubgraphNode =
    type === 'Subgraph' || type === 'SubgraphInput' || type === 'SubgraphOutput';
  if (!getNodeTypeDefs(type) && !isStructuralSubgraphNode) {
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
  const sourcePinType = canonicalPinType(raw.sourcePinType);
  const targetPinType = canonicalPinType(raw.targetPinType);
  if (raw.sourcePinType !== undefined && !sourcePinType) {
    return { ok: false, error: `Edge "${id}" has invalid "sourcePinType".` };
  }
  if (raw.targetPinType !== undefined && !targetPinType) {
    return { ok: false, error: `Edge "${id}" has invalid "targetPinType".` };
  }

  return {
    ok: true,
    edge: {
      id,
      source,
      target,
      sourceHandle: typeof raw.sourceHandle === 'string' ? raw.sourceHandle : 'out',
      targetHandle: typeof raw.targetHandle === 'string' ? raw.targetHandle : 'in',
      sourcePinType,
      targetPinType,
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

function toGraphSubgraph(item: unknown, index: number): SubgraphParse {
  if (!item || typeof item !== 'object' || Array.isArray(item)) {
    return { ok: false, error: `Subgraph at index ${index} is not an object.` };
  }
  const raw = item as Record<string, unknown>;
  const id = typeof raw.id === 'string' ? raw.id : '';
  const name = typeof raw.name === 'string' ? raw.name : '';
  if (!id) return { ok: false, error: `Subgraph at index ${index} is missing "id".` };
  if (!name) return { ok: false, error: `Subgraph "${id}" is missing "name".` };
  if (!Array.isArray(raw.inputs) || !Array.isArray(raw.outputs) ||
      !Array.isArray(raw.nodes) || !Array.isArray(raw.edges)) {
    return { ok: false, error: `Subgraph "${id}" has invalid ports, nodes, or edges.` };
  }

  const parsePorts = (
    values: unknown[],
    direction: 'input' | 'output',
  ): { ok: true; ports: GraphSubgraphPort[] } | { ok: false; error: string } => {
    const ports: GraphSubgraphPort[] = [];
    const ids = new Set<string>();
    for (let portIndex = 0; portIndex < values.length; portIndex++) {
      const value = values[portIndex];
      if (!value || typeof value !== 'object' || Array.isArray(value)) {
        return { ok: false, error: `Subgraph "${id}" ${direction} port ${portIndex} is invalid.` };
      }
      const port = value as Record<string, unknown>;
      const portId = typeof port.id === 'string' ? port.id : '';
      const portName = typeof port.name === 'string' ? port.name : '';
      const pinType = canonicalPinType(port.pinType) ?? '';
      if (!portId || !portName || !pinType || ids.has(portId)) {
        return { ok: false, error: `Subgraph "${id}" has an invalid or duplicate ${direction} port.` };
      }
      ids.add(portId);
      ports.push({ id: portId, name: portName, pinType });
    }
    return { ok: true, ports };
  };

  const inputs = parsePorts(raw.inputs, 'input');
  if (!inputs.ok) return inputs;
  const outputs = parsePorts(raw.outputs, 'output');
  if (!outputs.ok) return outputs;

  const nodes: GraphNode[] = [];
  const nodeIds = new Set<string>();
  for (let nodeIndex = 0; nodeIndex < raw.nodes.length; nodeIndex++) {
    const node = toGraphNode(raw.nodes[nodeIndex], nodeIndex);
    if (!node.ok) return { ok: false, error: `Subgraph "${id}": ${node.error}` };
    if (nodeIds.has(node.node.id)) {
      return { ok: false, error: `Subgraph "${id}" has duplicate node id "${node.node.id}".` };
    }
    nodeIds.add(node.node.id);
    nodes.push(node.node);
  }

  const edges: GraphEdge[] = [];
  const edgeIds = new Set<string>();
  for (let edgeIndex = 0; edgeIndex < raw.edges.length; edgeIndex++) {
    const edge = toGraphEdge(raw.edges[edgeIndex], edgeIndex);
    if (!edge.ok) return { ok: false, error: `Subgraph "${id}": ${edge.error}` };
    if (edgeIds.has(edge.edge.id) || !nodeIds.has(edge.edge.source) ||
        !nodeIds.has(edge.edge.target)) {
      return { ok: false, error: `Subgraph "${id}" has a duplicate or dangling edge.` };
    }
    edgeIds.add(edge.edge.id);
    edges.push(edge.edge);
  }

  return {
    ok: true,
    subgraph: { id, name, inputs: inputs.ports, outputs: outputs.ports, nodes, edges },
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
    data: {
      sourcePinType: graphEdge.sourcePinType,
      targetPinType: graphEdge.targetPinType,
    },
  };
}
