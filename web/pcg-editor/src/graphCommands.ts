import type { Edge, Node } from '@xyflow/react';

import { isValidConnection } from './connectionValidation';
import { defaultData, type GraphEdge, type GraphJson, type GraphNode, type GraphParameter, type GraphSubgraph } from './graphSchema';
import { parseGraphJson, type ImportResult } from './importGraph';
import { getNodeTypeDefs, validateNodePropertyValue } from './nodeManifest';

export type GraphOperation =
  | { op: 'add_node'; node: GraphNode }
  | { op: 'remove_node'; nodeId: string }
  | { op: 'patch_node'; nodeId: string; patch: Record<string, unknown> }
  | { op: 'move_node'; nodeId: string; position: { x: number; y: number } }
  | { op: 'add_edge'; edge: GraphEdge }
  | { op: 'remove_edge'; edgeId: string }
  | { op: 'upsert_parameter'; parameter: GraphParameter }
  | { op: 'remove_parameter'; parameterId: string };

interface QueuedCommandBase {
  id: number;
  baseGraphHash: string;
  editPath: string[];
  createdAt: number;
}

export type QueuedGraphCommand = QueuedCommandBase & (
  | { type: 'setNodeParams'; nodeId: string; patch: Record<string, unknown> }
  | { type: 'applyGraphOps'; operations: GraphOperation[] }
  | { type: 'replaceGraph'; graph: GraphJson }
  | { type: 'saveGraph'; path?: string }
);

export interface GraphCommandResult {
  id: number;
  ok: boolean;
  error?: string;
  detail?: unknown;
}

type GraphScope = Pick<GraphJson, 'nodes' | 'edges' | 'parameters'>;

export type ValidatedGraph = Extract<ImportResult, { ok: true }>;

function cloneGraph(graph: GraphJson): GraphJson {
  return structuredClone(graph);
}

function resolveScope(graph: GraphJson, editPath: string[]): GraphScope | null {
  if (editPath.length === 0) return graph;
  const subgraphId = editPath[editPath.length - 1];
  return graph.subgraphs?.find((subgraph) => subgraph.id === subgraphId) ?? null;
}

function isFinitePosition(position: { x: number; y: number }): boolean {
  return Number.isFinite(position.x) && Number.isFinite(position.y);
}

function validateNodePatch(nodeType: string, patch: Record<string, unknown>): string | null {
  const validateSemantic = (value: unknown): string | null => {
    const parsed = parseGraphJson(JSON.stringify({
      version: '1.0',
      nodes: [{ id: 'semantic_validation', type: nodeType, position: { x: 0, y: 0 }, data: { __semantic: value } }],
      edges: [],
    }));
    return parsed.ok ? null : parsed.error;
  };
  if (nodeType === 'Subgraph') {
    for (const [key, value] of Object.entries(patch)) {
      if (key === '__nodeTitle' && typeof value === 'string') continue;
      if (key === '__semantic') {
        const error = validateSemantic(value);
        if (!error) continue;
        return error;
      }
      if (key === 'subgraphId' && typeof value === 'string' && value.length > 0) continue;
      return `unsupported Subgraph property "${key}"`;
    }
    return null;
  }
  if (nodeType === 'SubgraphInput' || nodeType === 'SubgraphOutput') {
    return 'Subgraph interface nodes must be authored through pcg_replace_graph';
  }
  if (!getNodeTypeDefs(nodeType)) return `unknown node type "${nodeType}"`;
  for (const [key, value] of Object.entries(patch)) {
    if (key === '__nodeTitle') {
      if (typeof value !== 'string') return '__nodeTitle requires a string';
      continue;
    }
    if (key === '__semantic') {
      const error = validateSemantic(value);
      if (!error) continue;
      return error;
    }
    const error = validateNodePropertyValue(nodeType, key, value);
    if (error) return error;
  }
  return null;
}

function validateConnections(
  nodes: Node[],
  edges: Edge[],
  subgraphs: GraphSubgraph[],
  currentSubgraph: GraphSubgraph | null,
): string | null {
  const accepted: Edge[] = [];
  for (const edge of edges) {
    if (!isValidConnection(edge, nodes, accepted, subgraphs, currentSubgraph)) {
      return `invalid or duplicate edge "${edge.id}" (${edge.source}:${edge.sourceHandle ?? 'out'} → ${edge.target}:${edge.targetHandle ?? 'in'})`;
    }
    accepted.push(edge);
  }
  return null;
}

export function parseAndValidateGraph(graph: GraphJson):
  | { ok: true; parsed: ValidatedGraph }
  | { ok: false; error: string } {
  const parsed = parseGraphJson(JSON.stringify(graph));
  if (!parsed.ok) return parsed;
  const subgraphs = parsed.subgraphs;
  const rootConnectionError = validateConnections(parsed.nodes, parsed.edges, subgraphs, null);
  if (rootConnectionError) return { ok: false, error: rootConnectionError };

  for (const subgraph of subgraphs) {
    const nodes = subgraph.nodes as unknown as Node[];
    const edges = subgraph.edges.map((edge) => ({
      ...edge,
      data: { sourcePinType: edge.sourcePinType, targetPinType: edge.targetPinType },
    })) as Edge[];
    const error = validateConnections(nodes, edges, subgraphs, subgraph);
    if (error) return { ok: false, error: `Subgraph "${subgraph.id}": ${error}` };
  }
  return { ok: true, parsed };
}

