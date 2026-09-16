import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';

const SKILL_SKIP = new Set(['shared']);
const EXTENSION_DIR = 'picg-extension';
const LEGACY_CODELY_EXT_NAMES = ['pcg-extensions'];

const USER_HOME = process.env.USERPROFILE || process.env.HOME || '';

/** @typedef {'cursor' | 'trae' | 'codely' | 'kimi' | 'opencode' | 'codex'} ToolId */

export function getSkillLinkDestRoots(tool) {
  if (!USER_HOME) return [];
  if (tool === 'cursor') return [path.join(USER_HOME, '.cursor', 'skills')];
  if (tool === 'trae') {
    return [
      path.join(USER_HOME, '.trae', 'skills'),
      path.join(USER_HOME, '.trae-cn', 'skills'),
    ];
  }
  if (tool === 'kimi') return [path.join(USER_HOME, '.kimi-code', 'skills')];
  if (tool === 'opencode') return [path.join(USER_HOME, '.config', 'opencode', 'skills')];
  if (tool === 'codex') return [path.join(USER_HOME, '.codex', 'skills')];
  return [];
}

export function getCodelyExtensionLinkPath() {
  return path.join(USER_HOME, '.codely-cli', 'extensions', EXTENSION_DIR);
}

function pathEntryExists(p) {
  try {
    fs.lstatSync(p);
    return true;
  } catch (err) {
    if (err.code === 'ENOENT') return false;
    throw err;
  }
}

function isReparsePoint(p) {
  if (!pathEntryExists(p)) return false;
  if (process.platform !== 'win32') return fs.lstatSync(p).isSymbolicLink();
  const r = spawnSync('cmd', ['/c', 'fsutil', 'reparsepoint', 'query', p], { stdio: 'ignore' });
  return r.status === 0;
}

function removeForReplace(p) {
  if (!pathEntryExists(p)) return;
  if (isReparsePoint(p) || fs.lstatSync(p).isSymbolicLink()) {
    const stat = fs.lstatSync(p);
    if (stat.isDirectory()) fs.rmSync(p, { recursive: true, force: true });
    else fs.unlinkSync(p);
    return;
  }
  const stat = fs.statSync(p);
  if (stat.isDirectory()) fs.rmSync(p, { recursive: true, force: true });
  else fs.unlinkSync(p);
}

function linkDirectory(link, target) {
  const absTarget = path.resolve(target);
  if (process.platform === 'win32') {
    const r = spawnSync('cmd', ['/c', 'mklink', '/J', link, absTarget], { encoding: 'utf8' });
    if (r.status !== 0) {
      throw new Error(`mklink /J failed: ${link}\n${r.stderr || r.stdout}`);
    }
    return;
  }
  fs.symlinkSync(absTarget, link, 'dir');
}

function linkFile(link, target) {
  const absTarget = path.resolve(target);
  if (process.platform === 'win32') {
    let r = spawnSync('cmd', ['/c', 'mklink', '/H', link, absTarget], { stdio: 'ignore' });
    if (r.status !== 0) {
      r = spawnSync('cmd', ['/c', 'mklink', link, absTarget], { stdio: 'ignore' });
      if (r.status !== 0) fs.copyFileSync(absTarget, link);
    }
    return;
  }
  fs.symlinkSync(absTarget, link);
}

function linkSkillEntries(skillsSrc, destRoot, log) {
  fs.mkdirSync(destRoot, { recursive: true });
  let count = 0;
  for (const name of fs.readdirSync(skillsSrc)) {
    if (SKILL_SKIP.has(name.toLowerCase())) continue;
    const src = path.join(skillsSrc, name);
    const link = path.join(destRoot, name);
    const stat = fs.statSync(src);

    if (stat.isDirectory()) {
      if (pathEntryExists(link)) removeForReplace(link);
      linkDirectory(link, src);
      log(`  [JUNCTION] ${name}`);
      count += 1;
    } else if (name.toLowerCase() !== 'readme.md') {
      if (pathEntryExists(link)) removeForReplace(link);
      linkFile(link, src);
      log(`  [LINK] ${name}`);
      count += 1;
    }
  }
  return count;
}

function removeLegacyCodelyLinks(agentScope, log) {
  let total = 0;
  for (const legacyName of LEGACY_CODELY_EXT_NAMES) {
    const linkPath = path.join(USER_HOME, '.codely-cli', 'extensions', legacyName);
    if (isLinkToRepo(linkPath, agentScope) && removeLink(linkPath, log)) {
      log(`\n[LEGACY:codely] removed ${linkPath}`);
      total += 1;
    }
  }
  return total;
}

