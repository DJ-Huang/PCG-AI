// Dependency-light regression checks for the actual TypeScript modules.
// Uses the repository TypeScript dependency; no browser or running server.
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { mkdtempSync, readFileSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const require = createRequire(import.meta.url);
const ts = require('typescript');
const directory = mkdtempSync(join(tmpdir(), 'picg-review-test-'));
let count = 0;
function test(label, run) { run(); ++count; console.log(`PASS ${label}`); }
try {
  for (const name of ['previewParameters', 'reviewEvidence']) {
    const source = readFileSync(new URL(`../src/${name}.ts`, import.meta.url), 'utf8');
    const compiled = ts.transpileModule(source, {
      compilerOptions: { target: ts.ScriptTarget.ES2022, module: ts.ModuleKind.CommonJS },
      reportDiagnostics: true,
    });
    assert.equal(compiled.diagnostics?.filter(d => d.category === ts.DiagnosticCategory.Error).length, 0);
    writeFileSync(join(directory, `${name}.js`), compiled.outputText);
  }
  const { resolvePreviewParameterValues, applyPreviewParameterOverrides, savePreviewParameterDefaults,
    previewParameterDrift } = require(join(directory, 'previewParameters.js'));
  const { requireTrustworthyCook, requireSynchronizedParameters } = require(join(directory, 'reviewEvidence.js'));
  const parameter = { id: 'width', name: 'Width', type: 'number', default: 0.7, exposed: true,
    targetNode: 'box', targetProperty: 'width', hasRange: false, min: 0, max: 1 };
  const makeGraph = () => ({ version: '1.0', nodes: [{ id: 'box', type: 'CreateBoxMesh',
    position: { x: 0, y: 0 }, data: { width: 0.7 } }], edges: [], parameters: [{ ...parameter }] });
  test('effective graph and defaults change together without mutating source', () => {
    const graph = makeGraph(); const next = applyPreviewParameterOverrides(graph, { width: 0.8 });
    assert.equal(next.nodes[0].data.width, 0.8); assert.equal(next.parameters[0].default, 0.8);
    assert.equal(graph.nodes[0].data.width, 0.7); assert.equal(graph.parameters[0].default, 0.7);
    assert.deepEqual(previewParameterDrift(next), []);
  });
  test('saved source/default drift blocks final review', () => {
    const graph = makeGraph(); graph.parameters[0].default = 0.66;
    assert.equal(previewParameterDrift(graph).length, 1);
    assert.throws(() => requireSynchronizedParameters(graph), /default/);
    const saved = savePreviewParameterDefaults(graph.nodes, graph.parameters, { width: 0.7 });
    requireSynchronizedParameters({ ...graph, ...saved });
  });
  for (const value of [NaN, Infinity, -Infinity, true, '0.7', null]) {
    test(`reject invalid number ${String(value)}`, () => {
      assert.throws(() => resolvePreviewParameterValues([parameter], { width: value }), /Invalid/);
      assert.throws(() => applyPreviewParameterOverrides(makeGraph(), { width: value }), /Invalid/);
    });
  }
  test('reject fractional integer', () => assert.throws(() =>
    resolvePreviewParameterValues([{ ...parameter, type: 'integer' }], { width: 1.5 }), /Invalid/));
  test('reject malformed vector', () => assert.throws(() =>
    resolvePreviewParameterValues([{ ...parameter, type: 'vector3' }], { width: [1, 2, NaN] }), /Invalid/));
  test('vector copies cannot mutate source or values', () => {
    const p = { ...parameter, type: 'vector3', default: [1, 2, 3] };
    const values = resolvePreviewParameterValues([p]); values.width[0] = 9;
    assert.equal(p.default[0], 1);
    const result = applyPreviewParameterOverrides({ ...makeGraph(), parameters: [p] }, values);
    result.parameters[0].default[0] = 8;
    assert.equal(result.nodes[0].data.width[0], 9); assert.equal(values.width[0], 9);
  });
  test('duplicate binding is not last-write-wins', () => {
    const graph = makeGraph(); graph.parameters.push({ ...parameter, id: 'duplicate' });
    assert.throws(() => applyPreviewParameterOverrides(graph, { width: 1, duplicate: 2 }), /Multiple/);
  });
  test('orphan target is not silently ignored', () => {
    const graph = makeGraph(); graph.parameters[0].targetNode = 'gone';
    assert.throws(() => applyPreviewParameterOverrides(graph, { width: 1 }), /missing node/);
  });
  test('duplicate parameter IDs rejected', () => assert.throws(() =>
    resolvePreviewParameterValues([parameter, parameter]), /Duplicate/));
  test('prototype-like IDs remain own properties', () => {
    const values = resolvePreviewParameterValues([{ ...parameter, id: '__proto__' }]);
    assert(Object.hasOwn(values, '__proto__')); assert.equal(values.__proto__, 0.7);
  });
  const success = { diagnostics_version: 1, cook_outcome: 'success', fallback_used: false,
    execution_acceptable: true, node_diagnostics: [{ node_id: 'bool', outcome: 'empty', fallback_used: false }] };
  test('valid empty is execution-acceptable, not degraded', () => requireTrustworthyCook(JSON.stringify(success), 0));
  test('old server cannot certify a final export', () => assert.throws(() => requireTrustworthyCook('{}', 0), /blocked/));
  test('transport success is insufficient', () => assert.throws(() =>
    requireTrustworthyCook(JSON.stringify({ ...success, cook_outcome: 'degraded' }), 0), /blocked/));
  test('an earlier loop fallback cannot be hidden by final success', () => assert.throws(() =>
    requireTrustworthyCook(JSON.stringify({ ...success, node_diagnostics: [
      { node_id: 'bool', outcome: 'degraded', fallback_used: true }, ...success.node_diagnostics] }), 0), /blocked/));
  test('hard failure blocks export despite success-like JSON', () => assert.throws(() =>
    requireTrustworthyCook(JSON.stringify(success), -1), /blocked/));
  test('malformed diagnostics block export', () => assert.throws(() =>
    requireTrustworthyCook(JSON.stringify({ ...success, node_diagnostics: [null] }), 0), /Invalid/));
  console.log(`${count} review/parameter regression checks passed.`);
} finally {
  rmSync(directory, { recursive: true, force: true });
}
