#!/usr/bin/env node
// No QuickJS, browser, npm downloads or network required. Uses the project's
// installed TypeScript compiler (or a preinstalled tsc) and Node's test runner.
import assert from 'node:assert/strict';
import { after, test } from 'node:test';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, readFileSync, rmSync, existsSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createRequire } from 'node:module';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const temporary = mkdtempSync(join(tmpdir(), 'pcg-custom-function-contract-'));
after(() => rmSync(temporary, { recursive: true, force: true }));
const compiler = join(root, 'web/pcg-editor/node_modules/typescript/bin/tsc');
const source = join(root, 'web/pcg-editor/src');
const args = ['--strict', '--noUnusedLocals', '--noUnusedParameters', '--target', 'ES2022', '--module', 'commonjs', '--lib', 'ES2022', '--outDir', temporary,
  join(source, 'customFunctionContract.ts'), join(source, 'customFunctionGraph.ts')];
try {
  if (existsSync(compiler)) execFileSync(process.execPath, [compiler, ...args], { stdio: 'pipe' });
  else execFileSync('tsc', args, { stdio: 'pipe' });
} catch (error) {
  rmSync(temporary, { recursive: true, force: true });
  console.error('Contract TypeScript compilation failed. Install web/pcg-editor dependencies with npm ci, or provide tsc on PATH.');
  if (error.stdout) console.error(error.stdout.toString());
  if (error.stderr) console.error(error.stderr.toString());
  throw error;
}
const require = createRequire(import.meta.url);
const C = require(join(temporary, 'customFunctionContract.js'));
const G = require(join(temporary, 'customFunctionGraph.js'));
const fixture = JSON.parse(readFileSync(join(root, 'schema/fixtures/custom-function-v1.pcg'), 'utf8'));
const schema = JSON.parse(readFileSync(join(root, 'schema/custom-function-schema-v1.json'), 'utf8'));
const copy = (value) => JSON.parse(JSON.stringify(value));
const base = () => copy(fixture.nodes[0].data.customFunction);
const codes = (result) => result.diagnostics.map((d) => d.code);
function success(result) { assert.equal(result.ok, true, JSON.stringify(result.diagnostics)); return result.value; }
function fails(result, code) { assert.equal(result.ok, false); assert.ok(codes(result).includes(code), JSON.stringify(result.diagnostics)); assert.equal('value' in result, false); }
const p = (id, options = {}) => ({ id, label: id, pinType: 'SpatialGeometry', cardinality: 'single', required: true, ...options });
const cf = (id, contract = base()) => ({ id, type: 'CustomFunction', position: { x: 0, y: 0 }, data: { customFunction: contract } });
const edge = (id, source, sourceHandle, target, targetHandle) => ({ id, source, sourceHandle, target, targetHandle });
const handleTypes = new WeakMap();
function handle(type = 'SpatialGeometry') { const value = {}; handleTypes.set(value, type); return value; }
const inspect = (value) => value && typeof value === 'object' ? handleTypes.get(value) : undefined;
// A tiny manifest fixture, NOT a replacement operation registry. Production hosts
// must pass their ordinary pinTypesCompatible and scope-aware resolver functions.
const adapter = {
  compatibleTypes: (sourceType, targetType) => sourceType === targetType || sourceType === 'Any' || targetType === 'Any',
  resolvePort(node, direction, id) {
    if (node.type === 'GeometrySource' && direction === 'output' && id === 'out') return { pinType: 'SpatialGeometry', cardinality: 'single' };
    if ((node.type === 'GeometrySink' || node.type === 'Output') && direction === 'input' && id === 'in') return { pinType: 'SpatialGeometry', cardinality: 'single' };
    if (node.type === 'ParamSink' && direction === 'input' && id === 'in') return { pinType: 'Param', cardinality: 'single' };
    return undefined;
  },
};
const ordinary = (id, type) => ({ id, type, position: { x: 200, y: 100 }, data: {} });
function wired() {
  return {
    version: '2.0', nodes: [cf('script1'), ordinary('sink', 'GeometrySink'), ordinary('counter', 'ParamSink')],
    edges: [edge('body-edge', 'script1', 'body', 'sink', 'in'), edge('count-edge', 'script1', 'count', 'counter', 'in')],
    parameters: [{ id: 'graph-width', name: 'Width', type: 'number', default: 3, exposed: true, targetNode: 'sink', targetProperty: 'width' }],
  };
}

