import { describe, expect, it } from 'vitest';
import type { GraphJson } from './graphSchema';
import { PcgExecuteKind, type CookResult } from './cookResult';
import {
  PREVIEW_SINK_NODE_ID,
  SUBGRAPH_PREVIEW_OUTPUT_ID,
  buildPreviewDataFromCook,
  buildSubgraphCookGraph,
  prepareGraphForPreviewCook,
} from './previewCook';

function node(id: string, type: string, data: Record<string, unknown> = {}) {
  return { id, type, position: { x: 0, y: 0 }, data };
}

describe('prepareGraphForPreviewCook', () => {
  it('prunes orphan nodes that do not feed the Output', () => {
    const graph: GraphJson = {
      version: '2.0',
      nodes: [
        node('box', 'CreateBoxMesh'),
        node('cable', 'Subgraph', { subgraphId: 'lib_x' }),
        node('out', 'Output'),
      ],
      edges: [
        {
          id: 'e1',
          source: 'box',
          target: 'out',
          sourceHandle: 'out',
          targetHandle: 'in',
        },
      ],
      parameters: [],
      subgraphs: [
        {
          id: 'lib_x',
          name: 'X',
          inputs: [],
          outputs: [],
          nodes: [],
          edges: [],
        },
      ],
    };

    const cooked = prepareGraphForPreviewCook(graph);
    const ids = cooked.nodes.map((n) => n.id);
    expect(ids).toContain('box');
    expect(ids).not.toContain('cable');
    // Subgraph definitions pass through untouched (referenced or not).
    expect(cooked.subgraphs).toHaveLength(1);
  });

  it('keeps the full upstream chain and retargets Output to the preview sink', () => {
    const graph: GraphJson = {
      version: '2.0',
      nodes: [node('a', 'CreateBoxMesh'), node('b', 'BevelMesh'), node('out', 'Output')],
      edges: [
        { id: 'e1', source: 'a', target: 'b', sourceHandle: 'out', targetHandle: 'in' },
        { id: 'e2', source: 'b', target: 'out', sourceHandle: 'out', targetHandle: 'in' },
      ],
      parameters: [],
      subgraphs: [],
    };

    const cooked = prepareGraphForPreviewCook(graph);
    expect(cooked.nodes.map((n) => n.id).sort()).toEqual([PREVIEW_SINK_NODE_ID, 'a', 'b']);
    const sinkEdge = cooked.edges.find((e) => e.target === PREVIEW_SINK_NODE_ID);
    expect(sinkEdge?.source).toBe('b');
  });

  it('filters parameters to the cooked nodes', () => {
    const graph: GraphJson = {
      version: '2.0',
      nodes: [node('box', 'CreateBoxMesh'), node('orphan', 'CreateBoxMesh'), node('out', 'Output')],
      edges: [
        { id: 'e1', source: 'box', target: 'out', sourceHandle: 'out', targetHandle: 'in' },
      ],
      parameters: [
        { id: 'p1', name: 'W', type: 'number', default: 1, exposed: true, targetNode: 'box', targetProperty: 'width', hasRange: false, min: 0, max: 1 },
        { id: 'p2', name: 'H', type: 'number', default: 1, exposed: true, targetNode: 'orphan', targetProperty: 'height', hasRange: false, min: 0, max: 1 },
      ],
      subgraphs: [],
    };

    const cooked = prepareGraphForPreviewCook(graph);
    expect(cooked.parameters?.map((p) => p.id)).toEqual(['p1']);
  });

  it('returns the graph unchanged when there is no connected Output', () => {
    const graph: GraphJson = {
      version: '2.0',
      nodes: [node('box', 'CreateBoxMesh')],
      edges: [],
      parameters: [],
      subgraphs: [],
    };
    expect(prepareGraphForPreviewCook(graph)).toBe(graph);
  });
});

