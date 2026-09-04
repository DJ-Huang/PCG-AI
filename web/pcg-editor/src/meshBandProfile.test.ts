import { describe, expect, it } from 'vitest';

import { computeMeshBandProfile } from './meshBandProfile';

function profileMesh(): {
  positions: Float32Array;
  indices: Uint32Array;
  vertexCount: number;
  indexCount: number;
} {
  const positions: number[] = [];
  for (let band = 0; band < 4; band++) {
    const y = band * 0.25 + 0.125;
    const halfWidth = 0.5 + band * 0.25;
    for (let sample = 0; sample < 24; sample++) {
      const side = sample % 2 === 0 ? -1 : 1;
      positions.push(side * halfWidth + 2, y + 3, side * 0.25 - 4);
    }
  }
  const vertexCount = positions.length / 3;
  const indices = new Uint32Array(Math.floor(vertexCount / 3) * 3);
  for (let i = 0; i < indices.length; i++) indices[i] = i;
  return {
    positions: new Float32Array(positions),
    indices,
    vertexCount,
    indexCount: indices.length,
  };
}

describe('computeMeshBandProfile', () => {
  it('aligns to the ground, normalizes by height, and records percentile bands', () => {
    const profile = computeMeshBandProfile(profileMesh(), 4);
    expect(profile).not.toBeNull();
    expect(profile).toMatchObject({
      schemaVersion: 'pcg-mesh-band-profile/v1',
      alignment: 'ground-height',
      upAxis: 'y',
      bandCount: 4,
      finiteSampleCount: 96,
    });
    expect(profile!.normalization).toMatchObject({ groundY: 3.125, medianX: 2, medianZ: -4 });
    expect(profile!.bands.map((band) => band.widthP90)).toEqual([1.33333, 2, 2.66667, 3.33333]);
    expect(profile!.bands.every((band) => band.centroidX === 0)).toBe(true);
    expect(profile!.bands.every((band) => band.centroidZ === 0)).toBe(true);
  });

  it('returns null for a zero-height mesh', () => {
    const positions = new Float32Array([0, 2, 0, 1, 2, 0, 0, 2, 1]);
    expect(computeMeshBandProfile({
      positions,
      indices: new Uint32Array([0, 1, 2]),
      vertexCount: 3,
      indexCount: 3,
    })).toBeNull();
  });
});
