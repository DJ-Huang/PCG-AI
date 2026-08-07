// cookResult.ts — Parse pcg-server /v1/cook binary responses.
// SSOT for the wire formats:
//   envelope:  pcg-server/src/cook_service.cpp (EncodeResult, 'PCGR' v1)
//   geometry:  pcg-core/src/data/pcg_geometry_binary.cpp ('PCGG' v2/v3)
//   points:    pcg-core/src/data/pcg_point_binary.cpp ('PGTP' v1/v2)
// Kept three.js-free so the golden-fixture contract test can run under node.

export const COOK_RESULT_MAGIC = 0x52474350; // 'PCGR'
export const COOK_RESULT_VERSION = 1;
export const GEOMETRY_MAGIC = 0x47475043; // 'PCGG'
export const GEOMETRY_VERSION = 3;
export const GEOMETRY_PREVIOUS_VERSION = 2;
export const POINT_BINARY_MAGIC = 0x50544750; // 'PGTP'
export const MESH_BINARY_MAGIC = 0x4d474350; // 'PCGM'

export const PcgExecuteKind = {
  None: 0,
  Json: 1,
  Mesh: 2,
  Points: 3,
} as const;
export type PcgExecuteKind = (typeof PcgExecuteKind)[keyof typeof PcgExecuteKind];

export interface CookResult {
  code: number;
  kind: PcgExecuteKind;
  nodesExecuted: number;
  nodesSkipped: number;
  graphExecuteMs: number;
  binaryWriteMs: number;
  pointCount: number;
  pointAttrFlags: number;
  vertexCount: number;
  indexCount: number;
  error: string;
  json: string;
  mesh: Uint8Array;
  points: Uint8Array;
  geometry: Uint8Array;
  heightfield: Uint8Array;
  perf: string;
}

export interface ParsedGeometry {
  positions: Float32Array; // xyz per point
  faceOffsets: Uint32Array; // per-face start into faceIndices
  faceIndices: Uint32Array; // polygon vertex indices
  triangles: Uint32Array; // corner-domain triangulation (chunk 6), may be empty.
                          // Indices address flat-shading duplicated vertices
                          // (one per face corner), NOT `positions`. Use the
                          // PCGM mesh blob for triangle rendering.
  colors: Float32Array | null; // rgba per point
  uvs: Float32Array | null; // uv per point
  pointCount: number;
  faceCount: number;
}

const utf8 = new TextDecoder();

function readBlob(view: DataView, state: { offset: number }): Uint8Array {
  if (state.offset + 4 > view.byteLength) throw new Error('Truncated blob length');
  const len = view.getUint32(state.offset, true);
  state.offset += 4;
  if (state.offset + len > view.byteLength) throw new Error('Truncated blob payload');
  const slice = new Uint8Array(view.buffer, view.byteOffset + state.offset, len);
  state.offset += len;
  return slice;
}

export function parseCookResult(buffer: ArrayBuffer): CookResult {
  if (buffer.byteLength < 48) throw new Error('Cook result too short');
  const view = new DataView(buffer);
  let offset = 0;
  const magic = view.getUint32(offset, true); offset += 4;
  const version = view.getUint32(offset, true); offset += 4;
  if (magic !== COOK_RESULT_MAGIC) {
    throw new Error(`Bad cook result magic 0x${magic.toString(16).padStart(8, '0')}`);
  }
  if (version !== COOK_RESULT_VERSION) {
    throw new Error(`Unsupported cook result version ${version} (expected ${COOK_RESULT_VERSION})`);
  }
  const code = view.getInt32(offset, true); offset += 4;
  const kind = view.getUint32(offset, true) as PcgExecuteKind; offset += 4;
  const nodesExecuted = view.getInt32(offset, true); offset += 4;
  const nodesSkipped = view.getInt32(offset, true); offset += 4;
  const graphExecuteMs = view.getFloat64(offset, true); offset += 8;
  const binaryWriteMs = view.getFloat64(offset, true); offset += 8;
  const pointCount = view.getUint32(offset, true); offset += 4;
  const pointAttrFlags = view.getUint32(offset, true); offset += 4;
  const vertexCount = view.getInt32(offset, true); offset += 4;
  const indexCount = view.getInt32(offset, true); offset += 4;

  const state = { offset };
  const errorBytes = readBlob(view, state);
  const jsonBytes = readBlob(view, state);
  const mesh = readBlob(view, state);
  const points = readBlob(view, state);
  const geometry = readBlob(view, state);
  const heightfield = readBlob(view, state);
  const perfBytes = readBlob(view, state);

  return {
    code,
    kind,
    nodesExecuted,
    nodesSkipped,
    graphExecuteMs,
    binaryWriteMs,
    pointCount,
    pointAttrFlags,
    vertexCount,
    indexCount,
    error: utf8.decode(errorBytes),
    json: utf8.decode(jsonBytes),
    mesh,
    points,
    geometry,
    heightfield,
    perf: utf8.decode(perfBytes),
  };
}