test('fixture: zero inputs, multiple named outputs, explicit primary output', () => {
  const c = success(C.validateCustomFunction(base(), 'script1'));
  assert.equal(c.inputs.length, 0);
  assert.deepEqual(c.outputs.map((port) => port.id), ['body', 'count']);
  assert.equal(c.primaryOutput, 'body');
  assert.deepEqual(G.validateCustomFunctionConnections(fixture, adapter), []);
});

test('JSON Schema and executable contract use the same ordinary pin vocabulary', () => {
  assert.deepEqual([...C.PIN_TYPES].sort(), [...schema.$defs.pinType.enum].sort());
  const graphSchemaPath = join(root, 'schema/graph-schema-v2.json');
  assert.ok(existsSync(graphSchemaPath), 'The ordinary graph schema must be available; never substitute a stub');
  const graphSchema = JSON.parse(readFileSync(graphSchemaPath, 'utf8'));
  assert.deepEqual([...C.PIN_TYPES].sort(), [...graphSchema.$defs.pinType.enum].sort());
});

test('missing/unsupported schema, API, language and authoring profile fail with migration guidance', () => {
  for (const [field, code] of [['schemaVersion', 'SCHEMA_VERSION'], ['apiVersion', 'API_VERSION'], ['language', 'LANGUAGE'], ['languageVersion', 'LANGUAGE_VERSION']]) {
    for (const missing of [false, true]) {
      const c = base(); if (missing) delete c[field]; else c[field] = 'future';
      const before = copy(c);
      const result = C.migrateCustomFunction(c);
      fails(result, `CF_UNSUPPORTED_${code}`);
      assert.ok(result.diagnostics[0].message.length > 60);
      assert.deepEqual(c, before);
    }
  }
});

test('v1 identity migration and parse roundtrip preserve source exactly', () => {
  const c = base(); c.source = '\ufeff// 日本語 / 中文\r\nexport function main(ctx) { return {}; }\r\n';
  assert.deepEqual(success(C.parseCustomFunction(C.canonicalJson(c))), c);
  assert.deepEqual(success(C.migrateCustomFunction(c)), c);
  fails(C.parseCustomFunction('{broken'), 'CF_JSON_PARSE');
});

test('entry point and uint32 seed are checked without compiling the module', () => {
  for (const entryPoint of ['default', 'a.b', '', 'constructor']) { const c = base(); c.entryPoint = entryPoint; fails(C.validateCustomFunction(c), 'CF_ENTRY_POINT'); }
  for (const seed of [-1, 0x100000000, 1.5, '42']) { const c = base(); c.seed = seed; fails(C.validateCustomFunction(c), 'CF_SEED'); }
  for (const seed of [0, 0xffffffff]) { const c = base(); c.seed = seed; success(C.validateCustomFunction(c)); }
});

test('inspection cannot execute source, even top-level side effects, invalid syntax or infinite loops', () => {
  globalThis.__cfInspectionProbe = 0;
  const originalEval = globalThis.eval;
  const originalFunction = globalThis.Function;
  try {
    globalThis.eval = () => assert.fail('eval must not be used by inspection');
    globalThis.Function = () => assert.fail('Function must not be used by inspection');
    for (const sourceText of ['globalThis.__cfInspectionProbe++;', 'while (true) {}', 'throw new Error("executed");', 'this is not JavaScript', '']) {
      const c = base(); c.source = sourceText;
      success(C.validateCustomFunction(c)); success(C.parseCustomFunction(JSON.stringify(c)));
      success(G.createCustomFunctionPreset(c)); C.customFunctionCacheDescriptor(c);
    }
    assert.equal(globalThis.__cfInspectionProbe, 0);
  } finally { globalThis.eval = originalEval; globalThis.Function = originalFunction; delete globalThis.__cfInspectionProbe; }
});

