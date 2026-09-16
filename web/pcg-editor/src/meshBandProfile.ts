import type { ParsedMesh } from './cookResult';

const DEFAULT_BAND_COUNT = 20;
const MIN_BAND_SAMPLES = 20;
const ROUND_SCALE = 100_000;

export interface MeshBandProfileBand {
  band: number;
  low: number;
  high: number;
  samples: number;
  widthP90: number | null;
  depthP90: number | null;
  widthFull: number | null;
  centroidX: number | null;
  centroidZ: number | null;
}

export interface MeshBandProfile {
  schemaVersion: 'pcg-mesh-band-profile/v1';
  alignment: 'ground-height';
  upAxis: 'y';
  bandCount: number;
  vertexCount: number;
  triangleCount: number;
  finiteSampleCount: number;
  bounds: {
    min: [number, number, number];
    max: [number, number, number];
    size: [number, number, number];
  };
  normalization: {
    groundY: number;
    height: number;
    medianX: number;
    medianZ: number;
  };
  bands: MeshBandProfileBand[];
  note: string;
}

const profileCache = new WeakMap<Float32Array, Map<number, MeshBandProfile | null>>();

function round(value: number): number {
  return Math.round(value * ROUND_SCALE) / ROUND_SCALE;
}

function percentile(values: number[], q: number): number {
  values.sort((a, b) => a - b);
  const position = (values.length - 1) * q;
  const low = Math.floor(position);
  const high = Math.min(low + 1, values.length - 1);
  return values[low] + (values[high] - values[low]) * (position - low);
}

/**
 * Produce the same kind of diagnostic used by img2threejs' mesh-reference
 * comparison: feet/ground aligned, height normalized, percentile width and
 * depth per horizontal band, plus median offsets that expose misplaced mass.
 *
 * This is evidence, not topology transfer. Vertex density can still influence
 * medians, so the profile is intended to localize a mismatch before visual
 * review rather than act as a single automatic acceptance score.
 */
export function computeMeshBandProfile(
  mesh: Pick<ParsedMesh, 'positions' | 'indices' | 'vertexCount' | 'indexCount'>,
  bandCount = DEFAULT_BAND_COUNT,
): MeshBandProfile | null {
  const normalizedBandCount = Math.max(3, Math.min(64, Math.trunc(bandCount)));
  let byBandCount = profileCache.get(mesh.positions);
  const cached = byBandCount?.get(normalizedBandCount);
  if (cached !== undefined) return cached;

  const xs: number[] = [];
  const zs: number[] = [];
  let minX = Number.POSITIVE_INFINITY;
  let minY = Number.POSITIVE_INFINITY;
  let minZ = Number.POSITIVE_INFINITY;
  let maxX = Number.NEGATIVE_INFINITY;
  let maxY = Number.NEGATIVE_INFINITY;
  let maxZ = Number.NEGATIVE_INFINITY;
  for (let i = 0; i + 2 < mesh.positions.length; i += 3) {
    const x = mesh.positions[i];
    const y = mesh.positions[i + 1];
    const z = mesh.positions[i + 2];
    if (!Number.isFinite(x) || !Number.isFinite(y) || !Number.isFinite(z)) continue;
    xs.push(x);
    zs.push(z);
    minX = Math.min(minX, x);
    minY = Math.min(minY, y);
    minZ = Math.min(minZ, z);
    maxX = Math.max(maxX, x);
    maxY = Math.max(maxY, y);
    maxZ = Math.max(maxZ, z);
  }

  const height = maxY - minY;
  if (xs.length === 0 || !Number.isFinite(height) || height <= 1e-8) {
    byBandCount ??= new Map();
    byBandCount.set(normalizedBandCount, null);
    profileCache.set(mesh.positions, byBandCount);
    return null;
  }

  const medianX = percentile([...xs], 0.5);
  const medianZ = percentile([...zs], 0.5);
  const bucketXs = Array.from({ length: normalizedBandCount }, () => [] as number[]);
  const bucketZs = Array.from({ length: normalizedBandCount }, () => [] as number[]);
  for (let i = 0; i + 2 < mesh.positions.length; i += 3) {
    const x = mesh.positions[i];
    const y = mesh.positions[i + 1];
    const z = mesh.positions[i + 2];
    if (!Number.isFinite(x) || !Number.isFinite(y) || !Number.isFinite(z)) continue;
    const normalizedY = Math.max(0, Math.min(1, (y - minY) / height));
    const band = Math.min(normalizedBandCount - 1, Math.floor(normalizedY * normalizedBandCount));
    bucketXs[band].push((x - medianX) / height);
    bucketZs[band].push((z - medianZ) / height);
  }

  const bands = bucketXs.map((bandXs, band) => {
    const bandZs = bucketZs[band];
    const enoughSamples = bandXs.length >= MIN_BAND_SAMPLES;
    let widthP90: number | null = null;
    let depthP90: number | null = null;
    let widthFull: number | null = null;
    let centroidX: number | null = null;
    let centroidZ: number | null = null;
    if (enoughSamples) {
      const sortedXs = [...bandXs];
      const sortedZs = [...bandZs];
      const x05 = percentile(sortedXs, 0.05);
      const x95 = percentile(sortedXs, 0.95);
      const z05 = percentile(sortedZs, 0.05);
      const z95 = percentile(sortedZs, 0.95);
      widthP90 = round(x95 - x05);
      depthP90 = round(z95 - z05);
      widthFull = round(sortedXs.at(-1)! - sortedXs[0]);
      centroidX = round(percentile(sortedXs, 0.5));
      centroidZ = round(percentile(sortedZs, 0.5));
    }
    return {
      band,
      low: round(band / normalizedBandCount),
      high: round((band + 1) / normalizedBandCount),
      samples: bandXs.length,
      widthP90,
      depthP90,
      widthFull,
      centroidX,
      centroidZ,
    };
  });

  const profile: MeshBandProfile = {
    schemaVersion: 'pcg-mesh-band-profile/v1',
    alignment: 'ground-height',
    upAxis: 'y',
    bandCount: normalizedBandCount,
    vertexCount: mesh.vertexCount,
    triangleCount: Math.trunc(mesh.indexCount / 3),
    finiteSampleCount: xs.length,
    bounds: {
      min: [round(minX), round(minY), round(minZ)],
      max: [round(maxX), round(maxY), round(maxZ)],
      size: [round(maxX - minX), round(height), round(maxZ - minZ)],
    },
    normalization: {
      groundY: round(minY),
      height: round(height),
      medianX: round(medianX),
      medianZ: round(medianZ),
    },
    bands,
    note: '0=ground, 1=top; dimensions and median offsets are normalized by subject height; width/depth use 5th-95th percentiles.',
  };
  byBandCount ??= new Map();
  byBandCount.set(normalizedBandCount, profile);
  profileCache.set(mesh.positions, byBandCount);
  return profile;
}
