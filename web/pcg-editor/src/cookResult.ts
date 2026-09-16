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
export const HEIGHTFIELD_BINARY_MAGIC = 0x48474350; // 'PCGH'
export const HEIGHTFIELD_BINARY_VERSION = 1;

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

// Block reinterpret helpers: the wire format is little-endian and every
// supported platform (macOS/Windows, x64/arm64) is little-endian, so copying a
// byte range into a fresh aligned buffer and viewing it as typed elements is
// value-identical to per-element DataView reads, at memcpy speed. Callers
// bounds-check before calling; slice() guarantees a 4-aligned fresh buffer.
function copyFloat32(source: Uint8Array, byteOffset: number, count: number): Float32Array {
  return new Float32Array(source.slice(byteOffset, byteOffset + count * 4).buffer);
}

function copyUint32(source: Uint8Array, byteOffset: number, count: number): Uint32Array {
  return new Uint32Array(source.slice(byteOffset, byteOffset + count * 4).buffer);
}

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
      positions = copyFloat32(data, offset, pointCount * 3);
    } else if (chunkId === CHUNK_FACE_OFFSETS) {
      if (faceOffsets) throw new Error('Duplicate FACE_OFFSETS chunk');
      if (chunkSize !== faceCount * 4) {
        throw new Error(`FACE_OFFSETS chunk bytes mismatch: ${chunkSize} != ${faceCount * 4}`);
      }
      faceOffsets = copyUint32(data, offset, faceCount);
    } else if (chunkId === CHUNK_FACE_INDICES) {
      if (faceIndices) throw new Error('Duplicate FACE_INDICES chunk');
      if (chunkSize % 4 !== 0) throw new Error('FACE_INDICES chunk size not a multiple of 4');
      faceIndices = copyUint32(data, offset, chunkSize / 4);
    } else if (chunkId === CHUNK_TRIANGULATION) {
      if (chunkSize % 4 !== 0) throw new Error('TRIANGULATION chunk size not a multiple of 4');
      triangles = copyUint32(data, offset, chunkSize / 4);
    } else if (chunkId === CHUNK_COLORS) {
      if (chunkSize % 16 !== 0) throw new Error('COLORS chunk size not a multiple of 16');
      colors = copyFloat32(data, offset, chunkSize / 4);
    } else if (chunkId === CHUNK_UVS) {
      if (chunkSize % 8 !== 0) throw new Error('UVS chunk size not a multiple of 8');
      uvs = copyFloat32(data, offset, chunkSize / 4);
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
  return copyFloat32(data, 16, pointCount * 3);
}

// Mesh binary ('PCGM', pcg_mesh_binary.cpp): flat-shading duplicated vertices
// with optional normals/colors/uvs — the same payload Unity uses for preview Meshes.
export interface ParsedMesh {
  positions: Float32Array; // xyz per vertex
  indices: Uint32Array; // triangle indices
  normals: Float32Array | null;
  colors: Float32Array | null;
  uvs: Float32Array | null;
  /** PCGM v3 material slot names in Unity submesh order. */
  materialSlots: string[];
  /** Per-triangle slot index (length = indexCount / 3) for PCGM v3. */
  triangleMaterials: Uint32Array | null;
  vertexCount: number;
  indexCount: number;
}

export const HeightFieldSampling = {
  Center: 0,
  Corner: 1,
} as const;
export type HeightFieldSampling = (typeof HeightFieldSampling)[keyof typeof HeightFieldSampling];

export const HeightFieldOrientation = {
  ZX: 0,
  XY: 1,
  YZ: 2,
} as const;
export type HeightFieldOrientation = (typeof HeightFieldOrientation)[keyof typeof HeightFieldOrientation];

export interface ParsedHeightFieldLayer {
  name: string;
  tupleSize: number;
  values: Float32Array;
}

/** Typed PCGH payload from pcg-core (pcg_heightfield_binary.cpp). */
export interface ParsedHeightField {
  resolutionX: number;
  resolutionZ: number;
  sizeX: number;
  sizeZ: number;
  centerX: number;
  centerY: number;
  centerZ: number;
  sampling: HeightFieldSampling;
  orientation: HeightFieldOrientation;
  layers: ParsedHeightFieldLayer[];
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
const MESH_FLAG_MATERIALS = 0x8;

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
  let materialSectionSize = 0;
  if (version === 3) {
    materialSectionSize = view.getUint32(offset, true);
    offset += 4;
  }

  const need = offset + vertexCount * 12 + indexCount * 4;
  if (need > data.byteLength) throw new Error('Mesh binary positions/indices truncated');
  const positions = copyFloat32(data, offset, vertexCount * 3);
  offset += vertexCount * 12;
  const indices = copyUint32(data, offset, indexCount);
  offset += indexCount * 4;

