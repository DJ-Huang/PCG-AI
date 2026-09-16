import path from 'node:path';
import { promises as fs } from 'node:fs';
import type { IncomingMessage } from 'node:http';
import type { Plugin } from 'vite';

const MAX_FILES = 5000;
const MAX_ENTRIES = 50000;
const MAX_GRAPH_BYTES = 32 * 1024 * 1024;
const excluded = new Set(['node_modules', 'Library', 'Temp', 'dist', 'build']);
const within = (root: string, file: string) => {
  const relative = path.relative(root, file);
  return relative !== '..' && !relative.startsWith(`..${path.sep}`) && !path.isAbsolute(relative);
};
async function isLinkedSubgraphGraph(file: string) {
  try {
    const parsed = JSON.parse(await fs.readFile(file, 'utf8')) as unknown;
    return !!parsed && typeof parsed === 'object' && !Array.isArray(parsed)
      && (parsed as Record<string, unknown>).version === '3.0';
  } catch {
    // Keep malformed files visible so opening them still reports the parse error.
    return false;
  }
}
async function resolveFolder(workspace: string, folder: unknown) {
  if (typeof folder !== 'string' || !folder.trim() || folder.includes('\0')) throw new Error('Enter a valid folder path.');
  const root = await fs.realpath(path.resolve(workspace, folder.trim()));
  if (!(await fs.stat(root)).isDirectory()) throw new Error('The path is not a folder.');
  return root;
}
export async function listAssetFiles(workspace: string, folder: unknown = 'examples') {
  const root = await resolveFolder(workspace, folder);
  const files: { name: string; path: string }[] = [];
  const warnings: string[] = [];
  let visited = 0;
  let truncated = false;
  async function walk(directory: string, depth: number) {
    if (depth > 32) { truncated = true; return; }
    let entries;
    try { entries = await fs.readdir(directory, { withFileTypes: true }); }
    catch { warnings.push(`Could not read ${path.relative(root, directory) || '.'}`); return; }
    entries.sort((a, b) => a.name.localeCompare(b.name));
    for (const entry of entries) {
      if (files.length >= MAX_FILES || ++visited > MAX_ENTRIES) { truncated = true; return; }
      if (entry.name.startsWith('.') || entry.isSymbolicLink()) continue;
      const target = path.join(directory, entry.name);
      if (entry.isDirectory() && !excluded.has(entry.name)) await walk(target, depth + 1);
      else if (entry.isFile() && /\.pcg$/i.test(entry.name) && !(await isLinkedSubgraphGraph(target))) {
        files.push({ name: entry.name, path: path.relative(root, target).split(path.sep).join('/') });
      }
    }
  }
  await walk(root, 0);
  return { root, files, truncated, warnings };
}
export async function readAssetFile(workspace: string, folder: unknown, relativePath: unknown) {
  const root = await resolveFolder(workspace, folder);
  if (typeof relativePath !== 'string' || path.isAbsolute(relativePath) || !/\.pcg$/i.test(relativePath)) throw new Error('Choose a .pcg file in this folder.');
  const requested = path.resolve(root, relativePath);
  if (!within(root, requested)) throw new Error('File is outside the selected folder.');
  const resolved = await fs.realpath(requested);
  if (!within(root, resolved)) throw new Error('File is outside the selected folder.');
  const stat = await fs.stat(resolved);
  if (!stat.isFile() || stat.size > MAX_GRAPH_BYTES) throw new Error('Graph must be a file smaller than 32 MB.');
  const canonicalWorkspace = await fs.realpath(workspace);
  return {
    text: await fs.readFile(resolved, 'utf8'),
    // External folders remain read-only; the editor saves an exported copy.
    filename: within(canonicalWorkspace, resolved) ? path.relative(canonicalWorkspace, resolved).split(path.sep).join('/') : resolved,
  };
}
export function allowAssetRequest(req: Pick<IncomingMessage, 'headers' | 'method'>) {
  if (req.method !== 'POST' || !req.headers['content-type']?.startsWith('application/json')) return false;
  try {
    const target = new URL(`http://${req.headers.host}`);
    if (!['localhost', '127.0.0.1', '[::1]'].includes(target.hostname)) return false;
    if (req.headers.origin && new URL(req.headers.origin).host !== target.host) return false;
    return req.headers['sec-fetch-site'] !== 'cross-site';
  } catch { return false; }
}
export function assetFilesPlugin(workspace: string): Plugin {
  return {
    name: 'pcg-asset-files',
    configureServer(server) {
      server.middlewares.use('/api/asset-files', async (req, res) => {
        res.setHeader('Content-Type', 'application/json');
        res.setHeader('Cache-Control', 'no-store');
        if (!allowAssetRequest(req)) { res.statusCode = 403; res.end(JSON.stringify({ error: 'Use the local editor to browse asset folders.' })); return; }
        try {
          let body = '';
          for await (const chunk of req) {
            body += String(chunk);
            if (Buffer.byteLength(body) > 16384) throw new Error('Request is too large.');
          }
          const request = JSON.parse(body);
          const result = request.action === 'read'
            ? await readAssetFile(workspace, request.folder, request.path)
            : request.action === 'list'
              ? await listAssetFiles(workspace, request.folder ?? 'examples')
              : (() => { throw new Error('Unknown asset operation.'); })();
          res.end(JSON.stringify(result));
        } catch (error) {
          res.statusCode = 400;
          res.end(JSON.stringify({ error: error instanceof Error ? error.message : String(error) }));
        }
      });
    },
  };
}
