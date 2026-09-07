import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { afterEach, describe, expect, it } from 'vitest';

import {
  assertWorkspaceGraphParent,
  resolveExistingWorkspaceGraphPath,
  resolveWorkspaceGraphPath,
} from './devFileSecurity';

const roots: string[] = [];

function temporaryDirectory(prefix: string): string {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), prefix));
  roots.push(directory);
  return directory;
}

afterEach(() => {
  for (const root of roots.splice(0)) {
    fs.rmSync(root, { recursive: true, force: true });
  }
});

describe('dev file security', () => {
  it('accepts only .pcg paths inside the workspace', () => {
    const workspace = temporaryDirectory('pcg-workspace-');
    expect(resolveWorkspaceGraphPath(workspace, 'shots/city.pcg'))
      .toBe(path.join(workspace, 'shots/city.pcg'));
    expect(() => resolveWorkspaceGraphPath(workspace, '../escape.pcg')).toThrow(/inside the workspace/);
    expect(() => resolveWorkspaceGraphPath(workspace, 'shots/city.json')).toThrow(/inside the workspace/);
  });

  it('rejects existing files reached through an escaping symlink', () => {
    const workspace = temporaryDirectory('pcg-workspace-');
    const outside = temporaryDirectory('pcg-outside-');
    fs.writeFileSync(path.join(outside, 'escape.pcg'), '{}');
    fs.symlinkSync(outside, path.join(workspace, 'linked'));

    expect(() => resolveExistingWorkspaceGraphPath(workspace, 'linked/escape.pcg'))
      .toThrow(/inside the workspace/);
  });

  it('rejects graph writes through an escaping parent symlink', () => {
    const workspace = temporaryDirectory('pcg-workspace-');
    const outside = temporaryDirectory('pcg-outside-');
    fs.symlinkSync(outside, path.join(workspace, 'linked'));
    const graphPath = resolveWorkspaceGraphPath(workspace, 'linked/escape.pcg');

    expect(() => assertWorkspaceGraphParent(workspace, graphPath)).toThrow(/inside the workspace/);
  });
});
