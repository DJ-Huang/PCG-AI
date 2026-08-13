// previewCook.ts — Cook the current graph via pcg-server (through the Vite
// dev-server /api/cook proxy) and parse the binary result for the preview
// viewport. Requires `npm run dev` and a running pcg-server.

import type { GraphJson, GraphSubgraph } from './graphSchema';
import { getOutputPinType } from './nodeManifest';
import { isSubgraphInterfaceNode } from './subgraphs';
import {
  parseCookResult,
  parseGeometryBinary,
  parseMeshBinary,
  parsePointBinary,
  parseSplineJson,
  parseSourceMapping,
  parseHeightFieldBinary,
  buildHeightFieldPreviewMesh,
  extractCookJsonText,
  isHeightFieldCookJson,
  PcgExecuteKind,
  type CookResult,
  type ParsedGeometry,
  type ParsedHeightField,
  type ParsedMesh,
  type ParsedSplines,
  type SourceMapping,
} from './cookResult';
import { parsePbrMaterialLibrary, type PbrMaterialLibrary } from './preview/pbrMaterials';

export interface PreviewData {
  /** Polygon geometry (edges/points modes) when the graph outputs faces. */
  geometry: ParsedGeometry | null;
  /** Render mesh (flat-shading corner vertices) when the graph outputs faces. */
  mesh: ParsedMesh | null;
  /** Scatter point cloud when the graph outputs points only. */
  scatterPoints: Float32Array | null;
  /** Spline polylines when the graph outputs SpatialSpline data. */
  splines: ParsedSplines | null;
  /** Source heightfield when the graph outputs terrain data. */
  heightfield: ParsedHeightField | null;
  /** PBR definitions keyed by PCGM material slot name. */
  materials: PbrMaterialLibrary;
  /** Per-triangle node attribution (preview-sink cooks only); null when absent. */
  sourceMapping: SourceMapping | null;
  cook: CookResult;
  /** Client-side phase timings; filled by cookGraphPreview (absent in contract tests). */
  timings?: PreviewTimings;
}

export interface PreviewTimings {
  /** fetch → body downloaded: server wall clock + network/transfer. */
  fetchMs: number;
  /** parseCookResult: header, JSON summary decode, blob slicing. */
  parseCookMs: number;
  /** Binary payloads → preview data (geometry/mesh/points/heightfield/splines). */
  buildDataMs: number;
}

export interface PreviewResponse {
  ok: boolean;
  data?: PreviewData;
  error?: string;
}

/** Must match pcg-core graph_executor kPreviewSinkNodeId (and Unity PcgGraphPreviewSubgraph). */
export const PREVIEW_SINK_NODE_ID = '__pcg_preview_sink__';
export const PREVIEW_CONVERT_NODE_ID = '__pcg_preview_convert__';

function needsConvertHeightFieldPreview(nodeType: string, sourceHandle: string): boolean {
  if (nodeType === 'ConvertHeightField') return false;
  return getOutputPinType(nodeType, sourceHandle) === 'HeightField';
}

/**
 * Collects the upstream closure of the given root nodes. pcg-core executes
 * every node it receives, so graphs sent to cook must be limited to what the
 * preview actually pulls — orphan nodes with unbound required inputs would
 * otherwise fail the entire cook (e.g. a freshly dropped, still-unwired
 * library subgraph must not break the main preview).
 */
function collectUpstreamClosure(
  allEdges: GraphJson['edges'],
  rootIds: ReadonlySet<string>,
): Set<string> {
  const included = new Set<string>(rootIds);
  const queue = [...rootIds];
  while (queue.length > 0) {
    const nodeId = queue.shift()!;
    for (const edge of allEdges) {
      if (edge.target !== nodeId || included.has(edge.source)) continue;
      included.add(edge.source);
      queue.push(edge.source);
    }
  }
  return included;
}

/**
 * Full-graph preview: retarget the terminal Output to the preview sink id so
 * pcg-core enables preview-only behaviors (per-triangle node attribution for
 * component picking), and rasterize HeightField → Output through
 * ConvertHeightField.
 *
 * Cook scope is the Output node's upstream closure; nodes that don't feed the
 * Output cannot affect the preview result.
 */
