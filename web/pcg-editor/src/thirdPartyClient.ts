// Typed client for pcg-server third-party cloud generation vendors (Tripo, …).
// API keys are stored by the local server's protected credential store; they
// are never persisted in the browser and never returned by these APIs.

export interface TripoStatus {
  configured: boolean;
  source: 'env' | 'store' | 'none';
  baseUrl: string;
  keyHint: string;
  credentialStore: string;
}

interface TripoStatusResponse {
  ok: boolean;
  tripo: TripoStatus;
  error?: { code: string; message: string } | string;
}

async function readError(response: Response): Promise<never> {
  const raw = await response.text();
  let parsed: { error?: { message?: string } | string } | null = null;
  try {
    parsed = JSON.parse(raw) as { error?: { message?: string } | string };
  } catch {
    // Fall through to the generic HTTP error below.
  }
  if (typeof parsed?.error === 'object' && parsed.error?.message) throw new Error(parsed.error.message);
  if (typeof parsed?.error === 'string') throw new Error(parsed.error);
  throw new Error(`HTTP ${response.status}: ${raw || response.statusText}`);
}

async function request(path: string, init?: RequestInit): Promise<TripoStatus> {
  const response = await fetch(`/api/editor-bridge/third-party${path}`, init);
  if (!response.ok) return readError(response);
  const body = (await response.json()) as TripoStatusResponse;
  return body.tripo;
}

export async function getTripoStatus(): Promise<TripoStatus> {
  return request('/tripo/status');
}

export async function saveTripoKey(apiKey: string, baseUrl?: string): Promise<TripoStatus> {
  return request('/tripo/config', {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ apiKey, ...(baseUrl ? { baseUrl } : {}) }),
  });
}

export async function clearTripoKey(): Promise<TripoStatus> {
  return request('/tripo/config', { method: 'DELETE' });
}

export interface TripoGenerateRequest {
  /** Public http(s) URL; when set, the server skips uploading bytes. */
  imageUrl?: string;
  /** Web-editor texture storage string (pcg-resource://…, /assets/…, workspace-relative). */
  texturePath?: string;
  imageBase64?: string;
  modelVersion?: string;
  texture?: boolean;
  pbr?: boolean;
  faceLimit?: number;
}

export interface TripoGenerateResult {
  ok: boolean;
  /** Workspace-relative GLB path — assign it to the node's `path` property. */
  path?: string;
  taskId?: string;
  creditsConsumed?: number;
  cached?: boolean;
  stub?: boolean;
  bytes?: number;
}

export interface TripoGenerateProgress {
  /** 0..1 */
  percent: number;
  message: string;
}

export async function generateTripoMesh(
  input: TripoGenerateRequest,
  onProgress?: (progress: TripoGenerateProgress) => void,
): Promise<TripoGenerateResult> {
  const response = await fetch('/api/editor-bridge/third-party/tripo/generate', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(input),
  });
  if (!response.ok) return readError(response);
  if (!response.body) throw new Error('Tripo generate stream is unavailable');

  // The server streams `event: progress` updates and one terminal
  // `event: result` with the ok/error payload (SSE over chunked HTTP).
  const reader = response.body.getReader();
  const decoder = new TextDecoder();
  let buffer = '';
  let finalResult: (TripoGenerateResult & { error?: { message?: string } | string }) | null = null;

  const flush = (block: string) => {
    let type = '';
    const data: string[] = [];
    for (const line of block.split(/\r?\n/)) {
      if (line.startsWith('event:')) type = line.slice(6).trim();
      if (line.startsWith('data:')) data.push(line.slice(5).trimStart());
    }
    if (!type || data.length === 0) return;
    let parsed: Record<string, unknown>;
    try {
      parsed = JSON.parse(data.join('\n')) as Record<string, unknown>;
    } catch {
      return;
    }
    if (type === 'progress') {
      onProgress?.({
        percent: typeof parsed.percent === 'number' ? parsed.percent : 0,
        message: typeof parsed.message === 'string' ? parsed.message : '',
      });
    } else if (type === 'result') {
      finalResult = parsed as unknown as NonNullable<typeof finalResult>;
    }
  };

  while (true) {
    const chunk = await reader.read();
    buffer += decoder.decode(chunk.value, { stream: !chunk.done });
    let boundary = buffer.search(/\r?\n\r?\n/);
    while (boundary >= 0) {
      const block = buffer.slice(0, boundary);
      const match = buffer.slice(boundary).match(/^\r?\n\r?\n/);
      buffer = buffer.slice(boundary + (match?.[0].length ?? 2));
      flush(block);
      boundary = buffer.search(/\r?\n\r?\n/);
    }
    if (chunk.done) break;
  }
  if (buffer.trim()) flush(buffer);

  if (!finalResult) throw new Error('Tripo generate ended without a result.');
  const result = finalResult as TripoGenerateResult & { error?: { message?: string } | string };
  if (!result.ok) {
    if (typeof result.error === 'object' && result.error?.message) throw new Error(result.error.message);
    if (typeof result.error === 'string') throw new Error(result.error);
    throw new Error('Tripo generation failed.');
  }
  return result;
}

export interface OrientedSdfBakeOptions {
  cellSize?: number;
  /** Triangle-interior measurement spacing; defaults to 0.75 × cellSize. */
  sampleSpacing?: number;
  supportRadiusCells?: number;
  isoOffset?: number;
  maxActiveCells?: number;
  transferColors?: boolean;
  transferUvs?: boolean;
  flipUvV?: boolean;
  title?: string;
  componentId?: string;
}

export interface OrientedSdfNodeDataResult {
  ok: true;
  algorithm: 'dense-oriented-mls-sdf-surface-nets';
  topologyCopied: false;
  sourcePointCount: number;
  sourceVertexCount: number;
  sampleSpacing: number;
  payloadBytes: number;
  hasSourceVertexColors: boolean;
  hasSourceUvs: boolean;
  embeddedTextureBytes: { baseColor: number; normal: number; orm: number };
  bounds: { min: [number, number, number]; max: [number, number, number] };
  nodeType: 'OrientedSdfSurface';
  nodeData: Record<string, unknown>;
  suggestedMaterialData: Record<string, unknown>;
}

/**
 * Measure a source mesh into the topology-free OPC1 payload consumed by the
 * native OrientedSdfSurface node. The GLB itself remains a reference only.
 */
export async function buildOrientedSdfNodeData(
  sourcePath: string,
  options: OrientedSdfBakeOptions = {},
): Promise<OrientedSdfNodeDataResult> {
  const response = await fetch('/api/editor-bridge/reconstruct/oriented-sdf', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ sourcePath, ...options }),
  });
  if (!response.ok) return readError(response);
  const result = await response.json() as OrientedSdfNodeDataResult;
  if (!result.ok || result.nodeType !== 'OrientedSdfSurface' ||
      !result.nodeData || typeof result.nodeData !== 'object') {
    throw new Error('The reconstruction service returned an invalid OrientedSdfSurface payload.');
  }
  return result;
}