describe('buildSubgraphCookGraph', () => {
  const subgraph = {
    id: 'sg',
    name: 'Sg',
    inputs: [{ id: 'in_pin', name: 'In', pinType: 'Any' }],
    outputs: [{ id: 'mesh', name: 'Mesh', pinType: 'SpatialMesh' }],
    nodes: [
      node('subgraph_input', 'SubgraphInput'),
      node('chain_a', 'CreateBoxMesh'),
      node('chain_b', 'BevelMesh'),
      node('orphan', 'CreateBoxMesh'),
      node('subgraph_output', 'SubgraphOutput'),
    ],
    edges: [
      { id: 'i1', source: 'subgraph_input', target: 'chain_a', sourceHandle: 'in_pin', targetHandle: 'in' },
      { id: 'i2', source: 'chain_a', target: 'chain_b', sourceHandle: 'out', targetHandle: 'in' },
      { id: 'i3', source: 'chain_b', target: 'subgraph_output', sourceHandle: 'out', targetHandle: 'mesh' },
    ],
    parameters: [
      { id: 'p1', name: 'W', type: 'number' as const, default: 1, exposed: true, targetNode: 'chain_a', targetProperty: 'width', hasRange: false, min: 0, max: 1 },
      { id: 'p2', name: 'H', type: 'number' as const, default: 1, exposed: true, targetNode: 'orphan', targetProperty: 'height', hasRange: false, min: 0, max: 1 },
    ],
  };

  it('prunes interior orphan nodes and their parameters', () => {
    const cooked = buildSubgraphCookGraph(subgraph, []);
    const ids = cooked.nodes.map((n) => n.id);
    expect(ids).toContain('chain_a');
    expect(ids).toContain('chain_b');
    expect(ids).toContain(SUBGRAPH_PREVIEW_OUTPUT_ID);
    expect(ids).not.toContain('orphan');
    expect(ids).not.toContain('subgraph_input');
    expect(ids).not.toContain('subgraph_output');
    expect(cooked.parameters?.map((p) => p.id)).toEqual(['p1']);
    // The output feeder is rewired to the synthetic Output node.
    expect(cooked.edges.some((e) => e.target === SUBGRAPH_PREVIEW_OUTPUT_ID && e.source === 'chain_b')).toBe(true);
  });
});

function cookStub(json: string): CookResult {
  return {
    code: 0,
    kind: PcgExecuteKind.Json,
    nodesExecuted: 1,
    nodesSkipped: 0,
    graphExecuteMs: 0,
    binaryWriteMs: 0,
    pointCount: 0,
    pointAttrFlags: 0,
    vertexCount: 0,
    indexCount: 0,
    error: '',
    json,
    mesh: new Uint8Array(),
    points: new Uint8Array(),
    geometry: new Uint8Array(),
    heightfield: new Uint8Array(),
    perf: '',
  };
}

describe('buildPreviewDataFromCook texture output', () => {
  it('accepts a texture-only cook result and resolves the image url', () => {
    const result = buildPreviewDataFromCook(cookStub(JSON.stringify({
      kind: 'texture',
      slotId: 'tex1',
      source: 'pcg-resource://textures/brick.png',
      repeatX: 2,
      repeatY: 3,
    })));
    expect(result.ok).toBe(true);
    expect(result.data?.images).toEqual([
      {
        nodeId: 'tex1',
        storage: 'pcg-resource://textures/brick.png',
        url: '/assets/textures/brick.png',
        repeatX: 2,
        repeatY: 3,
      },
    ]);
  });

  it('keeps the JSON-only error when the result is not a texture', () => {
    const result = buildPreviewDataFromCook(cookStub(JSON.stringify({ kind: 'other' })));
    expect(result.ok).toBe(false);
    expect(result.error).toContain('JSON output only');
  });

  it('treats a texture without source as previewable with an empty url', () => {
    const result = buildPreviewDataFromCook(cookStub(JSON.stringify({ kind: 'texture', slotId: 'gen1' })));
    expect(result.ok).toBe(true);
    expect(result.data?.images[0]).toMatchObject({ nodeId: 'gen1', url: '', repeatX: 1, repeatY: 1 });
  });
});