// Geometry binary chunk ids (pcg_geometry_binary.cpp)
const CHUNK_POINTS = 1;
const CHUNK_FACE_OFFSETS = 2;
const CHUNK_FACE_INDICES = 3;
const CHUNK_TRIANGULATION = 6;
const CHUNK_COLORS = 7;
const CHUNK_UVS = 8;

export function parseGeometryBinary(data: Uint8Array): ParsedGeometry {
  if (data.byteLength < 16) throw new Error('Geometry binary payload is too small');
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
  const magic = view.getUint32(0, true);
  if (magic !== GEOMETRY_MAGIC) {
    throw new Error(`Invalid geometry binary magic 0x${magic.toString(16).padStart(8, '0')}`);
  }
  const version = view.getUint32(4, true);
  if (version !== GEOMETRY_VERSION && version !== GEOMETRY_PREVIOUS_VERSION) {
    throw new Error(`Unsupported geometry binary version ${version}`);
  }
  const pointCount = view.getUint32(8, true);
  const faceCount = view.getUint32(12, true);

  let positions: Float32Array | null = null;
  let faceOffsets: Uint32Array | null = null;
  let faceIndices: Uint32Array | null = null;
  let triangles: Uint32Array = new Uint32Array(0);
  let colors: Float32Array | null = null;
  let uvs: Float32Array | null = null;

  let offset = 16;
  while (offset + 8 <= data.byteLength) {
    const chunkId = view.getUint32(offset, true);
    const chunkSize = view.getUint32(offset + 4, true);
    offset += 8;
    const chunkEnd = offset + chunkSize;
    if (chunkEnd > data.byteLength) throw new Error('Geometry chunk extends past buffer');

    if (chunkId === CHUNK_POINTS) {
      if (positions) throw new Error('Duplicate POINTS chunk');
      if (chunkSize !== pointCount * 12) {
        throw new Error(`POINTS chunk bytes mismatch: ${chunkSize} != ${pointCount * 12}`);
      }
      positions = new Float32Array(pointCount * 3);
      for (let i = 0; i < pointCount * 3; i++) {
        positions[i] = view.getFloat32(offset + i * 4, true);
      }
    } else if (chunkId === CHUNK_FACE_OFFSETS) {
      if (faceOffsets) throw new Error('Duplicate FACE_OFFSETS chunk');
      if (chunkSize !== faceCount * 4) {
        throw new Error(`FACE_OFFSETS chunk bytes mismatch: ${chunkSize} != ${faceCount * 4}`);
      }
      faceOffsets = new Uint32Array(faceCount);
      for (let i = 0; i < faceCount; i++) {
        faceOffsets[i] = view.getUint32(offset + i * 4, true);
      }
    } else if (chunkId === CHUNK_FACE_INDICES) {
      if (faceIndices) throw new Error('Duplicate FACE_INDICES chunk');
      if (chunkSize % 4 !== 0) throw new Error('FACE_INDICES chunk size not a multiple of 4');
      const count = chunkSize / 4;
      faceIndices = new Uint32Array(count);
      for (let i = 0; i < count; i++) {
        faceIndices[i] = view.getUint32(offset + i * 4, true);
      }
    } else if (chunkId === CHUNK_TRIANGULATION) {
      if (chunkSize % 4 !== 0) throw new Error('TRIANGULATION chunk size not a multiple of 4');
      const count = chunkSize / 4;
      triangles = new Uint32Array(count);
      for (let i = 0; i < count; i++) {
        triangles[i] = view.getUint32(offset + i * 4, true);
      }
    } else if (chunkId === CHUNK_COLORS) {
      if (chunkSize % 16 !== 0) throw new Error('COLORS chunk size not a multiple of 16');
      colors = new Float32Array(chunkSize / 4);
      for (let i = 0; i < colors.length; i++) {
        colors[i] = view.getFloat32(offset + i * 4, true);
      }
    } else if (chunkId === CHUNK_UVS) {
      if (chunkSize % 8 !== 0) throw new Error('UVS chunk size not a multiple of 8');
      uvs = new Float32Array(chunkSize / 4);
      for (let i = 0; i < uvs.length; i++) {
        uvs[i] = view.getFloat32(offset + i * 4, true);
      }
    }
    // GROUPS / MATERIAL / ATTRIBUTES / unknown chunks: skip by size.

    offset = chunkEnd;
  }

  if (!positions) throw new Error('Missing POINTS chunk');
  if (!faceOffsets) throw new Error('Missing FACE_OFFSETS chunk');
  if (!faceIndices) throw new Error('Missing FACE_INDICES chunk');

  if (faceCount > 0 && faceOffsets[0] !== 0) throw new Error('FACE_OFFSETS[0] must be 0');
  for (let i = 0; i < faceCount; i++) {
    const start = faceOffsets[i];
    const end = i + 1 < faceCount ? faceOffsets[i + 1] : faceIndices.length;
    if (start > end || end > faceIndices.length) throw new Error('FACE_OFFSETS out of range');
    if (i + 1 < faceCount && faceOffsets[i + 1] <= start) {
      throw new Error('FACE_OFFSETS must be strictly increasing');
    }
  }

  return {
    positions,
    faceOffsets,
    faceIndices,
    triangles,
    colors,
    uvs,
    pointCount,
    faceCount,
  };
}

