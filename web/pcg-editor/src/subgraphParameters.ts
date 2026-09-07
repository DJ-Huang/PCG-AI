import type { GraphParameter, NodeData } from './graphSchema';

export const SUBGRAPH_PARAMETER_OVERRIDES_KEY = 'subgraphParameterOverrides';

function parseOverrides(data: NodeData): unknown {
  const stored = data[SUBGRAPH_PARAMETER_OVERRIDES_KEY];
  if (typeof stored !== 'string') return stored;
  try {
    return JSON.parse(stored);
  } catch {
    return null;
  }
}

function entryValue(entry: Record<string, unknown>, parameter: GraphParameter): unknown {
  if ('value' in entry) return entry.value;
  if (parameter.type === 'integer') return entry.intValue;
  if (parameter.type === 'number') return entry.floatValue;
  if (parameter.type === 'boolean') return entry.boolValue;
  if (parameter.type === 'vector3' && typeof entry.stringValue === 'string') {
    try {
      const value = JSON.parse(entry.stringValue);
      return Array.isArray(value) && value.length === 3 ? value : parameter.default;
    } catch {
      return parameter.default;
    }
  }
  return entry.stringValue;
}

export function readSubgraphParameterValue(data: NodeData, parameter: GraphParameter): unknown {
  const overrides = parseOverrides(data);
  if (Array.isArray(overrides)) {
    const entry = overrides.find((candidate) => (
      candidate && typeof candidate === 'object'
      && (candidate as Record<string, unknown>).parameterId === parameter.id
    )) as Record<string, unknown> | undefined;
    return entry ? entryValue(entry, parameter) : parameter.default;
  }
  if (overrides && typeof overrides === 'object' && parameter.id in overrides) {
    return (overrides as Record<string, unknown>)[parameter.id];
  }
  return parameter.default;
}

function overrideEntry(parameter: GraphParameter, value: unknown): Record<string, unknown> {
  return {
    parameterId: parameter.id,
    name: parameter.name,
    type: parameter.type,
    floatValue: parameter.type === 'number' ? Number(value) : 0,
    intValue: parameter.type === 'integer' ? Math.round(Number(value)) : 0,
    boolValue: parameter.type === 'boolean' ? value === true : false,
    stringValue: parameter.type === 'vector3'
      ? JSON.stringify(value)
      : parameter.type === 'string' ? String(value ?? '') : '',
  };
}

export function writeSubgraphParameterValue(
  data: NodeData,
  parameters: GraphParameter[],
  parameterId: string,
  value: unknown,
): string {
  return JSON.stringify(parameters.map((parameter) => overrideEntry(
    parameter,
    parameter.id === parameterId ? value : readSubgraphParameterValue(data, parameter),
  )));
}