function linkCodelyExtension(extensionRoot, log) {
  const agentScope = path.resolve(extensionRoot, '..');
  removeLegacyCodelyLinks(agentScope, log);
  const linkPath = getCodelyExtensionLinkPath();
  fs.mkdirSync(path.dirname(linkPath), { recursive: true });
  if (pathEntryExists(linkPath)) removeForReplace(linkPath);
  linkDirectory(linkPath, extensionRoot);
  log(`\n[SKILLS:codely] junction -> ${linkPath}`);
  log(`  [JUNCTION] .agents/ -> extensions/${EXTENSION_DIR}`);
  return 1;
}

/**
 * Junction/symlink each skill under skillsSrc into user profile dirs.
 * Codely CLI gets .agents/ at extensions/picg-extension.
 */
export function linkAgentSkillsForTools(tools, extensionRoot, skillsSrc, log = console.log) {
  if (!USER_HOME) {
    throw new Error('USERPROFILE/HOME not set');
  }
  if (!fs.existsSync(skillsSrc)) {
    throw new Error(`Missing skills source: ${skillsSrc}`);
  }

  let total = 0;
  for (const tool of tools) {
    if (tool === 'codely') {
      total += linkCodelyExtension(extensionRoot, log);
      continue;
    }

    const destRoots = getSkillLinkDestRoots(tool);
    for (const destRoot of destRoots) {
      log(`\n[SKILLS:${tool}] link -> ${destRoot}`);
      total += linkSkillEntries(skillsSrc, destRoot, log);
    }
  }
  return total;
}

function resolveRealPath(p) {
  try {
    return fs.realpathSync(p);
  } catch {
    return null;
  }
}

function isLinkToRepo(linkPath, repoRoot) {
  if (!pathEntryExists(linkPath)) return false;
  if (!isReparsePoint(linkPath) && !fs.lstatSync(linkPath).isSymbolicLink()) return false;
  const real = resolveRealPath(linkPath);
  if (!real) return false;
  const normalizedRepo = path.resolve(repoRoot);
  return real === normalizedRepo || real.startsWith(`${normalizedRepo}${path.sep}`);
}

function removeLink(p, log) {
  if (!pathEntryExists(p)) return false;
  if (!isReparsePoint(p) && !fs.lstatSync(p).isSymbolicLink()) return false;
  const stat = fs.lstatSync(p);
  if (stat.isDirectory()) fs.rmSync(p, { recursive: true, force: true });
  else fs.unlinkSync(p);
  if (!pathEntryExists(p)) {
    log(`  [RM] ${p}`);
    return true;
  }
  return false;
}

/**
 * Remove junctions/symlinks that point into this repo's .agents/ tree.
 */
export function unlinkAgentSkillsForTools(tools, extensionRoot, skillsSrc, log = console.log) {
  if (!USER_HOME) {
    throw new Error('USERPROFILE/HOME not set');
  }

  const agentScope = path.resolve(extensionRoot, '..');

  const skillNames = fs.existsSync(skillsSrc)
    ? fs.readdirSync(skillsSrc).filter((name) => {
      if (SKILL_SKIP.has(name.toLowerCase())) return false;
      return fs.statSync(path.join(skillsSrc, name)).isDirectory();
    })
    : [];
  const fileNames = fs.existsSync(skillsSrc)
    ? fs.readdirSync(skillsSrc).filter((name) => {
      if (name.toLowerCase() === 'readme.md') return false;
      return fs.statSync(path.join(skillsSrc, name)).isFile();
    })
    : [];

  let total = 0;
  for (const tool of tools) {
    if (tool === 'codely') {
      total += removeLegacyCodelyLinks(agentScope, log);
      const linkPath = getCodelyExtensionLinkPath();
      if (isLinkToRepo(linkPath, agentScope) && removeLink(linkPath, log)) {
        log(`\n[UNLINK:codely] ${linkPath}`);
        total += 1;
      }
      continue;
    }

    for (const destRoot of getSkillLinkDestRoots(tool)) {
      if (!fs.existsSync(destRoot)) continue;
      log(`\n[UNLINK:${tool}] ${destRoot}`);
      for (const name of [...skillNames, ...fileNames]) {
        const linkPath = path.join(destRoot, name);
        if (isLinkToRepo(linkPath, agentScope) && removeLink(linkPath, log)) {
          total += 1;
        }
      }
    }
  }
  return total;
}