  const readFloatBlock = (count: number, what: string): Float32Array => {
    if (offset + count * 4 > data.byteLength) throw new Error(`Mesh binary ${what} truncated`);
    const out = copyFloat32(data, offset, count);
    offset += count * 4;
    return out;
  };

  const normals = (flags & MESH_FLAG_NORMALS) !== 0 ? readFloatBlock(vertexCount * 3, 'normals') : null;
  const colors = (flags & MESH_FLAG_COLORS) !== 0 ? readFloatBlock(vertexCount * 4, 'colors') : null;
  const uvs = (flags & MESH_FLAG_UVS) !== 0 ? readFloatBlock(vertexCount * 2, 'uvs') : null;

  const materialSlots: string[] = [];
  let triangleMaterials: Uint32Array | null = null;
  if ((flags & MESH_FLAG_MATERIALS) !== 0) {
    if (version !== 3 || materialSectionSize < 4 || offset + materialSectionSize > data.byteLength) {
      throw new Error('Mesh binary material section is invalid');
    }
    const sectionEnd = offset + materialSectionSize;
    const slotCount = view.getUint32(offset, true);
    offset += 4;
    if (slotCount > 65536) throw new Error('Mesh binary material slot count is unreasonable');
    for (let slot = 0; slot < slotCount; slot++) {
      if (offset + 4 > sectionEnd) throw new Error('Mesh binary material name length truncated');
      const byteLength = view.getUint32(offset, true);
      offset += 4;
      if (offset + byteLength > sectionEnd) throw new Error('Mesh binary material name truncated');
      materialSlots.push(utf8.decode(data.subarray(offset, offset + byteLength)));
      offset += byteLength;
    }
    const triangleCount = indexCount / 3;
    if (!Number.isInteger(triangleCount) || offset + triangleCount * 4 !== sectionEnd) {
      throw new Error('Mesh binary triangle material table size mismatch');
    }
    triangleMaterials = copyUint32(data, offset, triangleCount);
    for (const slot of triangleMaterials) {
      if (slot >= materialSlots.length) throw new Error('Mesh binary triangle material slot out of range');
    }
    offset = sectionEnd;
  } else if (materialSectionSize !== 0) {
    throw new Error('Mesh binary contains an unexpected material section');
  }

  return {
    positions,
    indices,
    normals,
    colors,
    uvs,
    materialSlots,
    triangleMaterials,
    vertexCount,
    indexCount,
  };
}

function readU32(view: DataView, offset: number): number {
  return view.getUint32(offset, true);
}

function readI32(view: DataView, offset: number): number {
  return view.getInt32(offset, true);
}

function readF64(view: DataView, offset: number): number {
  return view.getFloat64(offset, true);
}

/** Parse PCGH heightfield binary (pcg_heightfield_binary.cpp). */
export function parseHeightFieldBinary(data: Uint8Array): ParsedHeightField {
  if (data.byteLength < 72) throw new Error('HeightField binary payload is too small');
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
  const magic = readU32(view, 0);
  const version = readU32(view, 4);
  if (magic !== HEIGHTFIELD_BINARY_MAGIC) {
    throw new Error(`Invalid heightfield binary magic 0x${magic.toString(16).padStart(8, '0')}`);
  }
  if (version !== HEIGHTFIELD_BINARY_VERSION) {
    throw new Error(`Unsupported heightfield binary version ${version}`);
  }

  const resolutionX = readI32(view, 8);
  const resolutionZ = readI32(view, 12);
  const layerCount = readI32(view, 16);
  const sampling = readU32(view, 20) as HeightFieldSampling;
  const orientation = readU32(view, 24) as HeightFieldOrientation;
  const sizeX = readF64(view, 32);
  const sizeZ = readF64(view, 40);
  const centerX = readF64(view, 48);
  const centerY = readF64(view, 56);
  const centerZ = readF64(view, 64);

  if (
    resolutionX < 2 || resolutionZ < 2 || layerCount < 0 || layerCount > 4096 ||
    sampling > HeightFieldSampling.Corner || orientation > HeightFieldOrientation.YZ ||
    !Number.isFinite(sizeX) || !Number.isFinite(sizeZ) || sizeX <= 0 || sizeZ <= 0
  ) {
    throw new Error('HeightField binary metadata is invalid');
  }

  const sampleCount = resolutionX * resolutionZ;
  const layers: ParsedHeightFieldLayer[] = [];
  let offset = 72;
  for (let i = 0; i < layerCount; i++) {
    if (offset + 20 > data.byteLength) throw new Error(`HeightField layer ${i} header truncated`);
    const nameBytes = readU32(view, offset); offset += 4;
    const tupleSize = readI32(view, offset); offset += 4;
    offset += 8; // border type + border value
    const valueCount = readU32(view, offset); offset += 4;
    if (
      nameBytes === 0 || tupleSize <= 0 || tupleSize > 64 ||
      valueCount !== sampleCount * tupleSize ||
      offset + nameBytes + valueCount * 4 > data.byteLength
    ) {
      throw new Error(`HeightField layer ${i} metadata is invalid`);
    }
    const name = utf8.decode(data.subarray(offset, offset + nameBytes));
    offset += nameBytes;
    const values = copyFloat32(data, offset, valueCount);
    offset += valueCount * 4;
    layers.push({ name, tupleSize, values });
  }

  return {
    resolutionX,
    resolutionZ,
    sizeX,
    sizeZ,
    centerX,
    centerY,
    centerZ,
    sampling,
    orientation,
    layers,
  };
}

