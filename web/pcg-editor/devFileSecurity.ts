import fs from 'node:fs';
import path from 'node:path';

function assertInsideWorkspace(workspaceRoot: string, candidate: string): void {
  const relative = path.relative(workspaceRoot, candidate);
  if (relative.startsWith('..') || path.isAbsolute(relative)) {
    throw new Error('Graph path must be a .pcg file inside the workspace');
  }
}

export function resolveWorkspaceGraphPath(workspaceRoot: string, filePath: unknown): string {
  if (typeof filePath !== 'string' || filePath.trim() === '') {
    throw new Error('Missing graph file path');
  }
  const root = path.resolve(workspaceRoot);
  const resolved = path.resolve(root, filePath);
  assertInsideWorkspace(root, resolved);
  if (!['.pcg', '.picg'].includes(path.extname(resolved).toLowerCase())) {
    throw new Error('Graph path must be a .picg or .pcg file inside the workspace');
  }
  return resolved;
}

export function resolveExistingWorkspaceGraphPath(workspaceRoot: string, filePath: unknown): string {
  const root = fs.realpathSync(path.resolve(workspaceRoot));
  const resolved = resolveWorkspaceGraphPath(root, filePath);
  const realPath = fs.realpathSync(resolved);
  assertInsideWorkspace(root, realPath);
  return realPath;
}

export function assertWorkspaceGraphParent(workspaceRoot: string, filePath: string): void {
  const root = fs.realpathSync(path.resolve(workspaceRoot));
  const realParent = fs.realpathSync(path.dirname(filePath));
  assertInsideWorkspace(root, realParent);
  if (fs.existsSync(filePath)) assertInsideWorkspace(root, fs.realpathSync(filePath));
}
