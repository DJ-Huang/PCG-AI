/** Runtime-independent graph transactions over the existing node/edge wire shape.
 * Host adapters retain ownership of ordinary port resolution/type compatibility,
 * graph history, scheduling and publication. See issue #25 for host integration.
 */
import {
  canonicalJson, cloneJson, diagnostic, validateCustomFunction,
  type ContractPinType, type CustomFunctionContract, type Diagnostic,
  type PortDeclaration, type Result, type TypeCompatibility, type ValueType,
} from './customFunctionContract';

// Structural subsets of GraphNode/GraphEdge/GraphJson: no special script edges.
export interface ContractNode {
  id: string;
  type: string;
  data: Record<string, unknown>;
}
export interface ContractEdge {
  id: string;
  source: string;
  target: string;
  sourceHandle?: string;
  targetHandle?: string;
  sourcePinType?: string;
  targetPinType?: string;
}
export interface ContractScope {
  nodes: ContractNode[];
  edges: ContractEdge[];
  parameters?: { id: string; targetNode: string }[];
}
export interface ResolvedPort {
  pinType: ContractPinType;
  cardinality: 'single' | 'many';
  valueType?: ValueType;
}
export interface GraphContractAdapter {
  /** Resolve ordinary nodes, including subgraph interface nodes, in THIS scope.
   * Do not infer endpoint types from the cached strings on an edge. */
  resolvePort(node: ContractNode, direction: 'input' | 'output', handle: string): ResolvedPort | undefined;
  compatibleTypes: TypeCompatibility;
}
export interface ContractTransaction<T extends ContractScope> {
  before: T;
  after: T;
  nodeId: string;
  removedEdgeIds: string[];
}
const own = (object: object, key: string) => Object.prototype.hasOwnProperty.call(object, key);

function contractsFor(scope: ContractScope): Result<Map<string, CustomFunctionContract>> {
  const contracts = new Map<string, CustomFunctionContract>();
  const errors: Diagnostic[] = [];
  const ids = new Set<string>();
  for (const node of scope.nodes) {
    if (!node.id || ids.has(node.id)) errors.push(diagnostic('CF_NODE_ID', '/nodes', 'Node IDs must be nonempty and unique within this scope', { nodeId: node.id }));
    ids.add(node.id);
    if (node.type !== 'CustomFunction') continue;
    const result = validateCustomFunction(node.data.customFunction, node.id);
    if (!result.ok) errors.push(...result.diagnostics);
    else contracts.set(node.id, result.value);
  }
  const edgeIds = new Set<string>();
  for (const edge of scope.edges) {
    if (!edge.id || edgeIds.has(edge.id)) errors.push(diagnostic('CF_EDGE_ID', '/edges', 'Edge IDs must be nonempty and unique within this scope', { edgeId: edge.id }));
    edgeIds.add(edge.id);
  }
  return errors.length ? { ok: false, diagnostics: errors } : { ok: true, value: contracts, diagnostics: [] };
}
function resolver(scope: ContractScope, contracts: Map<string, CustomFunctionContract>, adapter: GraphContractAdapter) {
  const nodes = new Map(scope.nodes.map((node) => [node.id, node]));
  return (id: string, direction: 'input' | 'output', handle: string | undefined): ResolvedPort | undefined => {
    const node = nodes.get(id);
    if (!node) return undefined;
    const contract = contracts.get(id);
    if (contract) return (direction === 'input' ? contract.inputs : contract.outputs).find((port) => port.id === handle);
    // Preserve ordinary graph legacy fallback, but never invent CF handle IDs.
    return adapter.resolvePort(node, direction, handle ?? (direction === 'input' ? 'in' : 'out'));
  };
}
function edgeErrors(scope: ContractScope, contracts: Map<string, CustomFunctionContract>, adapter: GraphContractAdapter, onlyNode?: string): Diagnostic[] {
  const errors: Diagnostic[] = [];
  const port = resolver(scope, contracts, adapter);
  for (const edge of scope.edges) {
    if (onlyNode !== undefined && edge.source !== onlyNode && edge.target !== onlyNode) continue;
    if (!contracts.has(edge.source) && !contracts.has(edge.target)) continue;
    const source = port(edge.source, 'output', edge.sourceHandle);
    const target = port(edge.target, 'input', edge.targetHandle);
    const add = (code: string, message: string, direction: 'input' | 'output') => {
      const nodeId = direction === 'input' ? edge.target : edge.source;
      const handle = direction === 'input' ? edge.targetHandle : edge.sourceHandle;
      errors.push(diagnostic(code, '/edges', message, { edgeId: edge.id, nodeId, direction, ...(handle === undefined ? {} : { portId: handle }) }));
    };
    if (!source) add('CF_UNKNOWN_PORT', 'Resolve the declared source port by stable ID; labels/array positions are not handles', 'output');
    if (!target) add('CF_UNKNOWN_PORT', 'Resolve the declared target port by stable ID; labels/array positions are not handles', 'input');
    if (!source || !target) continue;
    if (edge.source === edge.target) add('CF_SELF_EDGE', 'Ordinary graph self-connections are forbidden', 'input');
    if (!adapter.compatibleTypes(source.pinType, target.pinType) || (
      source.valueType && target.valueType && source.valueType !== target.valueType &&
      !(source.valueType === 'integer' && target.valueType === 'number')
    )) add('CF_EDGE_TYPE', `Incompatible connection ${source.pinType} -> ${target.pinType}; no implicit literal/geometry coercion`, 'input');
    if (source.cardinality === 'many' && target.cardinality === 'single') add('CF_EDGE_CARDINALITY', 'A many output cannot feed a single input', 'input');
    const incoming = scope.edges.filter((candidate) => candidate.target === edge.target && (candidate.targetHandle ?? 'in') === (edge.targetHandle ?? 'in'));
    if (target.cardinality === 'single' && incoming.length > 1) add('CF_EDGE_CARDINALITY', 'A single input accepts only one connection; explicitly remove competing edges', 'input');
    if (incoming.filter((candidate) => candidate.source === edge.source && (candidate.sourceHandle ?? 'out') === (edge.sourceHandle ?? 'out')).length > 1) add('CF_DUPLICATE_EDGE', 'Duplicate source/target handle connection', 'input');
  }
  return errors;
}