test('non-JSON data, getters, cycles and prototype-bearing objects fail without invocation', () => {
  for (const value of [NaN, Infinity, undefined, () => 1, new Date(), 1n]) {
    const c = base(); c.source = value; fails(C.validateCustomFunction(c), 'CF_NON_JSON');
  }
  const c = base(); let called = false;
  Object.defineProperty(c, 'source', { enumerable: true, get() { called = true; throw new Error('getter ran'); } });
  fails(C.validateCustomFunction(c), 'CF_NON_JSON'); assert.equal(called, false);
  const cycle = base(); cycle.source = cycle; fails(C.validateCustomFunction(cycle), 'CF_NON_JSON');
  const sparse = base(); sparse.inputs = Array(1); fails(C.validateCustomFunction(sparse), 'CF_NON_JSON');
});

test('malformed nested fields return diagnostics instead of throwing or silently dropping data', () => {
  for (const field of ['parameters', 'inputs', 'outputs', 'dependencies']) {
    for (const value of [null, {}, 3, [null], ['wrong']]) { const c = base(); c[field] = value; assert.equal(C.validateCustomFunction(c).ok, false); }
  }
  const c = base(); c.soruce = c.source; fails(C.validateCustomFunction(c), 'CF_UNKNOWN_FIELD');
  const extra = base(); extra.outputs[0].variadic = true; fails(C.validateCustomFunction(extra), 'CF_UNKNOWN_FIELD');
});

test('stable IDs are unique per direction/parameter namespace and reject reserved keys', () => {
  for (const id of ['', '0', 'name with spaces', '__proto__', 'constructor', 'prototype', 'x'.repeat(65)]) {
    const c = base(); c.outputs[0].id = id; fails(C.validateCustomFunction(c), 'CF_INVALID_ID');
  }
  for (const field of ['parameters', 'outputs']) { const c = base(); c[field].push(copy(c[field][0])); fails(C.validateCustomFunction(c), 'CF_DUPLICATE_ID'); }
  const c = base(); c.inputs = [p('body')]; success(C.validateCustomFunction(c)); // distinct direction namespace
});

test('parameter defaults, values, ranges, enums, vector3 and safe integers are validated', () => {
  const c = base();
  assert.deepEqual(success(C.validateParameters(c)), { width: 3 });
  c.parameterValues = {}; assert.deepEqual(success(C.validateParameters(c)), { width: 2 });
  c.parameterValues.width = 200; fails(C.validateCustomFunction(c), 'CF_PARAMETER_VALUE');
  c.parameterValues.width = '2'; fails(C.validateParameters(c), 'CF_PARAMETER_VALUE');
  c.parameterValues = {}; c.parameters[0].default = -1; fails(C.validateCustomFunction(c), 'CF_DEFAULT');
  const vector = base(); vector.parameters = [{ id: 'v', label: 'Vector', type: 'vector3', required: true, default: [1, 2, 3] }]; vector.parameterValues = {};
  assert.deepEqual(success(C.validateParameters(success(C.validateCustomFunction(vector)))), { v: [1, 2, 3] });
  vector.parameters[0].default = [1, 2]; fails(C.validateCustomFunction(vector), 'CF_DEFAULT');
  const integer = base(); integer.parameters[0].type = 'integer'; integer.parameters[0].default = Number.MAX_SAFE_INTEGER + 1; fails(C.validateCustomFunction(integer), 'CF_DEFAULT');
});

test('invalid constraints and undeclared parameter values are rejected', () => {
  for (const constraints of [{ minimum: 5, maximum: 1 }, { enum: [] }, { enum: [1, 1] }, { enum: ['wrong'] }, { minItems: 1 }, { minimum: '0' }]) {
    const c = base(); c.parameters[0].constraints = constraints; fails(C.validateCustomFunction(c), 'CF_CONSTRAINT');
  }
  const c = base(); c.parameterValues.other = 1;
  const result = C.validateCustomFunction(c); fails(result, 'CF_UNDECLARED_PARAMETER');
  assert.equal(result.diagnostics[0].parameterId, 'other');
});

