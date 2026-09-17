#!/usr/bin/env node
'use strict';
// Exercise the production consent store without a browser or a running server.
const assert = require('node:assert/strict');
const { test } = require('node:test');
const { mkdtempSync, rmSync, existsSync, copyFileSync, writeFileSync } = require('node:fs');
const { tmpdir } = require('node:os');
const path = require('node:path');
const { execFileSync } = require('node:child_process');

const root = path.resolve(__dirname, '..');
const editor = path.join(root, 'web/pcg-editor');
const output = mkdtempSync(path.join(tmpdir(), 'pcg-mcp-control-'));
process.on('exit', () => rmSync(output, { recursive: true, force: true }));
const localTsc = path.join(editor, 'node_modules/typescript/lib/tsc.js');
// Compile an exact temporary copy in an explicit CommonJS package so both TS 5
// and TS 6 work without inheriting the editor's ESM package or project config.
const source = path.join(output, 'mcpSessionState.ts');
const compiled = path.join(output, 'compiled');
copyFileSync(path.join(editor, 'src/mcpSessionState.ts'), source);
writeFileSync(path.join(output, 'package.json'), '{"type":"commonjs"}');
const args = ['--strict', '--target', 'ES2020', '--module', 'Node16',
  '--moduleResolution', 'Node16', '--outDir', compiled, source];
execFileSync(existsSync(localTsc) ? process.execPath : 'tsc',
  existsSync(localTsc) ? [localTsc, ...args] : args,
  { cwd: output, stdio: 'inherit', shell: !existsSync(localTsc) && process.platform === 'win32' });
const { createMcpSessionControl, sessionLabel, targetInstruction } = require(path.join(compiled, 'mcpSessionState.js'));
const fixture = () => {
  const control = createMcpSessionControl();
  control.attach('12345678-first', 'props/crate.pcg', 'http://localhost:5173/');
  return control;
};

test('a page is unapproved by default, even when it is the only page', () => {
  const control = fixture();
  assert.equal(control.getSnapshot().enabled, false);
  assert.equal(control.allows('12345678-first', 0), false);
  assert.equal(control.allows('', 0), false);
});

test('approval uses the exact ID, not a short label or matching filename', () => {
  const control = fixture();
  control.setEnabled('wrong', true);
  assert.equal(control.getSnapshot().enabled, false);
  control.setEnabled('12345678-first', true);
  assert.equal(control.allows('12345678-first', 1), true);
  assert.equal(control.allows('12345678', 1), false);
  assert.equal(control.allows('other-page', 1), false);
});

test('revoke/regrant rejects a previously fetched command generation', () => {
  const control = fixture();
  control.setEnabled('12345678-first', true);
  const oldRevision = control.getSnapshot().revision;
  control.setEnabled('12345678-first', false);
  assert.equal(control.allows('12345678-first', oldRevision), false);
  control.setEnabled('12345678-first', true);
  assert.equal(control.allows('12345678-first', oldRevision), false);
  assert.equal(control.allows('12345678-first', control.getSnapshot().revision), true);
});

test('a stale server response cannot re-enable local consent', () => {
  const control = fixture();
  control.setEnabled('12345678-first', true);
  control.report('12345678-first', { aiControlEnabled: true, aiControlRevision: 1 });
  control.setEnabled('12345678-first', false);
  control.report('12345678-first', { aiControlEnabled: false, aiControlRevision: 2 });
  control.report('12345678-first', { aiControlEnabled: true, aiControlRevision: 1 });
  assert.equal(control.getSnapshot().enabled, false);
  assert.equal(control.getSnapshot().serverEnabled, false);
  assert.equal(control.getSnapshot().serverRevision, 2);
});

test('ordinary graph sync does not reset consent, but a refreshed page ID does', () => {
  const control = fixture();
  control.setEnabled('12345678-first', true);
  control.attach('12345678-first', 'props/saved.pcg', 'http://localhost:5173/');
  assert.equal(control.getSnapshot().enabled, true);
  control.attach('new-page', 'props/saved.pcg', 'http://localhost:5173/');
  assert.equal(control.getSnapshot().enabled, false);
  assert.equal(control.getSnapshot().revision, 0);
  control.report('12345678-first', { aiControlEnabled: true, aiControlRevision: 1 });
  assert.equal(control.getSnapshot().online, false);
});

test('duplicate unsaved windows have independent grants', () => {
  const first = createMcpSessionControl();
  const second = createMcpSessionControl();
  first.attach('page-A', '', 'http://localhost:5173/');
  second.attach('page-B', '', 'http://localhost:5173/');
  first.setEnabled('page-A', true);
  assert.equal(second.getSnapshot().enabled, false);
  assert.equal(first.allows('page-B', 1), false);
});

test('bridge status is distinct from consent and identical polls do not rerender', () => {
  const control = fixture();
  let notifications = 0;
  const unsubscribe = control.subscribe(() => notifications++);
  control.setEnabled('12345678-first', true);
  assert.equal(control.getSnapshot().online, false);
  const status = { aiControlEnabled: true, aiControlRevision: 1, lastMcpAccessAt: 1000 };
  control.report('12345678-first', status);
  const before = control.getSnapshot();
  control.report('12345678-first', status);
  assert.equal(control.getSnapshot(), before);
  assert.equal(notifications, 2);
  control.disconnect('12345678-first');
  assert.equal(control.getSnapshot().online, false);
  assert.equal(control.getSnapshot().serverEnabled, false);
  unsubscribe();
});

test('copyable target contains the full ID, visible document and no-fallback instruction', () => {
  const control = fixture();
  assert.equal(sessionLabel('12345678-first'), 'PICG 12345678');
  const text = targetInstruction(control.getSnapshot());
  assert.match(text, /editorSessionId="12345678-first"/);
  assert.match(text, /props\/crate\.pcg/);
  assert.match(text, /Do not choose another session/);
});