/** Static connection diagnostics. An unconnected required port remains a savable
 * draft but must be diagnosed before Cook. No source is executed. */
export function validateCustomFunctionConnections(scope: ContractScope, adapter: GraphContractAdapter): Diagnostic[] {
  const contracts = contractsFor(scope);
  if (!contracts.ok) return contracts.diagnostics;
  const errors = edgeErrors(scope, contracts.value, adapter);
  for (const [nodeId, contract] of contracts.value) for (const input of contract.inputs) {
    if (input.required && !own(input, 'default') && !scope.edges.some((edge) => edge.target === nodeId && edge.targetHandle === input.id)) {
      errors.push(diagnostic('CF_MISSING_INPUT', `/inputs/${input.id}`, `Connect required input "${input.label}" (${input.id})`, { nodeId, portId: input.id, direction: 'input' }));
    }
  }
  return errors;
}

/** Prepare ONE history transaction. Default policy is rejection, never silent
 * pruning. Explicit remove prunes ALL invalid incident edges (no arbitrary winner).
 * The caller must publish after exactly once through its ordinary history stack. */
export function planCustomFunctionEdit<T extends ContractScope>(scope: T, nodeId: string, next: unknown, adapter: GraphContractAdapter, invalidEdges: 'reject' | 'remove' = 'reject'): Result<ContractTransaction<T>> {
  if (invalidEdges !== 'reject' && invalidEdges !== 'remove') return { ok: false, diagnostics: [diagnostic('CF_EDIT_POLICY', '', 'Choose reject or explicit remove')] };
  const current = contractsFor(scope);
  if (!current.ok) return current;
  const existing = scope.nodes.find((node) => node.id === nodeId);
  if (!existing || existing.type !== 'CustomFunction') return { ok: false, diagnostics: [diagnostic('CF_NODE_NOT_FOUND', '/nodes', 'Select a CustomFunction node in the current scope', { nodeId })] };
  const validation = validateCustomFunction(next, nodeId);
  if (!validation.ok) return validation;
  const after = cloneJson(scope);
  after.nodes.find((node) => node.id === nodeId)!.data.customFunction = validation.value;
  current.value.set(nodeId, validation.value);
  const invalid = edgeErrors(after, current.value, adapter, nodeId);
  if (invalid.length && invalidEdges === 'reject') return { ok: false, diagnostics: invalid };
  const removed = new Set(invalid.flatMap((error) => error.edgeId ? [error.edgeId] : []));
  after.edges = after.edges.filter((edge) => !removed.has(edge.id));
  // Cached pin-type annotations are advisory; refresh retained incident edges.
  const port = resolver(after, current.value, adapter);
  for (const edge of after.edges) if (edge.source === nodeId || edge.target === nodeId) {
    if (own(edge, 'sourcePinType')) edge.sourcePinType = port(edge.source, 'output', edge.sourceHandle)!.pinType;
    if (own(edge, 'targetPinType')) edge.targetPinType = port(edge.target, 'input', edge.targetHandle)!.pinType;
  }
  return {
    ok: true,
    value: { before: cloneJson(scope), after, nodeId, removedEdgeIds: [...removed].sort() },
    diagnostics: invalid.map((error) => ({ ...error, severity: 'warning', message: `${error.message}; edge removed by explicit transaction policy` })),
  };
}