export function parsePointBinary(data: Uint8Array): Float32Array {
  if (data.byteLength < 16) throw new Error('Point binary payload is too small');
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
  const magic = view.getUint32(0, true);
  if (magic !== POINT_BINARY_MAGIC) {
    throw new Error(`Invalid point binary magic 0x${magic.toString(16).padStart(8, '0')}`);
  }
  const pointCount = view.getUint32(8, true);
  const positionsBytes = pointCount * 12;
  if (16 + positionsBytes > data.byteLength) throw new Error('Point binary positions truncated');
  const positions = new Float32Array(pointCount * 3);
  for (let i = 0; i < pointCount * 3; i++) {
    positions[i] = view.getFloat32(16 + i * 4, true);
  }
  return positions;
}

// Mesh binary ('PCGM', pcg_mesh_binary.cpp): flat-shading duplicated vertices
// with optional normals/colors/uvs — the same payload Unity uses for preview Meshes.
export interface ParsedMesh {
  positions: Float32Array; // xyz per vertex
  indices: Uint32Array; // triangle indices
  normals: Float32Array | null;
  colors: Float32Array | null;
  uvs: Float32Array | null;
  vertexCount: number;
  indexCount: number;
}

/** Cook-result spline polyline (pcg-core PcgSplineData::to_json). */
export interface ParsedSpline {
  points: Float32Array; // xyz per control/sample point
  closed: boolean;
}

export interface ParsedSplines {
  splines: ParsedSpline[];
}

const MESH_FLAG_NORMALS = 0x1;
const MESH_FLAG_COLORS = 0x2;
const MESH_FLAG_UVS = 0x4;

