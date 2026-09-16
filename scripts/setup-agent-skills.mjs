#!/usr/bin/env node
/**
 * PCG-AI skills setup:
 *   Skills -> junction/symlink .agents/skills/* to user profile skill dirs
 *   Codely CLI -> junction .agents/ to ~/.codely-cli/extensions/picg-extension/
 */
import fs from 'node:fs';
import path from 'node:path';
import readline from 'node:readline';
import { fileURLToPath } from 'node:url';
import { linkAgentSkillsForTools, unlinkAgentSkillsForTools } from './link-agent-skills.mjs';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const SOURCE_ROOT = path.resolve(__dirname, '..');
const EXTENSION_ROOT = path.join(SOURCE_ROOT, '.agents');
const SKILLS_SRC = path.join(EXTENSION_ROOT, 'skills');

/** @typedef {'cursor' | 'trae' | 'codely' | 'kimi' | 'opencode' | 'codex'} ToolId */

const ALL_TOOLS = /** @type {ToolId[]} */ ([
  'cursor',
  'trae',
  'codely',
  'kimi',
  'opencode',
  'codex',
]);

function ask(rl, question) {
  return new Promise((resolve) => rl.question(question, resolve));
}

function pathExists(p) {
  try {
    fs.accessSync(p);
    return true;
  } catch {
    return false;
  }
}

async function promptYesNo(rl, label, defaultYes = true) {
  const hint = defaultYes ? 'Y/n' : 'y/N';
  const raw = (await ask(rl, `${label}? (${hint}): `)).trim().toLowerCase();
  if (!raw) return defaultYes;
  return raw === 'y' || raw === 'yes' || raw === '是';
}

async function promptTools(rl) {
  console.log('\nWhich tools? (comma-separated, e.g. 1,2,3)');
  console.log('  1 = Cursor');
  console.log('  2 = Trae');
  console.log('  3 = Codely CLI');
  console.log('  4 = Kimi Code');
  console.log('  5 = OpenCode');
  console.log('  6 = All (1-7)');
  console.log('  7 = Codex');
  const raw = (await ask(rl, 'Tools [6]: ')).trim() || '6';
  const tools = new Set();
  for (const part of raw.split(/[,，\s]+/)) {
    const t = part.trim();
    if (t === '1' || /^cursor$/i.test(t)) tools.add('cursor');
    if (t === '2' || /^trae$/i.test(t)) tools.add('trae');
    if (t === '3' || /^codely$/i.test(t)) tools.add('codely');
    if (t === '4' || /^kimi$/i.test(t)) tools.add('kimi');
    if (t === '5' || /^opencode$/i.test(t)) tools.add('opencode');
    if (t === '7' || /^codex$/i.test(t)) tools.add('codex');
    if (t === '6' || /^all$/i.test(t)) {
      for (const tool of ALL_TOOLS) tools.add(tool);
    }
  }
  if (tools.size === 0) {
    for (const tool of ALL_TOOLS) tools.add(tool);
  }
  return tools;
}

function parseCli(argv) {
  const out = {
    mode: 'sync',
    tools: null,
    once: false,
    help: false,
  };
  for (let i = 0; i < argv.length; i += 1) {
    const a = argv[i];
    if (a === 'unlink') out.mode = 'unlink';
    else if (a === '--tools') {
      out.tools = new Set();
      for (const part of (argv[++i] || '').split(/[,，]/)) {
        const t = part.trim().toLowerCase();
        if (t === '1' || t === 'cursor') out.tools.add('cursor');
        if (t === '2' || t === 'trae') out.tools.add('trae');
        if (t === '3' || t === 'codely') out.tools.add('codely');
        if (t === '4' || t === 'kimi') out.tools.add('kimi');
        if (t === '5' || t === 'opencode') out.tools.add('opencode');
        if (t === '7' || t === 'codex') out.tools.add('codex');
        if (t === '6' || t === 'all') {
          for (const tool of ALL_TOOLS) out.tools.add(tool);
        }
      }
    } else if (a === '--once') out.once = true;
    else if (a === '--help' || a === '-h') out.help = true;
  }
  return out;
}

function printHelp() {
  console.log(`Usage:
  .agents/setup.bat
  .agents/setup.bat unlink
  node scripts/setup-agent-skills.mjs --tools cursor,trae,codely --once

Skills source: .agents/skills/
Codely CLI: junction .agents/ -> ~/.codely-cli/extensions/picg-extension/
Other tools: junction each skill dir into ~/.{tool}/skills/

Options:
  --once   Run once and exit (no interactive loop)
  unlink   Remove links pointing to this repo's .agents/
`);
}

function runSync(tools) {
  console.log('\n------------------------------------------------------------');
  console.log(`Tools:  ${[...tools].join(', ')}`);
  console.log(`Source: ${SKILLS_SRC}`);
  console.log('------------------------------------------------------------');

  const total = linkAgentSkillsForTools([...tools], EXTENSION_ROOT, SKILLS_SRC);
  console.log(`\n[OK] Linked ${total} entr${total === 1 ? 'y' : 'ies'}.`);
  console.log('Restart IDE to pick up updated skills.');
  return total;
}

function runUnlink(tools) {
  console.log('\n------------------------------------------------------------');
  console.log(`Tools:  ${[...tools].join(', ')}`);
  console.log(`Repo:   ${EXTENSION_ROOT}`);
  console.log('------------------------------------------------------------');

  const total = unlinkAgentSkillsForTools([...tools], EXTENSION_ROOT, SKILLS_SRC);
  console.log(`\n[OK] Removed ${total} link${total === 1 ? '' : 's'}.`);
  return total;
}

async function main() {
  const cli = parseCli(process.argv.slice(2));
  if (cli.help) {
    printHelp();
    return;
  }

  if (!pathExists(SKILLS_SRC)) {
    console.error(`[ERROR] Missing skills source: ${SKILLS_SRC}`);
    process.exit(1);
  }

  console.log('============================================================');
  console.log(`  PCG-AI Skills Setup  [${cli.mode}]`);
  console.log(`  Repo:   ${SOURCE_ROOT}`);
  console.log(`  Extension: ${EXTENSION_ROOT}`);
  console.log(`  Skills:    ${SKILLS_SRC}`);
  console.log('============================================================');

  const rl = readline.createInterface({ input: process.stdin, output: process.stdout });
  const tools = cli.tools ?? (await promptTools(rl));
  rl.close();

  if (cli.mode === 'unlink') {
    runUnlink(tools);
    return;
  }

  runSync(tools);
}

main().catch((err) => {
  console.error('[FAILED]', err.message || err);
  process.exit(1);
});