/** Snapshot replay guard prevents Undo/Redo from overwriting intervening edits.
 * This is a history adapter, not a second global editor history stack. */
export function replayCustomFunctionEdit<T extends ContractScope>(current: T, transaction: ContractTransaction<T>, direction: 'undo' | 'redo'): Result<T> {
  if (direction !== 'undo' && direction !== 'redo') return { ok: false, diagnostics: [diagnostic('CF_HISTORY_DIRECTION', '', 'Choose undo or redo')] };
  const expected = direction === 'undo' ? transaction.after : transaction.before;
  if (canonicalJson(current) !== canonicalJson(expected)) return { ok: false, diagnostics: [diagnostic('CF_STALE_TRANSACTION', '', 'Graph changed since this transaction; use the current graph history instead of overwriting it')] };
  return { ok: true, value: cloneJson(direction === 'undo' ? transaction.before : transaction.after), diagnostics: [] };
}

export interface SelectionCopy {
  nodes: ContractNode[];
  edges: ContractEdge[];
  parameters: NonNullable<ContractScope['parameters']>;
  nodeIdMap: Record<string, string>;
}
/** Copy/duplicate: regenerate node/edge/graph-parameter identities ONLY. Port IDs,
 * parameter declaration IDs, source and resource references are node-local and
 * remain byte-for-byte data. External edges are deliberately not copied.
 * Shared subgraphId references stay shared; cloning a definition is a host action. */
export function copyContractSelection(scope: ContractScope, selectedIds: string[], newId: (kind: 'node' | 'edge' | 'parameter', oldId: string) => string): Result<SelectionCopy> {
  const valid = contractsFor(scope);
  if (!valid.ok) return valid;
  const selected = new Set(selectedIds);
  if (selected.size !== selectedIds.length || selectedIds.some((id) => !scope.nodes.some((node) => node.id === id))) return { ok: false, diagnostics: [diagnostic('CF_SELECTION', '', 'Selection must contain unique existing node IDs')] };
  const used = new Set([...scope.nodes.map((node) => node.id), ...scope.edges.map((edge) => edge.id), ...(scope.parameters ?? []).map((param) => param.id)]);
  const remap = new Map<string, string>();
  const allocate = (kind: 'node' | 'edge' | 'parameter', oldId: string) => {
    const id = newId(kind, oldId);
    if (typeof id !== 'string' || !id || used.has(id) || ['__proto__', 'prototype', 'constructor'].includes(id)) throw new Error('Identity allocator returned an empty, reserved, or colliding ID');
    used.add(id);
    return id;
  };
  try {
    const nodes = scope.nodes.filter((node) => selected.has(node.id)).map((node) => {
      const copy = cloneJson(node);
      copy.id = allocate('node', node.id);
      remap.set(node.id, copy.id);
      return copy;
    });
    const edges = scope.edges.filter((edge) => selected.has(edge.source) && selected.has(edge.target)).map((edge) => ({
      ...cloneJson(edge), id: allocate('edge', edge.id), source: remap.get(edge.source)!, target: remap.get(edge.target)!,
    }));
    const parameters = (scope.parameters ?? []).filter((param) => selected.has(param.targetNode)).map((param) => ({
      ...cloneJson(param), id: allocate('parameter', param.id), targetNode: remap.get(param.targetNode)!,
    }));
    return { ok: true, value: { nodes, edges, parameters, nodeIdMap: Object.fromEntries(remap) }, diagnostics: [] };
  } catch (error) { return { ok: false, diagnostics: [diagnostic('CF_COPY_ID', '', String(error))] }; }
}

/** Presets carry a complete contract, not node/global identities or live handles.
 * Applying one is planCustomFunctionEdit(...preset.value..., policy), never a
 * shallow data patch that can orphan edges or retain undeclared parameter values. */
export function createCustomFunctionPreset(contract: unknown): Result<CustomFunctionContract> {
  return validateCustomFunction(contract);
}

/** Manifest adapter helper: existing variadic input pins map to many, otherwise
 * single. Output cardinality must come from the host output contract, not this
 * input-only helper; hosts with richer metadata should supply that directly. */
export function manifestInputPortContract(pin: { pinType: ContractPinType; variadic?: boolean }): ResolvedPort {
  return { pinType: pin.pinType, cardinality: pin.variadic ? 'many' : 'single' };
}

export function getCustomFunctionPorts(contract: CustomFunctionContract, direction: 'input' | 'output'): PortDeclaration[] {
  return cloneJson(direction === 'input' ? contract.inputs : contract.outputs);
}
