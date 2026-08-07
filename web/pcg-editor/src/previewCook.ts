// previewCook.ts — Cook the current graph via pcg-server (through the Vite
// dev-server /api/cook proxy) and parse the binary result for the preview
// viewport. Requires `npm run dev` and a running pcg-server.

import type { GraphJson } from './graphSchema';
import {
  parseCookResult,
  parseGeometryBinary,
  parseMeshBinary,
  parsePointBinary,
  parseSplineJson,
  PcgExecuteKind,
  type CookResult,
  type ParsedGeometry,
  type ParsedMesh,
  type ParsedSplines,
} from './cookResult';

export interface PreviewData {
  /** Polygon geometry (edges/points modes) when the graph outputs faces. */
  geometry: ParsedGeometry | null;
  /** Render mesh (flat-shading corner vertices) when the graph outputs faces. */
  mesh: ParsedMesh | null;
  /** Scatter point cloud when the graph outputs points only. */
  scatterPoints: Float32Array | null;
  /** Spline polylines when the graph outputs SpatialSpline data. */
  splines: ParsedSplines | null;
  cook: CookResult;
}

export interface PreviewResponse {
  ok: boolean;
  data?: PreviewData;
  error?: string;
}

/** Must match pcg-core graph_executor kPreviewSinkNodeId (and Unity PcgGraphPreviewSubgraph). */
export const PREVIEW_SINK_NODE_ID = '__pcg_preview_sink__';

/**
 * Builds an upstream-only cook graph for per-node preview: the target node plus
 * all transitive upstream nodes/edges, terminated by the preview sink that
 * pcg-core recognizes. Mirrors Unity PcgGraphPreviewSubgraph.TryBuildUpstream.
 * Returns null when the target node is not in the graph.
 */
export function buildPreviewCookGraph(
  graph: GraphJson,
  targetNodeId: string,
  sourceHandle: string,
): GraphJson | null {
  const target = graph.nodes.find((n) => n.id === targetNodeId);
  if (!target) return null;

  const included = new Set<string>([targetNodeId]);
  const queue: string[] = [targetNodeId];
  while (queue.length > 0) {
    const nodeId = queue.shift()!;
    for (const edge of graph.edges) {
      if (edge.target !== nodeId || included.has(edge.source)) continue;
      included.add(edge.source);
      queue.push(edge.source);
    }
  }

  const nodes = graph.nodes.filter((n) => included.has(n.id)).map((n) => ({ ...n }));
  const edges = graph.edges
    .filter((e) => included.has(e.source) && included.has(e.target))
    .map((e) => ({ ...e }));
  const parameters = (graph.parameters ?? []).filter(
    (p) => p.targetNode && included.has(p.targetNode),
  );

  nodes.push({
    id: PREVIEW_SINK_NODE_ID,
    type: 'Output',
    position: { ...target.position },
    data: {},
  });
  edges.push({
    id: `${PREVIEW_SINK_NODE_ID}_edge`,
    source: targetNodeId,
    target: PREVIEW_SINK_NODE_ID,
    sourceHandle,
    targetHandle: 'in',
  });

  return {
    version: graph.version,
    nodes,
    edges,
    parameters,
    subgraphs: graph.subgraphs,
  };
}

export async function cookGraphPreview(
  graph: GraphJson,
  seed: number,
  signal?: AbortSignal,
): Promise<PreviewResponse> {
  try {
    const form = new FormData();
    const meta = JSON.stringify({
      seed,
      api_version: 1,
      job_id: crypto.randomUUID().replaceAll('-', ''),
    });
    form.append('meta', new Blob([meta], { type: 'application/json' }));
    form.append('graph', new Blob([JSON.stringify(graph)], { type: 'application/json' }));

    const res = await fetch('/api/cook', { method: 'POST', body: form, signal });
    if (!res.ok) {
      const text = await res.text();
      let message = text;
      try {
        const parsed = JSON.parse(text) as { error?: string };
        if (parsed.error) message = parsed.error;
      } catch {
        // non-JSON error body — keep raw text
      }
      return { ok: false, error: `HTTP ${res.status}: ${message}` };
    }

    const buffer = await res.arrayBuffer();
    const cook = parseCookResult(buffer);
    if (cook.code !== 0) {
      return { ok: false, error: cook.error || `Cook failed (code ${cook.code})` };
    }

    let geometry: ParsedGeometry | null = null;
    if (cook.geometry.length > 0) {
      geometry = parseGeometryBinary(cook.geometry);
    }
    let mesh: ParsedMesh | null = null;
    if (cook.mesh.length > 0) {
      mesh = parseMeshBinary(cook.mesh);
    }
    let scatterPoints: Float32Array | null = null;
    if (cook.points.length > 0) {
      scatterPoints = parsePointBinary(cook.points);
    }
    let splines: ParsedSplines | null = null;
    if (cook.json) {
      splines = parseSplineJson(cook.json);
    }
    if (!geometry && !mesh && !scatterPoints && !splines) {
      if (cook.kind === PcgExecuteKind.Json) {
        return { ok: false, error: 'Graph produced JSON output only — nothing to preview.' };
      }
      return { ok: false, error: 'Cook succeeded but produced no previewable geometry.' };
    }

    return { ok: true, data: { geometry, mesh, scatterPoints, splines, cook } };
  } catch (err) {
    if (err instanceof DOMException && err.name === 'AbortError') {
      return { ok: false, error: 'aborted' };
    }
    return { ok: false, error: String(err) };
  }
}

export async function cancelCook(): Promise<void> {
  try {
    await fetch('/api/cook-cancel', { method: 'POST' });
  } catch {
    // best-effort; server may already be gone
  }
}

export async function checkCookServer(): Promise<{ ok: boolean; version?: string }> {
  try {
    const res = await fetch('/api/cook-health');
    if (!res.ok) return { ok: false };
    const data = (await res.json()) as { ok?: boolean; version?: string };
    return { ok: data.ok === true, version: data.version };
  } catch {
    return { ok: false };
  }
}
