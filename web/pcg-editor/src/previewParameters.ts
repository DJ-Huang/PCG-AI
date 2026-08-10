import type { GraphJson, GraphNode, GraphParameter } from './graphSchema';

export type PreviewParameterValue = GraphParameter['default'];
export type PreviewParameterValues = Record<string, PreviewParameterValue>;

export function resolvePreviewParameterValues(
  parameters: readonly GraphParameter[],
  stored: PreviewParameterValues | undefined,
): PreviewParameterValues {
  const values: PreviewParameterValues = {};
  for (const parameter of parameters) {
    if (!parameter.exposed) continue;
    values[parameter.id] = stored && Object.hasOwn(stored, parameter.id)
      ? stored[parameter.id]
      : parameter.default;
  }
  return values;
}

export function applyPreviewParameterOverrides(
  graph: GraphJson,
  values: PreviewParameterValues,
): GraphJson {
  const overrides = new Map<string, Record<string, PreviewParameterValue>>();
  for (const parameter of graph.parameters ?? []) {
    if (
      !parameter.exposed ||
      !parameter.targetNode ||
      !parameter.targetProperty ||
      !Object.hasOwn(values, parameter.id)
    ) {
      continue;
    }
    const patch = overrides.get(parameter.targetNode) ?? {};
    patch[parameter.targetProperty] = values[parameter.id];
    overrides.set(parameter.targetNode, patch);
  }

  if (overrides.size === 0) return graph;
  return {
    ...graph,
    nodes: graph.nodes.map((node) => {
      const patch = overrides.get(node.id);
      return patch ? { ...node, data: { ...node.data, ...patch } } : node;
    }),
  };
}

export function savePreviewParameterDefaults(
  nodes: readonly GraphNode[],
  parameters: readonly GraphParameter[],
  values: PreviewParameterValues,
): { nodes: GraphNode[]; parameters: GraphParameter[] } {
  const exposedValues = new Map<string, PreviewParameterValue>();
  const nextParameters = parameters.map((parameter) => {
    if (!parameter.exposed || !Object.hasOwn(values, parameter.id)) return parameter;
    const value = values[parameter.id];
    if (parameter.targetNode && parameter.targetProperty) {
      const key = `${parameter.targetNode}\u0000${parameter.targetProperty}`;
      exposedValues.set(key, value);
    }
    return { ...parameter, default: value };
  });

  const nextNodes = nodes.map((node) => {
    let nextData: GraphNode['data'] | null = null;
    for (const [key, value] of exposedValues) {
      const [targetNode, targetProperty] = key.split('\u0000');
      if (targetNode !== node.id) continue;
      nextData ??= { ...node.data };
      nextData[targetProperty] = value;
    }
    return nextData ? { ...node, data: nextData } : node;
  });

  return { nodes: nextNodes, parameters: nextParameters };
}