test('missing required parameters are savable drafts but a pre-Cook diagnostic', () => {
  const c = base(); delete c.parameters[0].default; c.parameterValues = {};
  success(C.validateCustomFunction(c)); fails(C.validateParameters(c), 'CF_MISSING_PARAMETER');
});

test('defaults are permitted only for typed Param inputs, never outputs/native handles', () => {
  const c = base(); c.inputs = [p('scale', { pinType: 'Param', valueType: 'number', default: 2 })];
  success(C.validateCustomFunction(c)); assert.deepEqual(success(C.validatePortValues(c, 'input', {}, inspect)), { scale: 2 });
  c.inputs[0].default = '2'; fails(C.validateCustomFunction(c), 'CF_DEFAULT');
  c.inputs = [p('geometry', { default: {} })]; fails(C.validateCustomFunction(c), 'CF_DEFAULT');
  c.inputs = []; c.outputs[1].default = 0; fails(C.validateCustomFunction(c), 'CF_DEFAULT');
});

test('many/default constraints distinguish vector payloads from collections', () => {
  const c = base(); c.inputs = [p('vectors', { pinType: 'Param', valueType: 'vector3', cardinality: 'many', default: [[1, 2, 3]], constraints: { minItems: 1, maxItems: 2 } })];
  success(C.validateCustomFunction(c)); success(C.validatePortValues(c, 'input', {}, inspect));
  c.inputs[0].default = [1, 2, 3]; fails(C.validateCustomFunction(c), 'CF_DEFAULT');
  c.inputs[0].default = []; fails(C.validateCustomFunction(c), 'CF_DEFAULT');
  c.inputs[0].constraints.maxItems = 0; fails(C.validateCustomFunction(c), 'CF_CONSTRAINT');
});

test('primary output must reference a required single output, not its label or index', () => {
  for (const primaryOutput of ['Body', '0', 'missing']) { const c = base(); c.primaryOutput = primaryOutput; fails(C.validateCustomFunction(c), 'CF_PRIMARY_OUTPUT'); }
  const c = base(); c.outputs[0].cardinality = 'many'; fails(C.validateCustomFunction(c), 'CF_PRIMARY_OUTPUT');
  c.primaryOutput = null; success(C.validateCustomFunction(c));
  c.outputs = []; fails(C.validateCustomFunction(c), 'CF_OUTPUTS');
});

test('dependency metadata is content-pinned and does not load code or resources', () => {
  const c = base(); c.dependencies = [{ id: 'material', kind: 'resource', reference: 'asset:wood', version: '1.2.3', contentHash: `sha256:${'a'.repeat(64)}` }];
  success(C.validateCustomFunction(c));
  c.dependencies[0].contentHash = 'latest'; fails(C.validateCustomFunction(c), 'CF_DEPENDENCY');
  c.dependencies[0].contentHash = `sha256:${'a'.repeat(64)}`; c.dependencies.push(copy(c.dependencies[0])); fails(C.validateCustomFunction(c), 'CF_DEPENDENCY');
});

test('missing inputs and wrong values/cardinality have node- and port-specific diagnostics', () => {
  const c = base(); c.inputs = [p('mesh'), p('parts', { cardinality: 'many' })];
  const missing = C.validatePortValues(c, 'input', {}, inspect, adapter.compatibleTypes, 'script1');
  fails(missing, 'CF_MISSING_INPUT');
  assert.deepEqual(missing.diagnostics.map((d) => [d.nodeId, d.portId, d.direction]), [['script1', 'mesh', 'input'], ['script1', 'parts', 'input']]);
  const wrong = C.validatePortValues(c, 'input', { mesh: handle(), parts: handle() }, inspect);
  fails(wrong, 'CF_PORT_VALUE'); assert.equal(wrong.diagnostics[0].portId, 'parts');
  success(C.validatePortValues(c, 'input', { mesh: handle(), parts: [handle(), handle()] }, inspect));
});

