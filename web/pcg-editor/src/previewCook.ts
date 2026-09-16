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
  parseTextureJson,
  PcgExecuteKind,
  type CookResult,
  type ParsedGeometry,
  type ParsedHeightField,
  type ParsedMesh,
  type ParsedSplines,
  type SourceMapping,
} from './cookResult';
import { parsePbrMaterialLibrary, resolvePbrTextureUrl, type PbrMaterialLibrary } from './preview/pbrMaterials';
import {
  parseActionRuntimeFromCookJson,
  type ActionRuntimeMetadata,
} from './actionRuntime';
import {
  parsePreservedGltfRigFromCookJson,
  type PreservedGltfRigMetadata,
} from './preservedGltfRuntime';

/** A previewable image from a Texture-output node (ImageTexture/MeshyImageGen). */
export interface PreviewImage {
  /** Id of the node that produced the texture (cook JSON slotId). */
  nodeId: string;
  /** Raw storage string from node data (pcg-resource://…, /assets/…, URL). */
  storage: string;
  /** Resolved URL for <img>; empty when the node has no image source. */
  url: string;
  repeatX: number;
  repeatY: number;
}

/**
 * Avoid starting an expensive oriented-SDF reconstruction while an MCP authoring
 * transaction is still assembling its terminal chain.  Ordinary graphs keep
 * their eager preview behaviour; a graph containing OrientedSdfSurface waits
 * until at least one Output has an incoming edge.  Explicit node preview and
 * the Re-cook button still bypass this auto-cook guard.
 */
export function shouldAutoCookGraphPreview(graph: {
  nodes: readonly { id: string; type?: string }[];
  edges: readonly { target: string }[];
}): boolean {
  if (!graph.nodes.some((node) => node.type === 'OrientedSdfSurface')) return true;
  const outputIds = new Set(
    graph.nodes.filter((node) => node.type === 'Output').map((node) => node.id),
  );
  return graph.edges.some((edge) => outputIds.has(edge.target));
}

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
  /** Texture-output images when the preview target outputs a Texture pin. */
  images: PreviewImage[];
  /** Per-triangle node attribution (preview-sink cooks only); null when absent. */
  sourceMapping: SourceMapping | null;
  /** Optional editable skeleton, components, sockets, and clips emitted by ActionRig. */
  actionRuntime: ActionRuntimeMetadata | null;
  /** Optional exact glTF skin/animation preservation route. */
  sourceRig: PreservedGltfRigMetadata | null;
  cook: CookResult;
  /** Client-side phase timings; filled by cookGraphPreview (absent in contract tests). */
  timings?: PreviewTimings;
  /** Web-only SDF preview quality receipt. Full cook/export never reads these controls. */
  previewQuality?: PreviewQualityMetadata;
}

export type PreviewQualityMode = 'adaptive' | 'full' | 'medium' | 'low';

export interface PreviewSdfNodeQuality {
  nodeId: string;
  sourceCellSize: number;
  scale: number;
  effectiveCellSize: number;
}

export interface PreviewQualityMetadata {
  requestedQuality: PreviewQualityMode;
  sdfNodeCount: number;
  nodeQualities: PreviewSdfNodeQuality[];
  effectiveScale: number;
  triangleBudget: number | null;
  triangleCount: number;
  attempts: number;
  budgetSatisfied: boolean;
  fullResolution: boolean;
}

