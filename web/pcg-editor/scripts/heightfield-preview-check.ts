// heightfield-preview-check.ts — round-trip PCGH parse + preview mesh build.
// Run: npx tsx scripts/heightfield-preview-check.ts

import {
  buildHeightFieldPreviewMesh,
  heightFieldSamplePosition,
  HeightFieldOrientation,
  HeightFieldSampling,
  parseHeightFieldBinary,
  type ParsedHeightField,
} from '../src/cookResult';

function buildFixture(): ParsedHeightField {
  const resolutionX = 3;
  const resolutionZ = 3;
  const sampleCount = resolutionX * resolutionZ;
  return {
    resolutionX,
    resolutionZ,
    sizeX: 2,
    sizeZ: 2,
    centerX: 0,
    centerY: 0,
    centerZ: 0,
    sampling: HeightFieldSampling.Corner,
    orientation: HeightFieldOrientation.ZX,
    layers: [
      {
        name: 'height',
        tupleSize: 1,
        values: Float32Array.from([0, 1, 2, 3, 4, 5, 6, 7, 8]),
      },
      {
        name: 'mask',
        tupleSize: 1,
        values: new Float32Array(sampleCount).fill(1),
      },
    ],
  };
}

function encodeHeightFieldBinary(heightfield: ParsedHeightField): Uint8Array {
  const encoder = new TextEncoder();
  let payloadSize = 72;
  const encodedLayers = heightfield.layers.map((layer) => {
    const nameBytes = encoder.encode(layer.name);
    payloadSize += 20 + nameBytes.length + layer.values.length * 4;
    return { nameBytes, layer };
  });
  const out = new Uint8Array(payloadSize);
  const view = new DataView(out.buffer);
  let offset = 0;
  const writeU32 = (v: number) => {
    view.setUint32(offset, v, true);
    offset += 4;
  };
  const writeI32 = (v: number) => {
    view.setInt32(offset, v, true);
    offset += 4;
  };
  const writeF64 = (v: number) => {
    view.setFloat64(offset, v, true);
    offset += 8;
  };

  writeU32(0x48474350);
  writeU32(1);
  writeI32(heightfield.resolutionX);
  writeI32(heightfield.resolutionZ);
  writeI32(heightfield.layers.length);
  writeU32(heightfield.sampling);
  writeU32(heightfield.orientation);
  writeU32(0);
  writeF64(heightfield.sizeX);
  writeF64(heightfield.sizeZ);
  writeF64(heightfield.centerX);
  writeF64(heightfield.centerY);
  writeF64(heightfield.centerZ);

  for (const { nameBytes, layer } of encodedLayers) {
    writeU32(nameBytes.length);
    writeI32(layer.tupleSize);
    writeU32(0);
    view.setFloat32(offset, 0, true);
    offset += 4;
    writeU32(layer.values.length);
    out.set(nameBytes, offset);
    offset += nameBytes.length;
    for (let i = 0; i < layer.values.length; i++) {
      view.setFloat32(offset, layer.values[i], true);
      offset += 4;
    }
  }

  return out;
}

const failures: string[] = [];
const check = (label: string, ok: boolean) => {
  if (!ok) failures.push(label);
};

const source = buildFixture();
const [x, y, z] = heightFieldSamplePosition(source, 0, 0, 1);
check('sample_position corner', Math.abs(x + 1) < 1e-6 && Math.abs(y - 1) < 1e-6 && Math.abs(z + 1) < 1e-6);

const binary = encodeHeightFieldBinary(source);
const parsed = parseHeightFieldBinary(binary);
check('binary round-trip resolution', parsed.resolutionX === 3 && parsed.resolutionZ === 3);
check('binary round-trip height layer', parsed.layers.some((l) => l.name === 'height' && l.values[8] === 8));

const mesh = buildHeightFieldPreviewMesh(parsed);
check('preview mesh vertices', mesh.vertexCount === 9);
check('preview mesh indices', mesh.indexCount === 24);
let maxY = -Infinity;
for (let i = 1; i < mesh.positions.length; i += 3) maxY = Math.max(maxY, mesh.positions[i]);
check('preview mesh has lift', maxY > 0.5);

if (failures.length > 0) {
  console.error('HEIGHTFIELD PREVIEW CHECK FAILED:');
  for (const failure of failures) console.error(`  ${failure}`);
  process.exit(1);
}

console.log('Heightfield preview check OK');