test('fake/dead/wrong native handles never pass by exposing a pinType tag', () => {
  for (const body of [{ pinType: 'SpatialGeometry' }, handle('Texture'), null, undefined, [handle()]]) {
    fails(C.validatePortValues(base(), 'output', { body, count: 1 }, inspect), 'CF_PORT_VALUE');
  }
  fails(C.validatePortValues(base(), 'output', { body: handle(), count: 1 }, () => { throw new Error('released'); }), 'CF_PORT_VALUE');
});

test('all output validation is atomic: undeclared, missing and wrong outputs expose no partial map', () => {
  fails(C.validatePortValues(base(), 'output', { body: handle() }, inspect), 'CF_MISSING_OUTPUT');
  fails(C.validatePortValues(base(), 'output', { body: handle(), count: '1' }, inspect), 'CF_PORT_VALUE');
  fails(C.validatePortValues(base(), 'output', { body: handle(), count: 1, undeclared: handle() }, inspect), 'CF_UNDECLARED_OUTPUT');
  const outputs = { body: handle(), count: 1 };
  assert.deepEqual(success(C.validatePortValues(base(), 'output', outputs, inspect)), outputs);
});

test('optional outputs may be omitted, but explicit undefined is an invalid value', () => {
  const c = base(); c.outputs[1].required = false;
  success(C.validatePortValues(c, 'output', { body: handle() }, inspect));
  fails(C.validatePortValues(c, 'output', { body: handle(), count: undefined }, inspect), 'CF_PORT_VALUE');
});

test('save/reopen preserves the ordinary graph envelope, source, declarations and parameters', () => {
  const graph = wired(); const text = C.canonicalJson(graph); const reopened = JSON.parse(text);
  assert.deepEqual(reopened, graph);
  success(C.validateCustomFunction(reopened.nodes[0].data.customFunction));
  assert.deepEqual(G.validateCustomFunctionConnections(reopened, adapter), []);
  assert.deepEqual(Object.keys(reopened.edges[0]).sort(), ['id', 'source', 'sourceHandle', 'target', 'targetHandle']);
});

test('label rename/reorder keeps stable handles, IDs, edges and source untouched', () => {
  const graph = wired(); const next = base(); next.outputs.reverse(); next.outputs[1].label = 'Renamed shell'; next.parameters[0].label = 'Width in metres';
  const tx = success(G.planCustomFunctionEdit(graph, 'script1', next, adapter));
  assert.deepEqual(tx.after.edges, graph.edges); assert.deepEqual(tx.removedEdgeIds, []);
  assert.equal(tx.after.nodes[0].data.customFunction.source, base().source);
  assert.deepEqual(graph, wired());
});

test('port removal rejects by default; explicit removal and Undo/Redo are atomic', () => {
  const graph = wired(); const next = base(); next.outputs = next.outputs.filter((port) => port.id !== 'body'); next.primaryOutput = 'count';
  fails(G.planCustomFunctionEdit(graph, 'script1', next, adapter), 'CF_UNKNOWN_PORT');
  assert.deepEqual(graph, wired());
  const result = G.planCustomFunctionEdit(graph, 'script1', next, adapter, 'remove'); const tx = success(result);
  assert.deepEqual(tx.removedEdgeIds, ['body-edge']); assert.deepEqual(tx.after.edges.map((e) => e.id), ['count-edge']);
  assert.ok(result.diagnostics.every((d) => d.severity === 'warning'));
  const undone = success(G.replayCustomFunctionEdit(tx.after, tx, 'undo')); assert.deepEqual(undone, graph);
  const redone = success(G.replayCustomFunctionEdit(undone, tx, 'redo')); assert.deepEqual(redone, tx.after);
});

test('incompatible type changes prune only affected incident edges, not the other output', () => {
  const graph = wired(); const next = base(); next.outputs[0].pinType = 'Texture';
  fails(G.planCustomFunctionEdit(graph, 'script1', next, adapter), 'CF_EDGE_TYPE');
  const tx = success(G.planCustomFunctionEdit(graph, 'script1', next, adapter, 'remove'));
  assert.deepEqual(tx.removedEdgeIds, ['body-edge']); assert.equal(tx.after.edges[0].id, 'count-edge');
});

