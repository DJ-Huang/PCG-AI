import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { createServer } from 'vite';

const scriptDir = path.dirname(fileURLToPath(import.meta.url));
const editorRoot = path.resolve(scriptDir, '..');
const fixturePath = path.join(scriptDir, 'fixtures', 'golden-v2-subgraph.pcg');
const fixture = JSON.parse(fs.readFileSync(fixturePath, 'utf8'));
// The historical fixture's root AttributeTransfer edge predates the current
// manifest pin contract. Keep its v2 parameters and Subgraph coverage while
// removing that unrelated legacy edge from this command-executor test.
fixture.edges = [];
const original = structuredClone(fixture);

const server = await createServer({
  root: editorRoot,
  configFile: false,
  server: { middlewareMode: true },
});

try {
  const { applyGraphOperations, parseAndValidateGraph } = await server.ssrLoadModule(
    '/src/graphCommands.ts',
  );

  const parsed = parseAndValidateGraph(fixture);
  assert.equal(parsed.ok, true, parsed.error);
  assert.equal(parsed.parsed.subgraphs.length, 1);

  const failed = applyGraphOperations(fixture, [], [
    { op: 'move_node', nodeId: 'n1', position: { x: 99, y: 101 } },
    {
      op: 'add_edge',
      edge: {
        id: 'invalid-edge',
        source: 'n1',
        target: 'missing-node',
        sourceHandle: 'out',
        targetHandle: 'in',
      },
    },
  ]);
  assert.equal(failed.ok, false);
  assert.deepEqual(fixture, original, 'a failed batch mutated the input graph');

  const removed = applyGraphOperations(fixture, [], [
    { op: 'remove_node', nodeId: 'n1' },
  ]);
  assert.equal(removed.ok, true, removed.error);
  assert.equal(removed.graph.nodes.some((node) => node.id === 'n1'), false);
  assert.equal(removed.graph.edges.some((edge) => edge.source === 'n1'), false);
  assert.equal(removed.graph.parameters.some((parameter) => parameter.targetNode === 'n1'), false);
  assert.deepEqual(fixture, original, 'a successful batch mutated the input graph');

  console.log('Graph commands OK: v2/Subgraph parse + atomic rollback + cascade cleanup');
} finally {
  await server.close();
}
