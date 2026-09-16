import type { GraphJson, GraphNode, GraphParameter } from './graphSchema';

export type PreviewParameterValue = GraphParameter['default'];
export type PreviewParameterValues = Record<string, PreviewParameterValue>;

function validValue(parameter: GraphParameter, value: unknown): value is PreviewParameterValue {
  switch (parameter.type) {
    case 'number': return typeof value === 'number' && Number.isFinite(value);
    case 'integer': return typeof value === 'number' && Number.isSafeInteger(value);
    case 'boolean': return typeof value === 'boolean';
    case 'string': return typeof value === 'string';
    case 'vector3': return Array.isArray(value) && value.length === 3 &&
      value.every((part) => typeof part === 'number' && Number.isFinite(part));
    default: return false;
  }
}

function copyValue(value: PreviewParameterValue): PreviewParameterValue {
  return Array.isArray(value) ? [...value] : value;
}

function checkValue(parameter: GraphParameter, value: unknown): asserts value is PreviewParameterValue {
  if (!validValue(parameter, value)) {
    throw new Error(`Invalid ${parameter.type} value for parameter ${parameter.id}`);
  }
}

export function resolvePreviewParameterValues(
  parameters: readonly GraphParameter[],
  stored: PreviewParameterValues | undefined,
): PreviewParameterValues {
  // Null prototype also handles IDs such as '__proto__' as ordinary data.
  const values: PreviewParameterValues = Object.create(null);
  for (const parameter of parameters) {
    if (!parameter.exposed) continue;
    if (Object.hasOwn(values, parameter.id)) throw new Error(`Duplicate parameter ID: ${parameter.id}`);
    const value = stored && Object.hasOwn(stored, parameter.id)
      ? stored[parameter.id]
      : parameter.default;
    checkValue(parameter, value);
    values[parameter.id] = copyValue(value);
  }
  return values;
}

/** Explicitly report conflicting authoring state; do not guess which value won. */
export function previewParameterDrift(graph: Pick<GraphJson, 'nodes' | 'parameters'>): string[] {
  const nodes = new Map(graph.nodes.map((node) => [node.id, node]));
  const issues: string[] = [];
  const targets = new Set<string>();
  for (const parameter of graph.parameters ?? []) {
    if (!parameter.exposed || !parameter.targetNode || !parameter.targetProperty) continue;
    const key = JSON.stringify([parameter.targetNode, parameter.targetProperty]);
    if (targets.has(key)) issues.push(`Multiple parameters target ${parameter.targetNode}.${parameter.targetProperty}`);
    targets.add(key);
    const node = nodes.get(parameter.targetNode);
    if (!node) issues.push(`Parameter ${parameter.id} targets missing node ${parameter.targetNode}`);
    else if (Object.hasOwn(node.data, parameter.targetProperty) &&
      JSON.stringify(node.data[parameter.targetProperty]) !== JSON.stringify(parameter.default)) {
      issues.push(`Parameter ${parameter.id}: default differs from ${parameter.targetNode}.${parameter.targetProperty}`);
    }
  }
  return issues;
}

export function applyPreviewParameterOverrides(
  graph: GraphJson,
  values: PreviewParameterValues,
): GraphJson {
  const saved = savePreviewParameterDefaults(graph.nodes, graph.parameters ?? [], values);
  return { ...graph, nodes: saved.nodes, parameters: saved.parameters };
}

export function savePreviewParameterDefaults(
  nodes: readonly GraphNode[],
  parameters: readonly GraphParameter[],
  values: PreviewParameterValues,
): { nodes: GraphNode[]; parameters: GraphParameter[] } {
  const overrides = new Map<string, Map<string, PreviewParameterValue>>();
  const nodeIds = new Set(nodes.map((node) => node.id));
  const ids = new Set<string>();
  const nextParameters = parameters.map((parameter) => {
    if (!parameter.exposed || !Object.hasOwn(values, parameter.id)) return parameter;
    if (ids.has(parameter.id)) throw new Error(`Duplicate parameter ID: ${parameter.id}`);
    ids.add(parameter.id);
    const value = values[parameter.id];
    checkValue(parameter, value);
    if (parameter.targetNode && parameter.targetProperty) {
      if (!nodeIds.has(parameter.targetNode)) {
        throw new Error(`Parameter ${parameter.id} targets missing node ${parameter.targetNode}`);
      }
      const patch = overrides.get(parameter.targetNode) ?? new Map<string, PreviewParameterValue>();
      if (patch.has(parameter.targetProperty)) {
        throw new Error(`Multiple parameters target ${parameter.targetNode}.${parameter.targetProperty}`);
      }
      patch.set(parameter.targetProperty, copyValue(value));
      overrides.set(parameter.targetNode, patch);
    }
    return { ...parameter, default: copyValue(value) };
  });
  const nextNodes = nodes.map((node) => {
    const patch = overrides.get(node.id);
    if (!patch) return node;
    // Object.fromEntries creates own properties, including '__proto__'.
    return { ...node, data: { ...node.data, ...Object.fromEntries(patch) } };
  });
  return { nodes: nextNodes, parameters: nextParameters };
}