test('many-to-single edits never keep an arbitrary competing edge', () => {
  const c = base(); c.inputs = [p('parts', { cardinality: 'many' })];
  const graph = { nodes: [cf('script1', c), ordinary('a', 'GeometrySource'), ordinary('b', 'GeometrySource')], edges: [edge('a-edge', 'a', 'out', 'script1', 'parts'), edge('b-edge', 'b', 'out', 'script1', 'parts')] };
  assert.deepEqual(G.validateCustomFunctionConnections(graph, adapter), []);
  const next = copy(c); next.inputs[0].cardinality = 'single';
  fails(G.planCustomFunctionEdit(graph, 'script1', next, adapter), 'CF_EDGE_CARDINALITY');
  const tx = success(G.planCustomFunctionEdit(graph, 'script1', next, adapter, 'remove'));
  assert.deepEqual(tx.removedEdgeIds, ['a-edge', 'b-edge']); assert.equal(tx.after.edges.length, 0);
  assert.ok(G.validateCustomFunctionConnections(tx.after, adapter).some((d) => d.code === 'CF_MISSING_INPUT'));
  assert.deepEqual(success(G.replayCustomFunctionEdit(tx.after, tx, 'undo')), graph);
});

test('many output to single input is diagnosed at the target port', () => {
  const graph = wired(); const c = graph.nodes[0].data.customFunction; c.outputs[0].cardinality = 'many'; c.primaryOutput = null;
  const result = G.validateCustomFunctionConnections(graph, adapter);
  assert.ok(result.some((d) => d.code === 'CF_EDGE_CARDINALITY' && d.portId === 'in' && d.edgeId === 'body-edge'));
});

test('cached edge type annotations are never authoritative and are refreshed on edits', () => {
  const graph = wired(); graph.edges[0].sourcePinType = 'Texture'; graph.edges[0].targetPinType = 'Texture';
  const tx = success(G.planCustomFunctionEdit(graph, 'script1', base(), adapter));
  assert.equal(tx.after.edges[0].sourcePinType, 'SpatialGeometry'); assert.equal(tx.after.edges[0].targetPinType, 'SpatialGeometry');
});

test('missing handles, duplicate connections, duplicate IDs and stale transactions fail', () => {
  const graph = wired(); delete graph.edges[0].sourceHandle;
  assert.ok(G.validateCustomFunctionConnections(graph, adapter).some((d) => d.code === 'CF_UNKNOWN_PORT'));
  const duplicate = wired(); duplicate.edges.push({ ...duplicate.edges[0], id: 'duplicate' });
  assert.ok(G.validateCustomFunctionConnections(duplicate, adapter).some((d) => d.code === 'CF_DUPLICATE_EDGE'));
  duplicate.edges[2].id = duplicate.edges[0].id;
  assert.ok(G.validateCustomFunctionConnections(duplicate, adapter).some((d) => d.code === 'CF_EDGE_ID'));
  const tx = success(G.planCustomFunctionEdit(wired(), 'script1', base(), adapter)); const current = copy(tx.after); current.nodes[0].position.x++;
  fails(G.replayCustomFunctionEdit(current, tx, 'undo'), 'CF_STALE_TRANSACTION');
});

test('invalid incoming contracts/policies fail before any graph mutation', () => {
  const graph = wired(); const invalid = base(); invalid.apiVersion = '999';
  fails(G.planCustomFunctionEdit(graph, 'script1', invalid, adapter, 'remove'), 'CF_UNSUPPORTED_API_VERSION');
  fails(G.planCustomFunctionEdit(graph, 'missing', base(), adapter), 'CF_NODE_NOT_FOUND');
  fails(G.planCustomFunctionEdit(graph, 'script1', base(), adapter, 'silent'), 'CF_EDIT_POLICY');
  assert.deepEqual(graph, wired());
});