function heightFieldLayer(
  heightfield: ParsedHeightField,
  layerName: string,
): ParsedHeightFieldLayer | null {
  return heightfield.layers.find((layer) => layer.name === layerName && layer.tupleSize === 1) ?? null;
}

function heightFieldSpacing(heightfield: ParsedHeightField): { spacingX: number; spacingZ: number } {
  const divisionsX =
    heightfield.sampling === HeightFieldSampling.Corner
      ? heightfield.resolutionX - 1
      : heightfield.resolutionX;
  const divisionsZ =
    heightfield.sampling === HeightFieldSampling.Corner
      ? heightfield.resolutionZ - 1
      : heightfield.resolutionZ;
  return {
    spacingX: divisionsX > 0 ? heightfield.sizeX / divisionsX : 0,
    spacingZ: divisionsZ > 0 ? heightfield.sizeZ / divisionsZ : 0,
  };
}

/** World position of one heightfield sample (pcg_heightfield.cpp::sample_position). */
export function heightFieldSamplePosition(
  heightfield: ParsedHeightField,
  x: number,
  z: number,
  height: number,
): [number, number, number] {
  const sampleOffset = heightfield.sampling === HeightFieldSampling.Center ? 0.5 : 0;
  const { spacingX, spacingZ } = heightFieldSpacing(heightfield);
  const u = -heightfield.sizeX * 0.5 + (x + sampleOffset) * spacingX;
  const v = -heightfield.sizeZ * 0.5 + (z + sampleOffset) * spacingZ;
  switch (heightfield.orientation) {
    case HeightFieldOrientation.XY:
      return [heightfield.centerX + u, heightfield.centerY + v, heightfield.centerZ + height];
    case HeightFieldOrientation.YZ:
      return [heightfield.centerX + height, heightfield.centerY + u, heightfield.centerZ + v];
    case HeightFieldOrientation.ZX:
    default:
      return [heightfield.centerX + u, heightfield.centerY + height, heightfield.centerZ + v];
  }
}

/**
 * Convert a heightfield to a render mesh (ConvertHeightField density=1 parity).
 * ponytail: preview caps at 512×512; full-res terrain should use ConvertHeightField in-graph.
 */
