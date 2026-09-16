import type { GraphJson, GraphParameter, GraphSubgraph } from '../src/graphSchema';
import { buildPreviewCookGraph, buildSubgraphCookGraph } from '../src/previewCook';
import {
  applyPreviewParameterOverrides,
  resolvePreviewParameterValues,
  savePreviewParameterDefaults,
} from '../src/previewParameters';

const failures: string[] = [];

function check(label: string, condition: boolean): void {
  if (!condition) failures.push(label);
}

const parameters: GraphParameter[] = [
  { id: 'p-int', name: 'Segments', type: 'integer', default: 1, exposed: true, targetNode: 'n1', targetProperty: 'segments', hasRange: true, min: 1, max: 10 },
  { id: 'p-number', name: 'Width', type: 'number', default: 1, exposed: true, targetNode: 'n1', targetProperty: 'sizeX', hasRange: true, min: 0, max: 5 },
  { id: 'p-bool', name: 'Enabled', type: 'boolean', default: false, exposed: true, targetNode: 'n2', targetProperty: 'enabled', hasRange: false, min: 0, max: 1 },
  { id: 'p-string', name: 'Label', type: 'string', default: 'old', exposed: true, targetNode: 'n2', targetProperty: 'label', hasRange: false, min: 0, max: 1 },
  { id: 'p-vector', name: 'Offset', type: 'vector3', default: [0, 0, 0], exposed: true, targetNode: 'n2', targetProperty: 'translate', hasRange: false, min: 0, max: 1 },
  { id: 'p-unbound', name: 'Unbound', type: 'number', default: 7, exposed: true, targetNode: '', targetProperty: '', hasRange: false, min: 0, max: 1 },
  { id: 'p-hidden', name: 'Hidden', type: 'number', default: 3, exposed: false, targetNode: 'n1', targetProperty: 'hidden', hasRange: false, min: 0, max: 1 },
];

const graph: GraphJson = {
  version: '1.0',
  nodes: [
    { id: 'n1', type: 'CreateBoxMesh', position: { x: 0, y: 0 }, data: { segments: 1, sizeX: 1 } },
    { id: 'n2', type: 'TransformMesh', position: { x: 120, y: 0 }, data: { enabled: false, label: 'old', translate: [0, 0, 0] } },
  ],
  edges: [{ id: 'e1', source: 'n1', target: 'n2', sourceHandle: 'out', targetHandle: 'in' }],
  parameters,
};

const resolved = resolvePreviewParameterValues(parameters, {
  'p-int': 4,
  'p-number': 2.5,
  'p-bool': true,
  'p-string': 'preview',
  'p-vector': [1, 2, 3],
  'p-unbound': 9,
  'p-hidden': 99,
});
check('resolve includes exposed values', Object.keys(resolved).length === 6);
check('resolve omits hidden values', !Object.hasOwn(resolved, 'p-hidden'));

const overridden = applyPreviewParameterOverrides(graph, resolved);
check('root override returns a new graph', overridden !== graph);
check('root override does not mutate source nodes', graph.nodes[0].data.sizeX === 1);
check('integer override is applied', overridden.nodes[0].data.segments === 4);
check('number override is applied', overridden.nodes[0].data.sizeX === 2.5);
check('boolean override is applied', overridden.nodes[1].data.enabled === true);
check('string override is applied', overridden.nodes[1].data.label === 'preview');
check('vector3 override is applied', JSON.stringify(overridden.nodes[1].data.translate) === '[1,2,3]');
check('hidden parameter is ignored', !Object.hasOwn(overridden.nodes[0].data, 'hidden'));

const perNode = buildPreviewCookGraph(overridden, 'n2', 'out');
check('per-node preview graph is built', perNode !== null);
check('per-node preview keeps upstream overrides', perNode?.nodes.find((node) => node.id === 'n1')?.data.sizeX === 2.5);
check('per-node preview keeps target overrides', perNode?.nodes.find((node) => node.id === 'n2')?.data.label === 'preview');

const subgraph: GraphSubgraph = {
  id: 'sg1',
  name: 'Subgraph',
  inputs: [],
  outputs: [{ id: 'out', name: 'Output', pinType: 'Geometry' }],
  nodes: [
    { id: 'sn1', type: 'CreateBoxMesh', position: { x: 0, y: 0 }, data: { sizeX: 1 } },
    { id: 'sg-out', type: 'SubgraphOutput', position: { x: 120, y: 0 }, data: {} },
  ],
  edges: [{ id: 'se1', source: 'sn1', target: 'sg-out', sourceHandle: 'out', targetHandle: 'out' }],
  parameters: [
    { id: 'p-sub', name: 'Sub Width', type: 'number', default: 1, exposed: true, targetNode: 'sn1', targetProperty: 'sizeX', hasRange: false, min: 0, max: 1 },
  ],
};
const subgraphCook = buildSubgraphCookGraph(subgraph, [subgraph]);
const overriddenSubgraph = applyPreviewParameterOverrides(subgraphCook, { 'p-sub': 6 });
check('subgraph override is applied after isolation', overriddenSubgraph.nodes.find((node) => node.id === 'sn1')?.data.sizeX === 6);

const saved = savePreviewParameterDefaults(graph.nodes, parameters, resolved);
check('save updates parameter defaults', saved.parameters.find((parameter) => parameter.id === 'p-number')?.default === 2.5);
check('save updates bound node data', saved.nodes.find((node) => node.id === 'n2')?.data.label === 'preview');
check('save leaves source graph immutable', graph.nodes[1].data.label === 'old');
check('save leaves hidden defaults unchanged', saved.parameters.find((parameter) => parameter.id === 'p-hidden')?.default === 3);
check('save preserves unbound parameter defaults', saved.parameters.find((parameter) => parameter.id === 'p-unbound')?.default === 9);

if (failures.length > 0) {
  for (const failure of failures) console.error(`FAIL: ${failure}`);
  process.exit(1);
}

console.log('Preview parameter override contract passed.');
