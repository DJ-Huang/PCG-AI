#!/usr/bin/env node
// Focused regression for malformed persisted dependency IDs. This compiles the
// actual TypeScript contract, then verifies validation returns diagnostics rather
// than throwing while coercing attacker-controlled JSON values.
import assert from 'node:assert/strict';
import { after, test } from 'node:test';
import { execFileSync } from 'node:child_process';
import { existsSync, mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createRequire } from 'node:module';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const temporary = mkdtempSync(join(tmpdir(), 'pcg-custom-function-malformed-dependency-'));
after(() => rmSync(temporary, { recursive: true, force: true }));

const compiler = join(root, 'web/pcg-editor/node_modules/typescript/bin/tsc');
const source = join(root, 'web/pcg-editor/src/customFunctionContract.ts');
const args = [
  '--strict',
  '--noUnusedLocals',
  '--noUnusedParameters',
  '--target', 'ES2022',
  '--module', 'commonjs',
  '--lib', 'ES2022',
  '--outDir', temporary,
  source,
];

if (existsSync(compiler)) execFileSync(process.execPath, [compiler, ...args], { stdio: 'pipe' });
else execFileSync('tsc', args, { stdio: 'pipe' });

const require = createRequire(import.meta.url);
const contractModule = require(join(temporary, 'customFunctionContract.js'));
const fixture = JSON.parse(readFileSync(join(root, 'schema/fixtures/custom-function-v1.pcg'), 'utf8'));
const base = () => JSON.parse(JSON.stringify(fixture.nodes[0].data.customFunction));

test('malformed dependency IDs return CF_DEPENDENCY instead of throwing during string coercion', () => {
  const contract = base();
  contract.dependencies = [{
    id: JSON.parse('{"toString":"x"}'),
    kind: 'resource',
    reference: 'asset:wood',
    version: '1.2.3',
    contentHash: `sha256:${'a'.repeat(64)}`,
  }];

  let result;
  assert.doesNotThrow(() => {
    result = contractModule.validateCustomFunction(contract, 'script1');
  });
  assert.equal(result.ok, false);
  assert.ok(result.diagnostics.some((diagnostic) =>
    diagnostic.code === 'CF_DEPENDENCY' &&
    diagnostic.path === '/dependencies/0/id' &&
    diagnostic.nodeId === 'script1'));
});
