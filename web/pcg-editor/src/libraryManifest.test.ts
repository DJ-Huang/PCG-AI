import { beforeEach, describe, expect, it, vi } from 'vitest';
import {
  ensureLibraryIndex,
  getLibraryItemsByCategory,
  libraryDefinitionId,
  libraryItemHasCompatibleInput,
  libraryItemHasCompatibleOutput,
  libraryItemMatchesQuery,
  loadLibrarySubgraph,
  type LibraryIndexItem,
} from './libraryManifest';

const INDEX = {
  version: 1,
  items: [
    {
      id: 'pcg.lib:cable:cable_generator',
      displayName: 'Cable Generator',
      category: 'Cable',
      file: 'cable/cable_generator.pcgsubgraph',
      assetVersion: '1.0.0',
      contentHash: 'abc123',
      keywords: ['cable', 'wire', '电缆'],
      description: '沿 spline 扫掠电缆',
      inputs: [{ id: 'path', name: 'Path', pinType: 'SpatialSpline' }],
      outputs: [{ id: 'out', name: 'Geometry', pinType: 'SpatialGeometry' }],
      parameters: [
        {
          id: 'thickness',
          name: 'Thickness',
          type: 'number',
          default: 0.03,
          hasRange: true,
          min: 0.002,
          max: 0.5,
        },
      ],
    },
  ],
};

const ASSET = {
  version: '2.0',
  name: 'Cable Generator',
  contentHash: 'abc123',
  inputs: [{ id: 'path', name: 'Path', pinType: 'SpatialSpline' }],
  outputs: [{ id: 'out', name: 'Geometry', pinType: 'SpatialGeometry' }],
  parameters: [
    {
      id: 'thickness',
      name: 'Thickness',
      type: 'number',
      default: 0.03,
      exposed: true,
      targetNode: 'sweep',
      targetProperty: 'radius',
      hasRange: true,
      min: 0.002,
      max: 0.5,
    },
  ],
  nodes: [
    { id: 'subgraph_input', type: 'SubgraphInput', position: { x: 200, y: 0 }, data: {} },
    { id: 'sweep', type: 'SweepAlongSpline', position: { x: 200, y: 160 }, data: {} },
    { id: 'subgraph_output', type: 'SubgraphOutput', position: { x: 200, y: 320 }, data: {} },
  ],
  edges: [],
  subgraphs: [],
};

const ITEM = INDEX.items[0] as LibraryIndexItem;

function mockFetch() {
  return vi.fn((url: string) => {
    const body = url.endsWith('library-index.json') ? INDEX : ASSET;
    return Promise.resolve({
      ok: true,
      json: () => Promise.resolve(body),
    } as Response);
  });
}

describe('libraryManifest', () => {
  beforeEach(() => {
    vi.stubGlobal('fetch', mockFetch());
  });

  it('loads and caches the index', async () => {
    const index = await ensureLibraryIndex();
    expect(index?.items).toHaveLength(1);
    const byCategory = getLibraryItemsByCategory();
    expect(byCategory.get('Cable')?.[0].id).toBe('pcg.lib:cable:cable_generator');
  });

  it('derives a stable definition id from the library id', () => {
    expect(libraryDefinitionId(ITEM)).toBe('lib_pcg_lib_cable_cable_generator');
  });

  it('matches queries across display name, id and keywords', () => {
    expect(libraryItemMatchesQuery(ITEM, 'cable')).toBe(true);
    expect(libraryItemMatchesQuery(ITEM, '电缆')).toBe(true);
    expect(libraryItemMatchesQuery(ITEM, 'unrelated')).toBe(false);
  });

  it('checks pin compatibility using manifest pin rules', () => {
    expect(libraryItemHasCompatibleInput(ITEM, 'SpatialSpline')).toBe(true);
    expect(libraryItemHasCompatibleInput(ITEM, 'SpatialMesh')).toBe(false);
    expect(libraryItemHasCompatibleOutput(ITEM, 'SpatialGeometry')).toBe(true);
    expect(libraryItemHasCompatibleOutput(ITEM, 'SpatialMesh')).toBe(true);
    expect(libraryItemHasCompatibleOutput(ITEM, 'HeightField')).toBe(false);
  });

  it('converts the asset payload into an inline subgraph definition', async () => {
    await ensureLibraryIndex();
    const subgraph = await loadLibrarySubgraph(ITEM);
    expect(subgraph.id).toBe('lib_pcg_lib_cable_cable_generator');
    expect(subgraph.name).toBe('Cable Generator');
    expect(subgraph.inputs[0].pinType).toBe('SpatialSpline');
    expect(subgraph.nodes).toHaveLength(3);
    expect(subgraph.parameters?.[0].targetNode).toBe('sweep');
  });
});
