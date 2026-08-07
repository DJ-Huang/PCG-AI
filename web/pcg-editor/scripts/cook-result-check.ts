// cook-result-check.ts — Golden cook-result binary parse contract entry.
// Bundled by scripts/validate-cook-result-parse.py with rolldown and run under node.
// Verifies that src/cookResult.ts parses golden pcg-server binaries and that
// decoded counts match the recorded expectations in the sibling .json meta.
// Regenerate fixtures: cook the sibling .pcg against pcg-server /v1/cook with
// seed=42 and record the counts (see meta description fields).

import { readFileSync } from 'node:fs';
import {
  parseCookResult,
  parseGeometryBinary,
  parseMeshBinary,
  parsePointBinary,
  buildEdgeIndices,
} from '../src/cookResult';

interface GeometryExpectation {
  pointCount: number;
  faceCount: number;
  faceIndexCount: number;
  triangleCount: number;
}

interface MeshExpectation {
  vertexCount: number;
  indexCount: number;
}

interface Expectation {
  kind: number;
  code: number;
  geometry: GeometryExpectation | null;
  mesh: MeshExpectation | null;
  scatterPointCount: number;
}

const binPath = process.argv[2];
const metaPath = process.argv[3];
if (!binPath || !metaPath) {
  console.error('Usage: node cook-result-check.mjs <fixture.bin> <expectation.json>');
  process.exit(2);
}

const failures: string[] = [];
const check = (label: string, actual: unknown, expected: unknown) => {
  if (actual !== expected) {
    failures.push(`${label}: expected ${String(expected)}, got ${String(actual)}`);
  }
};

const expected = JSON.parse(readFileSync(metaPath, 'utf8')) as Expectation;
const bytes = readFileSync(binPath);
const buffer = bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength);

const cook = parseCookResult(buffer);
check('code', cook.code, expected.code);
check('kind', cook.kind, expected.kind);

if (expected.geometry) {
  if (cook.geometry.length === 0) {
    failures.push('geometry blob: expected non-empty, got empty');
  } else {
    const geo = parseGeometryBinary(cook.geometry);
    check('geometry.pointCount', geo.pointCount, expected.geometry.pointCount);
    check('geometry.faceCount', geo.faceCount, expected.geometry.faceCount);
    check('geometry.positions.length', geo.positions.length, expected.geometry.pointCount * 3);
    check('geometry.faceIndices.length', geo.faceIndices.length, expected.geometry.faceIndexCount);
    check('geometry.triangleCount', geo.triangles.length / 3, expected.geometry.triangleCount);
    const edges = buildEdgeIndices(geo);
    if (edges.length === 0) failures.push('buildEdgeIndices produced no edges for polygon geometry');
    // Triangulation chunk indexes the corner domain (flat-shading duplicated
    // vertices): valid range is corners of faces with >= 3 vertices.
    let cornerCount = 0;
    for (let f = 0; f < geo.faceCount; f++) {
      const start = geo.faceOffsets[f];
      const end = f + 1 < geo.faceCount ? geo.faceOffsets[f + 1] : geo.faceIndices.length;
      if (end - start >= 3) cornerCount += end - start;
    }
    for (const idx of geo.triangles) {
      if (idx >= cornerCount) {
        failures.push(`corner-domain triangle index ${idx} out of range (${cornerCount} corners)`);
        break;
      }
    }
  }
} else if (cook.geometry.length > 0) {
  failures.push(`geometry blob: expected empty, got ${cook.geometry.length} bytes`);
}

if (expected.mesh) {
  if (cook.mesh.length === 0) {
    failures.push('mesh blob: expected non-empty, got empty');
  } else {
    const mesh = parseMeshBinary(cook.mesh);
    check('mesh.vertexCount', mesh.vertexCount, expected.mesh.vertexCount);
    check('mesh.indexCount', mesh.indexCount, expected.mesh.indexCount);
    check('mesh.positions.length', mesh.positions.length, expected.mesh.vertexCount * 3);
    for (const idx of mesh.indices) {
      if (idx >= mesh.vertexCount) {
        failures.push(`mesh index ${idx} out of range (${mesh.vertexCount} vertices)`);
        break;
      }
    }
  }
} else if (cook.mesh.length > 0) {
  failures.push(`mesh blob: expected empty, got ${cook.mesh.length} bytes`);
}

if (expected.scatterPointCount > 0) {
  if (cook.points.length === 0) {
    failures.push('points blob: expected non-empty, got empty');
  } else {
    const scatter = parsePointBinary(cook.points);
    check('scatterPointCount', scatter.length / 3, expected.scatterPointCount);
  }
} else if (cook.points.length > 0) {
  failures.push(`points blob: expected empty, got ${cook.points.length} bytes`);
}

if (failures.length > 0) {
  console.error(`COOK-RESULT PARSE FAILED (${failures.length} failure(s)):`);
  for (const failure of failures) console.error(`  ${failure}`);
  process.exit(1);
}

console.log(`Cook-result parse OK: ${binPath}`);
