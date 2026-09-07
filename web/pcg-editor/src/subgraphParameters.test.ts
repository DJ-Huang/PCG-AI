import { describe, expect, it } from 'vitest';

import type { GraphParameter } from './graphSchema';
import {
  readSubgraphParameterValue,
  SUBGRAPH_PARAMETER_OVERRIDES_KEY,
  writeSubgraphParameterValue,
} from './subgraphParameters';

const PARAMETERS: GraphParameter[] = [
  {
    id: 'scale',
    name: 'Scale',
    type: 'vector3',
    default: [1, 1, 1],
    exposed: true,
    targetNode: 'transform',
    targetProperty: 'scale',
    hasRange: false,
    min: 0,
    max: 1,
  },
  {
    id: 'caps',
    name: 'Caps',
    type: 'boolean',
    default: true,
    exposed: true,
    targetNode: 'cylinder',
    targetProperty: 'capTop',
    hasRange: false,
    min: 0,
    max: 1,
  },
];

describe('subgraph instance parameters', () => {
  it('writes Unity-compatible overrides and reads vector3/boolean values back', () => {
    const stored = writeSubgraphParameterValue({}, PARAMETERS, 'scale', [2, 3, 4]);
    const data = { [SUBGRAPH_PARAMETER_OVERRIDES_KEY]: stored };

    expect(readSubgraphParameterValue(data, PARAMETERS[0])).toEqual([2, 3, 4]);
    expect(readSubgraphParameterValue(data, PARAMETERS[1])).toBe(true);
    expect(JSON.parse(stored)[0].stringValue).toBe('[2,3,4]');
  });

  it('accepts the compact object form used by direct graph operations', () => {
    const data = { [SUBGRAPH_PARAMETER_OVERRIDES_KEY]: { scale: [4, 2, 1], caps: false } };
    expect(readSubgraphParameterValue(data, PARAMETERS[0])).toEqual([4, 2, 1]);
    expect(readSubgraphParameterValue(data, PARAMETERS[1])).toBe(false);
  });
});