test('copy/paste remaps node/edge/graph-parameter IDs and targets, never local port IDs or source', () => {
  const graph = wired(); graph.nodes[0].data.customFunction.source += '// literal script1 is not a graph reference\n';
  const result = success(G.copyContractSelection(graph, ['script1', 'sink'], (kind, id) => `${kind}-copy-${id}`));
  assert.equal(result.nodes[0].id, 'node-copy-script1');
  assert.equal(result.edges.length, 1); assert.equal(result.edges[0].sourceHandle, 'body'); assert.equal(result.edges[0].source, 'node-copy-script1'); assert.equal(result.edges[0].target, 'node-copy-sink');
  assert.equal(result.parameters[0].targetNode, 'node-copy-sink'); assert.equal(result.parameters[0].id, 'parameter-copy-graph-width');
  assert.deepEqual(result.nodes[0].data, graph.nodes[0].data);
  result.nodes[0].data.customFunction.parameterValues.width = 99; assert.equal(graph.nodes[0].data.customFunction.parameterValues.width, 3);
});

test('single-node duplication excludes external wires and rejects colliding identities', () => {
  const graph = wired();
  const duplicate = success(G.copyContractSelection(graph, ['script1'], (_, id) => `copy-${id}`));
  assert.equal(duplicate.edges.length, 0); assert.deepEqual(duplicate.nodes[0].data, graph.nodes[0].data);
  fails(G.copyContractSelection(graph, ['script1'], () => 'sink'), 'CF_COPY_ID');
  fails(G.copyContractSelection(graph, ['script1', 'script1'], (_, id) => `copy-${id}`), 'CF_SELECTION');
  assert.deepEqual(graph, wired());
});

test('presets roundtrip and apply through the same explicit undoable port transaction', () => {
  const preset = success(G.createCustomFunctionPreset(base())); const reopened = success(C.parseCustomFunction(JSON.stringify(preset)));
  reopened.outputs = [reopened.outputs[1]]; reopened.primaryOutput = 'count'; reopened.parameterValues.width = 4;
  fails(G.planCustomFunctionEdit(wired(), 'script1', reopened, adapter), 'CF_UNKNOWN_PORT');
  const tx = success(G.planCustomFunctionEdit(wired(), 'script1', reopened, adapter, 'remove'));
  assert.equal(tx.after.nodes[0].data.customFunction.parameterValues.width, 4);
  assert.deepEqual(success(G.replayCustomFunctionEdit(tx.after, tx, 'undo')), wired());
});

test('subgraph definition reuse preserves shared identity; edits are scoped, not root-ID based', () => {
  const definition = { id: 'reusable', name: 'Reusable script', inputs: [], outputs: [{ id: 'out', name: 'Out', pinType: 'SpatialGeometry' }], ...wired() };
  delete definition.version;
  delete definition.parameters;
  definition.nodes.find((node) => node.id === 'sink').type = 'Output';
  const document = { ...fixture, nodes: [cf('script1'), { id: 'instance-a', type: 'Subgraph', position: { x: 0, y: 0 }, data: { subgraphId: 'reusable' } }, { id: 'instance-b', type: 'Subgraph', position: { x: 0, y: 100 }, data: { subgraphId: 'reusable' } }], subgraphs: [definition] };
  const reopened = JSON.parse(C.canonicalJson(document));
  const next = base(); next.outputs[0].label = 'Subgraph-only label';
  const tx = success(G.planCustomFunctionEdit(reopened.subgraphs[0], 'script1', next, adapter));
  reopened.subgraphs[0] = tx.after;
  assert.equal(reopened.nodes[0].data.customFunction.outputs[0].label, 'Body');
  assert.equal(reopened.subgraphs[0].nodes[0].data.customFunction.outputs[0].label, 'Subgraph-only label');
  const copied = success(G.copyContractSelection(reopened, ['instance-a'], (_, id) => `copy-${id}`));
  assert.equal(copied.nodes[0].data.subgraphId, 'reusable'); assert.equal(reopened.subgraphs.length, 1);
});