export function parseMeshBinary(data: Uint8Array): ParsedMesh {
  if (data.byteLength < 16) throw new Error('Mesh binary payload is too small');
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
  const magic = view.getUint32(0, true);
  if (magic !== MESH_BINARY_MAGIC) {
    throw new Error(`Invalid mesh binary magic 0x${magic.toString(16).padStart(8, '0')}`);
  }
  const version = view.getUint32(4, true);
  if (version !== 2 && version !== 3) {
    throw new Error(`Unsupported mesh binary version ${version}`);
  }
  const vertexCount = view.getUint32(8, true);
  const indexCount = view.getUint32(12, true);
  let flags = 0;
  let offset = 16;
  if (version >= 2) {
    flags = view.getUint32(offset, true);
    offset += 4;
  }
  if (version === 3) {
    offset += 4; // material section size — materials are out of preview scope
  }

  const need = offset + vertexCount * 12 + indexCount * 4;
  if (need > data.byteLength) throw new Error('Mesh binary positions/indices truncated');
  const positions = new Float32Array(vertexCount * 3);
  for (let i = 0; i < vertexCount * 3; i++) {
    positions[i] = view.getFloat32(offset + i * 4, true);
  }
  offset += vertexCount * 12;
  const indices = new Uint32Array(indexCount);
  for (let i = 0; i < indexCount; i++) {
    indices[i] = view.getUint32(offset + i * 4, true);
  }
  offset += indexCount * 4;

  const readFloatBlock = (count: number, what: string): Float32Array => {
    if (offset + count * 4 > data.byteLength) throw new Error(`Mesh binary ${what} truncated`);
    const out = new Float32Array(count);
    for (let i = 0; i < count; i++) {
      out[i] = view.getFloat32(offset + i * 4, true);
    }
    offset += count * 4;
    return out;
  };

  const normals = (flags & MESH_FLAG_NORMALS) !== 0 ? readFloatBlock(vertexCount * 3, 'normals') : null;
  const colors = (flags & MESH_FLAG_COLORS) !== 0 ? readFloatBlock(vertexCount * 4, 'colors') : null;
  const uvs = (flags & MESH_FLAG_UVS) !== 0 ? readFloatBlock(vertexCount * 2, 'uvs') : null;

  return { positions, indices, normals, colors, uvs, vertexCount, indexCount };
}

/** Parse spline payload from cook-result JSON blob (Unity PcgResultParser.TryParseSplines). */
export function parseSplineJson(json: string): ParsedSplines | null {
  if (!json.trim()) return null;
  try {
    const payload = JSON.parse(json) as { splines?: unknown };
    if (!Array.isArray(payload.splines)) return null;

    const splines: ParsedSpline[] = [];
    for (const entry of payload.splines) {
      if (!entry || typeof entry !== 'object') continue;
      const rec = entry as { points?: unknown; closed?: unknown };
      if (!Array.isArray(rec.points) || rec.points.length === 0) continue;

      const positions = new Float32Array(rec.points.length * 3);
      for (let i = 0; i < rec.points.length; i++) {
        const pt = rec.points[i] as Record<string, unknown> | null;
        positions[i * 3] = Number(pt?.x) || 0;
        positions[i * 3 + 1] = Number(pt?.y) || 0;
        positions[i * 3 + 2] = Number(pt?.z) || 0;
      }
      splines.push({
        points: positions,
        closed: rec.closed === true,
      });
    }

    return splines.length > 0 ? { splines } : null;
  } catch {
    return null;
  }
}

/**
 * Deduplicated line-segment index pairs from polygon face loops.
 * Faces: 1 index = isolated point (no edge), 2 = single edge, 3+ = closed loop.
 */
export function buildEdgeIndices(geometry: ParsedGeometry): Uint32Array {
  const seen = new Set<string>();
  const edges: number[] = [];
  const push = (a: number, b: number) => {
    const key = a < b ? `${a}_${b}` : `${b}_${a}`;
    if (seen.has(key)) return;
    seen.add(key);
    edges.push(a, b);
  };
  const { faceOffsets, faceIndices, faceCount } = geometry;
  for (let f = 0; f < faceCount; f++) {
    const start = faceOffsets[f];
    const end = f + 1 < faceCount ? faceOffsets[f + 1] : faceIndices.length;
    const n = end - start;
    if (n === 2) {
      push(faceIndices[start], faceIndices[start + 1]);
    } else if (n >= 3) {
      for (let i = start; i < end; i++) {
        push(faceIndices[i], faceIndices[i + 1 < end ? i + 1 : start]);
      }
    }
  }
  return new Uint32Array(edges);
}
