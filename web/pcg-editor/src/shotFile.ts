import type { GraphJson } from './graphSchema';
import { createDefaultShot, normalizeShotDocument, syncShotCameras, type ShotDocument } from './shot';
import { parseGraphJson, type ImportResult } from './importGraph';

export async function loadWorkspaceProject(filePath: string): Promise<{ graph: Extract<ImportResult, { ok: true }>; shot: ShotDocument; hasShot: boolean }> {
  const query = encodeURIComponent(filePath);
  const graphResponse = await fetch(`/api/load-graph?path=${query}`);
  if (!graphResponse.ok) throw new Error('Cannot open this workspace graph. Check its project-relative path.');
  const graph = parseGraphJson(await graphResponse.text());
  if (!graph.ok) throw new Error(graph.error);
  const shotResponse = await fetch(`/api/load-shot?path=${query}`);
  if (!shotResponse.ok && shotResponse.status !== 404) throw new Error('The camera sidecar could not be loaded.');
  const hasShot = shotResponse.ok;
  const shot = hasShot ? parseShotFile(await shotResponse.text()) : createDefaultShot();
  return { graph, shot: { ...shot, graphPath: filePath }, hasShot };
}

export function parseShotFile(text: string): ShotDocument {
  const raw: unknown = JSON.parse(text);
  if (!raw || typeof raw !== 'object' || (!('cameras' in raw) && !('camera' in raw)) || !('durationSeconds' in raw)) {
    throw new Error('Choose a PICG shot file containing cameras and durationSeconds.');
  }
  return normalizeShotDocument(raw);
}

export function downloadShot(shot: ShotDocument): Promise<{ path: string; url: string }> {
  return exportDocument(normalizeShotDocument(syncShotCameras(shot)), `${shot.name.replace(/[^\p{L}\p{N}_-]/gu, '_') || 'shot'}.picgshot`);
}

export function downloadProject(graph: GraphJson, shot: ShotDocument, filename: string): Promise<{ path: string; url: string }> {
  return exportDocument({ format: 'PICG-project', version: '1.0', graph, shot: normalizeShotDocument(syncShotCameras(shot)), graphPath: filename },
    `${filename.split('/').pop()?.replace(/\.(pcg|picg)$/i, '') || 'project'}.picgproject`);
}

async function exportDocument(value: unknown, filename: string): Promise<{ path: string; url: string }> {
  const response = await fetch(`/api/export-document?name=${encodeURIComponent(filename)}`, {
    method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(value, null, 2),
  });
  const result = await response.json() as { ok: boolean; path: string; url: string; error?: string };
  if (!response.ok || !result.ok) throw new Error(result.error ?? 'Cannot save the export. Start the PICG web server.');
  return result;
}
