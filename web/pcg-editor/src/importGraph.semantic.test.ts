import { describe, expect, it } from 'vitest';

import { parseGraphJson } from './importGraph';

describe('semantic graph metadata', () => {
  it('preserves validated node and Subgraph semantics', () => {
    const parsed = parseGraphJson(JSON.stringify({
      version: '2.0', nodes: [{ id: 'table', type: 'CreateBoxMesh', position: { x: 0, y: 0 }, data: {
        __semantic: { componentId: 'interior_table', label: 'Table', role: 'prop', zone: 'room_a', intent: 'blocking', recipeId: 'pcg.recipe:blockout:placed-box', memberNodeIds: ['table'], bounds: { center: [-2, 1, -2], size: [2, 1, 1] }, camera: { occluder: true } },
      } }], edges: [], subgraphs: [{
        id: 'doorway', name: 'Doorway', inputs: [], outputs: [], nodes: [], edges: [],
        semantic: { componentId: 'doorway_opening', role: 'negative_space', bounds: { center: [0, 1, 5], size: [1.2, 2, 0.2] }, anchors: { focus: [0, 1, 5] } },
      }],
    }));
    expect(parsed.ok).toBe(true);
    if (!parsed.ok) return;
    expect(parsed.nodes[0].data.__semantic).toMatchObject({ componentId: 'interior_table', label: 'Table', recipeId: 'pcg.recipe:blockout:placed-box', memberNodeIds: ['table'] });
    expect(parsed.subgraphs[0].semantic?.role).toBe('negative_space');
  });

  it('rejects an invalid semantic AABB before the graph reaches the editor', () => {
    const parsed = parseGraphJson(JSON.stringify({
      version: '1.0', nodes: [{ id: 'bowl', type: 'CreateBoxMesh', position: { x: 0, y: 0 }, data: {
        __semantic: { componentId: 'bowl_landing', bounds: { center: [0, 0, 0], size: [1, 0, 1] } },
      } }], edges: [],
    }));
    expect(parsed.ok).toBe(false);
    if (parsed.ok) return;
    expect(parsed.error).toContain('bounds.size');
  });

  it('rejects duplicate semantic members', () => {
    const parsed = parseGraphJson(JSON.stringify({
      version: '1.0', nodes: [{ id: 'wall', type: 'CreateBoxMesh', position: { x: 0, y: 0 }, data: {
        __semantic: { componentId: 'wall', memberNodeIds: ['wall', 'wall'] },
      } }], edges: [],
    }));
    expect(parsed.ok).toBe(false);
    if (parsed.ok) return;
    expect(parsed.error).toContain('duplicates');
  });
});