export function buildHeightFieldPreviewMesh(
  heightfield: ParsedHeightField,
  layerName = 'height',
  maxSamples = 512,
): ParsedMesh {
  const heightLayer = heightFieldLayer(heightfield, layerName);
  if (!heightLayer) {
    throw new Error(`HeightField is missing scalar layer '${layerName}'`);
  }

  let { resolutionX, resolutionZ } = heightfield;
  const sampleCount = resolutionX * resolutionZ;
  if (sampleCount > maxSamples * maxSamples) {
    const scale = Math.sqrt(sampleCount / (maxSamples * maxSamples));
    resolutionX = Math.max(2, Math.round(resolutionX / scale));
    resolutionZ = Math.max(2, Math.round(resolutionZ / scale));
  }

  const vertexCount = resolutionX * resolutionZ;
  const positions = new Float32Array(vertexCount * 3);
  const sampleHeight = (x: number, z: number): number => {
    const gx = (x / Math.max(1, resolutionX - 1)) * (heightfield.resolutionX - 1);
    const gz = (z / Math.max(1, resolutionZ - 1)) * (heightfield.resolutionZ - 1);
    const x0 = Math.floor(gx);
    const z0 = Math.floor(gz);
    const tx = gx - x0;
    const tz = gz - z0;
    const idx = (ix: number, iz: number) => iz * heightfield.resolutionX + ix;
    const fetch = (ix: number, iz: number) => heightLayer.values[idx(ix, iz)] ?? 0;
    const v00 = fetch(x0, z0);
    const v10 = fetch(Math.min(heightfield.resolutionX - 1, x0 + 1), z0);
    const v01 = fetch(x0, Math.min(heightfield.resolutionZ - 1, z0 + 1));
    const v11 = fetch(
      Math.min(heightfield.resolutionX - 1, x0 + 1),
      Math.min(heightfield.resolutionZ - 1, z0 + 1),
    );
    const vx0 = v00 + (v10 - v00) * tx;
    const vx1 = v01 + (v11 - v01) * tx;
    return vx0 + (vx1 - vx0) * tz;
  };

  const previewGrid = {
    ...heightfield,
    resolutionX,
    resolutionZ,
    sizeX: heightfield.sizeX,
    sizeZ: heightfield.sizeZ,
  };

  for (let z = 0; z < resolutionZ; z++) {
    for (let x = 0; x < resolutionX; x++) {
      const height = sampleHeight(x, z);
      const [px, py, pz] = heightFieldSamplePosition(previewGrid, x, z, height);
      const o = (z * resolutionX + x) * 3;
      positions[o] = px;
      positions[o + 1] = py;
      positions[o + 2] = pz;
    }
  }

  const quadCount = (resolutionX - 1) * (resolutionZ - 1);
  const indices = new Uint32Array(quadCount * 6);
  let write = 0;
  for (let z = 0; z + 1 < resolutionZ; z++) {
    for (let x = 0; x + 1 < resolutionX; x++) {
      const i00 = z * resolutionX + x;
      const i10 = i00 + 1;
      const i01 = (z + 1) * resolutionX + x;
      const i11 = i01 + 1;
      if (heightfield.orientation === HeightFieldOrientation.ZX) {
        indices[write++] = i00;
        indices[write++] = i01;
        indices[write++] = i11;
        indices[write++] = i00;
        indices[write++] = i11;
        indices[write++] = i10;
      } else {
        indices[write++] = i00;
        indices[write++] = i10;
        indices[write++] = i11;
        indices[write++] = i00;
        indices[write++] = i11;
        indices[write++] = i01;
      }
    }
  }

  return {
    positions,
    indices,
    normals: null,
    colors: null,
    uvs: null,
    materialSlots: [],
    triangleMaterials: null,
    vertexCount,
    indexCount: indices.length,
  };
}

/** Recover cook JSON when pcg-server left the summary in the points blob. */
export function extractCookJsonText(cook: CookResult): string {
  if (cook.json.trim()) return cook.json;
  if (cook.points.length > 0 && cook.points[0] === 0x7b) {
    try {
      return utf8.decode(cook.points);
    } catch {
      return '';
    }
  }
  return '';
}

export function isHeightFieldCookJson(json: string): boolean {
  if (!json.trim()) return false;
  try {
    const payload = JSON.parse(json) as { kind?: unknown };
    return payload.kind === 'heightfield';
  } catch {
    return false;
  }
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
 * Texture descriptor emitted by ImageTexture/MeshyImageGen when the preview
 * target outputs a Texture pin (pcg-core mesh_elements.cpp). `source` is the
 * node data storage string (pcg-resource://…, /assets/… or URL), empty when
 * the node has no image baked yet.
 */
export interface ParsedTexture {
  slotId: string;
  source: string;
  repeatX: number;
  repeatY: number;
}

export function parseTextureJson(json: string): ParsedTexture | null {
  if (!json.trim()) return null;
  try {
    const payload = JSON.parse(json) as {
      kind?: unknown;
      slotId?: unknown;
      source?: unknown;
      repeatX?: unknown;
      repeatY?: unknown;
    };
    if (payload.kind !== 'texture' || typeof payload.slotId !== 'string') return null;
    return {
      slotId: payload.slotId,
      source: typeof payload.source === 'string' ? payload.source : '',
      repeatX: Number.isFinite(payload.repeatX) ? Number(payload.repeatX) : 1,
      repeatY: Number.isFinite(payload.repeatY) ? Number(payload.repeatY) : 1,
    };
  } catch {
    return null;
  }
}

/**
 * Per-triangle node attribution emitted by pcg-core for preview-sink cooks.
 * triangleSources[i] indexes sourceNodes for PCGM triangle i; -1 = unknown.
 */
export interface SourceMapping {
  sourceNodes: string[];
  triangleSources: Int32Array;
}

export function parseSourceMapping(json: string): SourceMapping | null {
  if (!json.trim()) return null;
  try {
    const payload = JSON.parse(json) as {
      source_nodes?: unknown;
      triangle_sources?: unknown;
    };
    if (!Array.isArray(payload.source_nodes) || !Array.isArray(payload.triangle_sources)) {
      return null;
    }
    const sourceNodes: string[] = [];
    for (const entry of payload.source_nodes) {
      if (typeof entry !== 'string') return null;
      sourceNodes.push(entry);
    }
    const triangleSources = new Int32Array(payload.triangle_sources.length);
    for (let i = 0; i < payload.triangle_sources.length; i++) {
      triangleSources[i] = Number(payload.triangle_sources[i]) | 0;
    }
    return { sourceNodes, triangleSources };
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