export interface PreviewCookOptions {
  quality?: PreviewQualityMode;
  /** False is reserved for final-quality export/acceptance cooks. */
  enforceBudget?: boolean;
  maxAttempts?: number;
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

const DEFAULT_SDF_CELL_SIZE = 0.004;
export const DEFAULT_SDF_PREVIEW_SCALE = 2;
export const DEFAULT_SDF_PREVIEW_TRIANGLE_BUDGET = 500_000;
const MEDIUM_SDF_PREVIEW_SCALE = 2;
const LOW_SDF_PREVIEW_SCALE = 3;
const MAX_SDF_PREVIEW_SCALE = 8;
const MAX_SDF_CELL_SIZE = 0.05;

interface AppliedPreviewQuality {
  graph: GraphJson;
  sdfNodeCount: number;
  nodeQualities: PreviewSdfNodeQuality[];
  triangleBudget: number | null;
}

function finiteNumber(value: unknown, fallback: number): number {
  return typeof value === 'number' && Number.isFinite(value) ? value : fallback;
}

function clamp(value: number, minimum: number, maximum: number): number {
  return Math.min(maximum, Math.max(minimum, value));
}

function previewScaleForNode(
  data: Readonly<Record<string, unknown>>,
  quality: PreviewQualityMode,
): number {
  if (quality === 'full') return 1;
  if (quality === 'medium') return MEDIUM_SDF_PREVIEW_SCALE;
  if (quality === 'low') return LOW_SDF_PREVIEW_SCALE;
  return clamp(
    finiteNumber(data.previewCellSizeScale, DEFAULT_SDF_PREVIEW_SCALE),
    1,
    MAX_SDF_PREVIEW_SCALE,
  );
}

function triangleBudgetForGraph(graph: GraphJson): number | null {
  const budgets = graph.nodes
    .filter((node) => node.type === 'OrientedSdfSurface')
    .map((node) => Math.round(finiteNumber(
      node.data.previewTriangleBudget,
      DEFAULT_SDF_PREVIEW_TRIANGLE_BUDGET,
    )))
    .filter((value) => value >= 10_000);
  return budgets.length > 0 ? Math.min(...budgets) : null;
}

/**
 * Applies a Web-only SDF preview policy without mutating the authored graph.
 * The native node intentionally ignores previewCellSizeScale and
 * previewTriangleBudget, so saved/final cooks retain the authored cellSize.
 */
export function applySdfPreviewQuality(
  graph: GraphJson,
  quality: PreviewQualityMode = 'adaptive',
  retryScaleMultiplier = 1,
): AppliedPreviewQuality {
  const sdfNodes = graph.nodes.filter((node) => node.type === 'OrientedSdfSurface');
  if (sdfNodes.length === 0) {
    return { graph, sdfNodeCount: 0, nodeQualities: [], triangleBudget: null };
  }

  const triangleBudget = quality === 'full' ? null : triangleBudgetForGraph(graph);
  // A Surface Nets shell is normally close to two triangles per active cell.
  // Capping candidate cells keeps an accidental ultra-fine preview from ever
  // serializing a multi-million-triangle PCGR response into the browser.
  const activeCellsPerNode = triangleBudget === null
    ? null
    : Math.max(1_000, Math.floor((triangleBudget * 0.65) / sdfNodes.length));
  const nodeQualities: PreviewSdfNodeQuality[] = [];

  const nodes = graph.nodes.map((node) => {
    if (node.type !== 'OrientedSdfSurface') return node;
    const sourceCellSize = Math.max(
      0.0005,
      finiteNumber(node.data.cellSize, DEFAULT_SDF_CELL_SIZE),
    );
    const requestedScale = quality === 'full'
      ? 1
      : clamp(
        previewScaleForNode(node.data, quality) * retryScaleMultiplier,
        1,
        MAX_SDF_PREVIEW_SCALE,
      );
    const requestedCellSize = sourceCellSize * requestedScale;
    const effectiveCellSize = Math.min(MAX_SDF_CELL_SIZE, requestedCellSize);
    const scale = requestedCellSize <= MAX_SDF_CELL_SIZE
      ? requestedScale
      : effectiveCellSize / sourceCellSize;
    nodeQualities.push({ nodeId: node.id, sourceCellSize, scale, effectiveCellSize });

    if (quality === 'full') return node;
    const authoredActiveCellLimit = Math.max(
      1_000,
      Math.round(finiteNumber(node.data.maxActiveCells, 3_000_000)),
    );
    return {
      ...node,
      data: {
        ...node.data,
        cellSize: effectiveCellSize,
        maxActiveCells: Math.min(authoredActiveCellLimit, activeCellsPerNode!),
      },
    };
  });

  return {
    graph: quality === 'full' ? graph : { ...graph, nodes },
    sdfNodeCount: sdfNodes.length,
    nodeQualities,
    triangleBudget,
  };
}

export function previewTriangleCount(data: PreviewData): number {
  if (data.mesh) return Math.floor(data.mesh.indexCount / 3);
  if (data.geometry) return Math.floor(data.geometry.triangles.length / 3);
  return 0;
}

/** Surface triangle density is approximately inverse-square in cell size. */
export function nextSdfRetryScale(triangleCount: number, triangleBudget: number): number {
  if (triangleCount <= triangleBudget || triangleBudget <= 0) return 1;
  return clamp(Math.sqrt(triangleCount / triangleBudget) * 1.08, 1.2, 2.5);
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

  let mesh: ParsedMesh | null = null;
  if (cook.mesh.length > 0) {
    mesh = parseMeshBinary(cook.mesh);
  }
  let geometry: ParsedGeometry | null = null;
  // PCGM is the authoritative indexed render payload. Parsing PCGG as well
  // duplicates positions, topology, colors and UVs for the same surface and
  // used to make dense Surface Nets cooks exhaust the browser heap. Geometry
  // remains useful for geometry-only/point previews; mesh wireframe mode uses
  // the indexed PCGM triangles when both payloads are present.
  if (!mesh && cook.geometry.length > 0) {
    geometry = parseGeometryBinary(cook.geometry);
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
  let actionRuntime: ActionRuntimeMetadata | null = null;
  let sourceRig: PreservedGltfRigMetadata | null = null;
  try {
    actionRuntime = parseActionRuntimeFromCookJson(cookJson);
    sourceRig = parsePreservedGltfRigFromCookJson(cookJson);
  } catch (error) {
    return {
      ok: false,
      error: `Invalid ActionRig metadata: ${error instanceof Error ? error.message : String(error)}`,
    };
  }
  const images: PreviewImage[] = [];
  const texture = parseTextureJson(cookJson);
  if (texture) {
    images.push({
      nodeId: texture.slotId,
      storage: texture.source,
      url: texture.source ? resolvePbrTextureUrl(texture.source) : '',
      repeatX: texture.repeatX,
      repeatY: texture.repeatY,
    });
  }
  if (cookJson) {
    splines = parseSplineJson(cookJson);
  }
  if (!geometry && !mesh && !scatterPoints && !splines && images.length === 0) {
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

  // Parsed typed arrays and material/image descriptors are self-contained.
  // Do not retain the original PCGR ArrayBuffer through its blob views: a
  // high-resolution reconstruction can otherwise keep tens of megabytes of
  // duplicate binary data alive for the lifetime of the preview.
  const cookMetadata: CookResult = {
    ...cook,
    json: '',
    mesh: new Uint8Array(),
    points: new Uint8Array(),
    geometry: new Uint8Array(),
    heightfield: new Uint8Array(),
    perf: '',
  };

  return {
    ok: true,
    data: {
      geometry,
      mesh,
      scatterPoints,
      splines,
      heightfield,
      materials,
      images,
      sourceMapping,
      actionRuntime,
      sourceRig,
      cook: cookMetadata,
    },
  };
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

function isRetryableSdfBudgetError(error: string | undefined): boolean {
  if (!error || !error.includes('OrientedSdfSurface')) return false;
  return /active cell limit|candidate grid exceeds|increase cellSize/i.test(error);
}

/**
 * Default Web cook path for authored graphs. Adaptive/medium/low previews are
 * bounded and may retry at a coarser SDF cell size; full mode preserves the
 * authored graph exactly and is used by final GLB export and pixel acceptance.
 */
export async function cookGraphPreviewBudgeted(
  graph: GraphJson,
  seed: number,
  signal?: AbortSignal,
  jobId: string = newPreviewJobId(),
  options: PreviewCookOptions = {},
): Promise<PreviewResponse> {
  const requestedQuality = options.quality ?? 'adaptive';
  const enforceBudget = options.enforceBudget ?? requestedQuality !== 'full';
  const maxAttempts = clamp(Math.round(options.maxAttempts ?? 3), 1, 5);
  const prepared = prepareGraphForPreviewCook(graph);
  const initial = applySdfPreviewQuality(prepared, requestedQuality);

  if (initial.sdfNodeCount === 0 || !enforceBudget || requestedQuality === 'full') {
    const response = await cookGraphPreview(initial.graph, seed, signal, jobId);
    if (response.data) {
      const triangleCount = previewTriangleCount(response.data);
      response.data.previewQuality = {
        requestedQuality,
        sdfNodeCount: initial.sdfNodeCount,
        nodeQualities: initial.nodeQualities,
        effectiveScale: Math.max(1, ...initial.nodeQualities.map((item) => item.scale)),
        triangleBudget: null,
        triangleCount,
        attempts: 1,
        budgetSatisfied: true,
        fullResolution: requestedQuality === 'full' || initial.sdfNodeCount === 0,
      };
    }
    return response;
  }

  const triangleBudget = initial.triangleBudget ?? DEFAULT_SDF_PREVIEW_TRIANGLE_BUDGET;
  let retryScaleMultiplier = 1;
  let lastError = '';
  for (let attempt = 1; attempt <= maxAttempts; attempt += 1) {
    if (signal?.aborted) return { ok: false, error: 'aborted' };
    const applied = applySdfPreviewQuality(prepared, requestedQuality, retryScaleMultiplier);
    const response = await cookGraphPreview(applied.graph, seed, signal, jobId);
    if (!response.ok || !response.data) {
      lastError = response.error ?? 'Cook failed';
      if (attempt < maxAttempts && isRetryableSdfBudgetError(response.error)) {
        retryScaleMultiplier *= 1.5;
        continue;
      }
      return response;
    }

    const triangleCount = previewTriangleCount(response.data);
    if (triangleCount <= triangleBudget) {
      response.data.previewQuality = {
        requestedQuality,
        sdfNodeCount: applied.sdfNodeCount,
        nodeQualities: applied.nodeQualities,
        effectiveScale: Math.max(...applied.nodeQualities.map((item) => item.scale)),
        triangleBudget,
        triangleCount,
        attempts: attempt,
        budgetSatisfied: true,
        fullResolution: false,
      };
      return response;
    }

    lastError = `SDF preview produced ${triangleCount.toLocaleString()} triangles for a ${triangleBudget.toLocaleString()} triangle budget`;
    if (attempt < maxAttempts) {
      retryScaleMultiplier *= nextSdfRetryScale(triangleCount, triangleBudget);
    }
  }

  return {
    ok: false,
    error: `${lastError}. Increase Preview Cell Scale or Preview Triangle Budget, or choose Full explicitly.`,
  };
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