export function applyGraphOperations(
  graph: GraphJson,
  editPath: string[],
  operations: GraphOperation[],
): { ok: true; graph: GraphJson; parsed: ValidatedGraph; detail: Record<string, unknown> }
  | { ok: false; error: string } {
  if (operations.length === 0) return { ok: false, error: 'operations must not be empty' };
  const draft = cloneGraph(graph);
  const scope = resolveScope(draft, editPath);
  if (!scope) return { ok: false, error: `edit path not found: ${editPath.join('/')}` };
  scope.parameters ??= [];

  const addedNodeIds: string[] = [];
  const addedEdgeIds: string[] = [];
  for (let index = 0; index < operations.length; index++) {
    const operation = operations[index];
    const fail = (message: string) => ({ ok: false as const, error: `operation ${index} (${operation.op}): ${message}` });
    switch (operation.op) {
      case 'add_node': {
        const node = operation.node;
        if (!node.id || !node.type || !isFinitePosition(node.position)) return fail('node requires id, type, and finite position');
        if (scope.nodes.some((candidate) => candidate.id === node.id)) return fail(`duplicate node id "${node.id}"`);
        const patch = node.data ?? {};
        const error = validateNodePatch(node.type, patch);
        if (error) return fail(error);
        if (node.type === 'Subgraph') {
          const subgraphId = typeof patch.subgraphId === 'string' ? patch.subgraphId : '';
          if (!draft.subgraphs?.some((subgraph) => subgraph.id === subgraphId)) {
            return fail(`Subgraph definition "${subgraphId}" not found`);
          }
        }
        scope.nodes.push({
          id: node.id,
          type: node.type,
          position: { ...node.position },
          data: { ...defaultData(node.type), ...patch },
        });
        addedNodeIds.push(node.id);
        break;
      }
      case 'remove_node': {
        const node = scope.nodes.find((candidate) => candidate.id === operation.nodeId);
        if (!node) return fail(`node "${operation.nodeId}" not found`);
        if (node.type === 'SubgraphInput' || node.type === 'SubgraphOutput') {
          return fail('Subgraph interface nodes cannot be removed with graph ops');
        }
        scope.nodes = scope.nodes.filter((candidate) => candidate.id !== operation.nodeId);
        scope.edges = scope.edges.filter((edge) => edge.source !== operation.nodeId && edge.target !== operation.nodeId);
        scope.parameters = scope.parameters.filter((parameter) => parameter.targetNode !== operation.nodeId);
        break;
      }
      case 'patch_node': {
        const node = scope.nodes.find((candidate) => candidate.id === operation.nodeId);
        if (!node) return fail(`node "${operation.nodeId}" not found`);
        const error = validateNodePatch(node.type, operation.patch);
        if (error) return fail(error);
        node.data = { ...node.data, ...operation.patch };
        break;
      }
      case 'move_node': {
        const node = scope.nodes.find((candidate) => candidate.id === operation.nodeId);
        if (!node) return fail(`node "${operation.nodeId}" not found`);
        if (!isFinitePosition(operation.position)) return fail('position must contain finite x/y');
        node.position = { ...operation.position };
        break;
      }
      case 'add_edge': {
        const edge = operation.edge;
        if (!edge.id || !edge.source || !edge.target || !edge.sourceHandle || !edge.targetHandle) {
          return fail('edge requires id, source, target, sourceHandle, and targetHandle');
        }
        if (scope.edges.some((candidate) => candidate.id === edge.id)) return fail(`duplicate edge id "${edge.id}"`);
        scope.edges.push({ ...edge });
        addedEdgeIds.push(edge.id);
        break;
      }
      case 'remove_edge': {
        if (!scope.edges.some((edge) => edge.id === operation.edgeId)) return fail(`edge "${operation.edgeId}" not found`);
        scope.edges = scope.edges.filter((edge) => edge.id !== operation.edgeId);
        break;
      }
      case 'upsert_parameter': {
        const parameter = operation.parameter;
        if (!parameter.id) return fail('parameter id is required');
        if (!scope.nodes.some((node) => node.id === parameter.targetNode)) {
          return fail(`parameter target node "${parameter.targetNode}" not found`);
        }
        const existing = scope.parameters.findIndex((candidate) => candidate.id === parameter.id);
        if (existing >= 0) scope.parameters[existing] = { ...parameter };
        else scope.parameters.push({ ...parameter });
        break;
      }
      case 'remove_parameter': {
        if (!scope.parameters.some((parameter) => parameter.id === operation.parameterId)) {
          return fail(`parameter "${operation.parameterId}" not found`);
        }
        scope.parameters = scope.parameters.filter((parameter) => parameter.id !== operation.parameterId);
        break;
      }
    }
  }

  const validated = parseAndValidateGraph(draft);
  if (!validated.ok) return { ok: false, error: validated.error };
  return {
    ok: true,
    graph: draft,
    parsed: validated.parsed,
    detail: {
      operationCount: operations.length,
      addedNodeIds,
      addedEdgeIds,
      editPath,
    },
  };
}