test('cache descriptor covers every persisted contract field, and canonicalizes object key order', () => {
  const original = base(); const descriptor = C.customFunctionCacheDescriptor(original);
  for (const mutate of [
    (c) => c.source += '\n// changed', (c) => c.seed++, (c) => c.parameterValues.width++,
    (c) => c.parameters[0].default++, (c) => c.parameters[0].constraints.maximum++,
    (c) => c.inputs.push(p('optional', { required: false })), (c) => c.outputs[0].label = 'New label',
    (c) => c.outputs.reverse(), (c) => c.primaryOutput = 'count', (c) => c.apiVersion = 'future',
    (c) => c.dependencies.push({ id: 'r', kind: 'resource', reference: 'asset:1', version: '1', contentHash: `sha256:${'b'.repeat(64)}` }),
  ]) { const c = base(); mutate(c); assert.notEqual(C.customFunctionCacheDescriptor(c), descriptor); }
  assert.equal(C.customFunctionCacheDescriptor(Object.fromEntries(Object.entries(original).reverse())), descriptor);
});

test('manifest port adapter maps variadic metadata without inventing a special edge format', () => {
  assert.deepEqual(G.manifestInputPortContract({ pinType: 'SpatialGeometry', variadic: true }), { pinType: 'SpatialGeometry', cardinality: 'many' });
  assert.deepEqual(G.manifestInputPortContract({ pinType: 'Param' }), { pinType: 'Param', cardinality: 'single' });
  const ports = G.getCustomFunctionPorts(base(), 'output'); ports[0].label = 'mutated'; assert.equal(base().outputs[0].label, 'Body');
});


test('value-map accessors cannot run during parameter/output validation', () => {
  let invoked = false;
  const values = {};
  Object.defineProperty(values, 'body', { enumerable: true, get() { invoked = true; return handle(); } });
  fails(C.validatePortValues(base(), 'output', values, inspect), 'CF_VALUE_MAP');
  fails(C.validateParameters(base(), values), 'CF_PARAMETERS');
  assert.equal(invoked, false);
});

test('an explicitly invalid bound input never falls back to its default', () => {
  const c = base(); c.inputs = [p('size', { pinType: 'Param', valueType: 'number', default: 2 })];
  fails(C.validatePortValues(c, 'input', { size: undefined }, inspect), 'CF_PORT_VALUE');
});

const sharedCases = JSON.parse(readFileSync(join(root, 'schema/fixtures/custom-function-cases.json'), 'utf8'));
for (const item of sharedCases) test(`shared schema/semantic corpus: ${item.name}`, () => {
  const c = base();
  for (const change of item.changes) {
    let parent = c;
    for (const key of change.path.slice(0, -1)) parent = parent[key];
    const key = change.path.at(-1);
    if (change.remove) delete parent[key];
    else parent[key] = copy(change.value);
  }
  const result = C.validateCustomFunction(c);
  assert.equal(result.ok, item.contractValid, JSON.stringify(result.diagnostics));
});


test('nested arrays cannot run getters/iterators during static or value inspection', () => {
  let invoked = false;
  const array = [1, 2, 3];
  Object.defineProperty(array, '0', { enumerable: true, get() { invoked = true; throw new Error('getter ran'); } });
  const vector = base();
  vector.parameters = [{ id: 'v', label: 'v', type: 'vector3', required: true }];
  vector.parameterValues = {};
  fails(C.validateParameters(vector, { v: array }), 'CF_PARAMETER_VALUE');
  const c = base(); c.outputs = [p('items', { cardinality: 'many' })]; c.primaryOutput = null;
  fails(C.validatePortValues(c, 'output', { items: array }, inspect), 'CF_PORT_VALUE');
  const customIterator = [handle()];
  customIterator[Symbol.iterator] = () => { invoked = true; throw new Error('iterator ran'); };
  fails(C.validatePortValues(c, 'output', { items: customIterator }, inspect), 'CF_PORT_VALUE');
  const draft = base(); draft.inputs = array;
  fails(C.validateCustomFunction(draft), 'CF_NON_JSON');
  assert.equal(invoked, false);
});

test('non-index numeric array properties cannot hide a sparse persistence hole', () => {
  const sparse = Array(1); sparse['4294967295'] = {};
  const draft = base(); draft.inputs = sparse;
  fails(C.validateCustomFunction(draft), 'CF_NON_JSON');
});
