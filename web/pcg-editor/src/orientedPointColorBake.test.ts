import { describe, expect, it } from 'vitest';
import {
  bakeOpc1BaseColorPixels,
  makeVertexColorMaterialData,
} from './orientedPointColorBake';

function writeU16(bytes: Uint8Array, offset: number, value: number): void {
  bytes[offset] = value & 0xff;
  bytes[offset + 1] = value >>> 8;
}

function writeU32(bytes: Uint8Array, offset: number, value: number): void {
  bytes[offset] = value & 0xff;
  bytes[offset + 1] = value >>> 8;
  bytes[offset + 2] = value >>> 16;
  bytes[offset + 3] = value >>> 24;
}

function base64(bytes: Uint8Array): string {
  return btoa(String.fromCharCode(...bytes));
}

function onePointPayload(u: number, v: number, flags = 2): string {
  const bytes = new Uint8Array(60);
  bytes.set([0x4f, 0x50, 0x43, 0x31]);
  writeU32(bytes, 4, 2);
  writeU32(bytes, 8, flags);
  writeU32(bytes, 12, 1);
  writeU16(bytes, 56, Math.round(u * 65535));
  writeU16(bytes, 58, Math.round(v * 65535));
  return base64(bytes);
}

function decoded(base64Value: string): Uint8Array {
  return Uint8Array.from(atob(base64Value), (value) => value.charCodeAt(0));
}

describe('bakeOpc1BaseColorPixels', () => {
  it('flips GLB V into canvas top-left coordinates and sets the colour flag', () => {
    const result = bakeOpc1BaseColorPixels(onePointPayload(1, 1), {
      width: 2,
      height: 2,
      rgba: new Uint8ClampedArray([
        255, 0, 0, 255,   0, 255, 0, 255,
        0, 0, 255, 255,   255, 255, 255, 128,
      ]),
    });
    const bytes = decoded(result.pointCloud);
    expect(result.sampleCount).toBe(1);
    expect(bytes[8] & 1).toBe(1);
    expect(Array.from(bytes.slice(52, 56))).toEqual([0, 255, 0, 255]);
  });

  it('converts sRGB texture channels to linear vertex colours', () => {
    const result = bakeOpc1BaseColorPixels(onePointPayload(0, 0), {
      width: 1,
      height: 1,
      rgba: new Uint8ClampedArray([128, 128, 128, 255]),
    });
    const bytes = decoded(result.pointCloud);
    expect(bytes[52]).toBe(55);
    expect(bytes[53]).toBe(55);
    expect(bytes[54]).toBe(55);
  });

  it('rejects payloads without source UV measurements', () => {
    expect(() => bakeOpc1BaseColorPixels(onePointPayload(0, 0, 0), {
      width: 1,
      height: 1,
      rgba: new Uint8ClampedArray([255, 255, 255, 255]),
    })).toThrow(/source UV/);
  });
});

describe('makeVertexColorMaterialData', () => {
  it('removes every UV-dependent material map', () => {
    expect(makeVertexColorMaterialData({
      baseColorMap: 'base', normalMap: 'normal', metallicMap: 'orm',
      roughnessMap: 'orm', aoMap: 'orm', emissiveMap: '',
    })).toMatchObject({
      baseColor: '#ffffff', baseColorMap: '', normalMap: '', metallicMap: '',
      roughnessMap: '', aoMap: '', metallic: 0, roughness: 0.85,
    });
  });
});