export function prepareGraphForPreviewCook(graph: GraphJson): GraphJson {
  const output = graph.nodes.find((n) => n.type === 'Output');
  if (!output) return graph;
  const incoming = graph.edges.find((e) => e.target === output.id);
  if (!incoming) return graph;
  const source = graph.nodes.find((n) => n.id === incoming.source);
  const convertHeightField =
    source != null && needsConvertHeightFieldPreview(source.type, incoming.sourceHandle ?? 'out');

  const included = collectUpstreamClosure(graph.edges, new Set([output.id]));
  const nodes = graph.nodes.filter((n) => included.has(n.id)).map((n) => ({ ...n }));
  const edges = graph.edges
    .filter((e) => included.has(e.source) && included.has(e.target))
    .map((e) => ({ ...e }));
  const parameters = (graph.parameters ?? []).filter(
    (p) => p.targetNode && included.has(p.targetNode),
  );

  const outputNode = nodes.find((n) => n.id === output.id);
  const incomingEdge = edges.find((e) => e.target === output.id);
  if (!outputNode || !incomingEdge) return graph;

  outputNode.id = PREVIEW_SINK_NODE_ID;
  incomingEdge.target = PREVIEW_SINK_NODE_ID;

  if (convertHeightField) {
    nodes.push({
      id: PREVIEW_CONVERT_NODE_ID,
      type: 'ConvertHeightField',
      position: { ...output.position },
      data: {},
    });
    incomingEdge.target = PREVIEW_CONVERT_NODE_ID;
    incomingEdge.targetHandle = 'in';
    edges.push({
      id: `${PREVIEW_CONVERT_NODE_ID}_edge`,
      source: PREVIEW_CONVERT_NODE_ID,
      target: PREVIEW_SINK_NODE_ID,
      sourceHandle: 'out',
      targetHandle: 'in',
    });
  }

  return { ...graph, nodes, edges, parameters };
}

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

  let previewSourceId = targetNodeId;
  let previewSourceHandle = sourceHandle;
  if (needsConvertHeightFieldPreview(target.type, sourceHandle)) {
    previewSourceId = PREVIEW_CONVERT_NODE_ID;
    previewSourceHandle = 'out';
    nodes.push({
      id: PREVIEW_CONVERT_NODE_ID,
      type: 'ConvertHeightField',
      position: { x: target.position.x, y: target.position.y + 80 },
      data: {},
    });
    edges.push({
      id: `${PREVIEW_CONVERT_NODE_ID}_in`,
      source: targetNodeId,
      target: PREVIEW_CONVERT_NODE_ID,
      sourceHandle,
      targetHandle: 'in',
    });
  }

  nodes.push({
    id: PREVIEW_SINK_NODE_ID,
    type: 'Output',
    position: { ...target.position },
    data: {},
  });
  edges.push({
    id: `${PREVIEW_SINK_NODE_ID}_edge`,
    source: previewSourceId,
    target: PREVIEW_SINK_NODE_ID,
    sourceHandle: previewSourceHandle,
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

export function newPreviewJobId(): string {
  return crypto.randomUUID().replaceAll('-', '');
}

/** Synthetic Output node id for cooking a subgraph interior in isolation. */
export const SUBGRAPH_PREVIEW_OUTPUT_ID = '__pcg_subgraph_output__';

/**
 * Builds a cookable root graph from a subgraph interior: structural interface
 * nodes (SubgraphInput/SubgraphOutput) are stripped, edges that fed the
 * SubgraphOutput are rewired into a synthetic Output node, and edges from
 * SubgraphInput are dropped (parent inputs are unbound when cooking the
 * interior standalone). Nested Subgraph instances stay intact — pcg-core
 * expands them via the accompanying subgraphs array.
 */
export function buildSubgraphCookGraph(
  subgraph: GraphSubgraph,
  subgraphs: GraphSubgraph[],
): GraphJson {
  const interfaceIds = new Set(
    subgraph.nodes.filter((n) => isSubgraphInterfaceNode(n.type)).map((n) => n.id),
  );
  const outputIds = new Set(
    subgraph.nodes.filter((n) => n.type === 'SubgraphOutput').map((n) => n.id),
  );

  // Cook scope: upstream closure of the SubgraphOutput feeders, mirroring the
  // root-level preview pruning (orphan interior nodes must not fail the cook).
  const feederIds = new Set(
    subgraph.edges.filter((e) => outputIds.has(e.target)).map((e) => e.source),
  );
  const cookScope =
    feederIds.size > 0
      ? collectUpstreamClosure(subgraph.edges, new Set([...feederIds, ...outputIds]))
      : null;

  const nodes = subgraph.nodes
    .filter((n) => !interfaceIds.has(n.id))
    .filter((n) => cookScope === null || cookScope.has(n.id))
    .map((n) => ({ id: n.id, type: n.type, position: { ...n.position }, data: { ...n.data } }));

  const edges: GraphJson['edges'] = [];
  let feedsOutput = false;
  for (const e of subgraph.edges) {
    if (interfaceIds.has(e.source)) continue;
    if (cookScope !== null && !cookScope.has(e.source)) continue;
    if (cookScope !== null && !outputIds.has(e.target) && !cookScope.has(e.target)) continue;
    if (outputIds.has(e.target)) {
      feedsOutput = true;
      edges.push({
        id: `${e.id}__subgraph_out`,
        source: e.source,
        target: SUBGRAPH_PREVIEW_OUTPUT_ID,
        sourceHandle: e.sourceHandle,
        targetHandle: 'in',
      });
      continue;
    }
    if (interfaceIds.has(e.target)) continue;
    edges.push({ ...e });
  }

  if (feedsOutput) {
    const anchor = subgraph.nodes.find((n) => n.type === 'SubgraphOutput');
    nodes.push({
      id: SUBGRAPH_PREVIEW_OUTPUT_ID,
      type: 'Output',
      position: anchor ? { ...anchor.position } : { x: 0, y: 0 },
      data: {},
    });
  }

  const scopedNodeIds = new Set(nodes.map((n) => n.id));

  return {
    version: '2.0',
    nodes,
    edges,
    parameters: (subgraph.parameters ?? []).filter(
      (p) => p.targetNode && scopedNodeIds.has(p.targetNode),
    ),
    subgraphs,
  };
}

/** Parse cook output into preview data (shared by live cook and contract tests). */
export function buildPreviewDataFromCook(cook: CookResult): PreviewResponse {
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
  let heightfield: ParsedHeightField | null = null;
  let heightfieldParseError: string | null = null;
  if (cook.heightfield.length > 0) {
    try {
      heightfield = parseHeightFieldBinary(cook.heightfield);
    } catch (err) {
      heightfieldParseError = err instanceof Error ? err.message : String(err);
    }
  }
  if (!mesh && heightfield) {
    try {
      mesh = buildHeightFieldPreviewMesh(heightfield);
    } catch {
      // fall through to summary-only error below
    }
  }
  let scatterPoints: Float32Array | null = null;
  if (cook.points.length > 0 && cook.points[0] === 0x50) {
    scatterPoints = parsePointBinary(cook.points);
  }
  let splines: ParsedSplines | null = null;
  const cookJson = extractCookJsonText(cook);
  const materials = parsePbrMaterialLibrary(cookJson);
  const sourceMapping = parseSourceMapping(cookJson);
  if (cookJson) {
    splines = parseSplineJson(cookJson);
  }
  if (!geometry && !mesh && !scatterPoints && !splines) {
    if (heightfieldParseError) {
      return {
        ok: false,
        error: `Invalid heightfield payload: ${heightfieldParseError}`,
      };
    }
    if (isHeightFieldCookJson(cookJson) && cook.heightfield.length === 0) {
      return {
        ok: false,
        error:
          'Terrain cook succeeded but heightfield binary is missing — restart pcg-server and re-cook.',
      };
    }
    if (cook.kind === PcgExecuteKind.Json) {
      return { ok: false, error: 'Graph produced JSON output only — nothing to preview.' };
    }
    return { ok: false, error: 'Cook succeeded but produced no previewable geometry.' };
  }

  return { ok: true, data: { geometry, mesh, scatterPoints, splines, heightfield, materials, sourceMapping, cook } };
}

export async function cookGraphPreview(
  graph: GraphJson,
  seed: number,
  signal?: AbortSignal,
  jobId: string = newPreviewJobId(),
): Promise<PreviewResponse> {
  try {
    const form = new FormData();
    const meta = JSON.stringify({
      seed,
      api_version: 1,
      job_id: jobId,
    });
    form.append('meta', new Blob([meta], { type: 'application/json' }));
    form.append('graph', new Blob([JSON.stringify(graph)], { type: 'application/json' }));

    const fetchStart = performance.now();
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
    const fetchEnd = performance.now();
    const cook = parseCookResult(buffer);
    const parseEnd = performance.now();
    const response = buildPreviewDataFromCook(cook);
    const buildEnd = performance.now();
    if (response.data) {
      response.data.timings = {
        fetchMs: fetchEnd - fetchStart,
        parseCookMs: parseEnd - fetchEnd,
        buildDataMs: buildEnd - parseEnd,
      };
    }
    return response;
  } catch (err) {
    if (err instanceof DOMException && err.name === 'AbortError') {
      return { ok: false, error: 'aborted' };
    }
    return { ok: false, error: String(err) };
  }
}

export async function cancelCook(jobId: string): Promise<boolean> {
  if (!jobId) return false;
  try {
    const res = await fetch('/api/cook-cancel', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ job_id: jobId }),
    });
    if (!res.ok) return false;
    const data = (await res.json()) as { canceled?: boolean };
    return data.canceled === true;
  } catch {
    // best-effort; server may already be gone
    return false;
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
