import { afterEach, describe, expect, it } from 'vitest';
import { promises as fs } from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import { allowAssetRequest, listAssetFiles, readAssetFile } from './assetFiles';
const temporary: string[] = [];
afterEach(async () => { await Promise.all(temporary.splice(0).map((root) => fs.rm(root, { recursive: true, force: true }))); });
async function fixture() {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'picg-assets-')); temporary.push(root);
  await fs.mkdir(path.join(root, 'examples/nested'), { recursive: true });
  await fs.writeFile(path.join(root, 'examples/first.pcg'), '{"version":"2.0","nodes":[]}');
  await fs.writeFile(path.join(root, 'examples/nested/second.PCG'), '{}');
  await fs.writeFile(path.join(root, 'examples/linked-subgraph.pcg'), '{"version":"3.0","nodes":[]}');
  await fs.writeFile(path.join(root, 'examples/note.txt'), 'not a graph');
  return root;
}
describe('asset file access', () => {
  it('defaults to Examples and discovers only Web-editable pcg files recursively', async () => {
    const root = await fixture();
    const result = await listAssetFiles(root);
    expect(result.files.map((file) => file.path)).toEqual(['first.pcg', 'nested/second.PCG']);
    const graph = await readAssetFile(root, 'examples', 'first.pcg');
    expect(graph.filename).toBe('examples/first.pcg');
    expect(JSON.parse(graph.text).version).toBe('2.0');
  });
  it('supports explicitly selected external folders', async () => {
    const root = await fixture(); const external = await fixture();
    const folder = path.join(external, 'examples');
    expect((await listAssetFiles(root, folder)).files).toHaveLength(2);
    expect((await readAssetFile(root, folder, 'first.pcg')).filename).toBe(await fs.realpath(path.join(folder, 'first.pcg')));
  });
  it('rejects traversal, non-graphs, symlink escapes and missing directories', async () => {
    const root = await fixture();
    await fs.writeFile(path.join(root, 'outside.pcg'), '{}');
    await fs.symlink(path.join(root, 'outside.pcg'), path.join(root, 'examples/link.pcg'));
    expect((await listAssetFiles(root)).files).toHaveLength(2);
    await expect(readAssetFile(root, 'examples', '../outside.pcg')).rejects.toThrow('outside');
    await expect(readAssetFile(root, 'examples', 'link.pcg')).rejects.toThrow('outside');
    await expect(readAssetFile(root, 'examples', 'note.txt')).rejects.toThrow('.pcg');
    await expect(listAssetFiles(root, 'missing')).rejects.toThrow();
  });
  it('requires local JSON requests from the editor origin', () => {
    const headers = { host: '127.0.0.1:5173', 'content-type': 'application/json', origin: 'http://127.0.0.1:5173' };
    expect(allowAssetRequest({ method: 'POST', headers })).toBe(true);
    expect(allowAssetRequest({ method: 'POST', headers: { ...headers, origin: 'https://example.com' } })).toBe(false);
    expect(allowAssetRequest({ method: 'GET', headers })).toBe(false);
    expect(allowAssetRequest({ method: 'POST', headers: { ...headers, host: 'attacker.example' } })).toBe(false);
  });
});
