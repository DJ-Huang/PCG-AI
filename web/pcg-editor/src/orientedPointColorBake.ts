const OPC_HEADER_BYTES = 40;
const OPC_V2_RECORD_BYTES = 20;
const OPC_COLOR_FLAG = 1;
const OPC_UV_FLAG = 2;

export interface ImagePixels {
  width: number;
  height: number;
  /** Top-left-origin RGBA8 pixels, as returned by CanvasRenderingContext2D. */
  rgba: Uint8ClampedArray;
}

export interface OrientedPointColorBakeResult {
  pointCloud: string;
  sampleCount: number;
  textureWidth: number;
  textureHeight: number;
}

function decodeBase64(base64: string): Uint8Array {
  const binary = atob(base64);
  const bytes = new Uint8Array(binary.length);
  for (let index = 0; index < binary.length; index += 1) {
    bytes[index] = binary.charCodeAt(index);
  }
  return bytes;
}

function encodeBase64(bytes: Uint8Array): string {
  const chunkSize = 0x8000;
  const chunks: string[] = [];
  for (let offset = 0; offset < bytes.length; offset += chunkSize) {
    chunks.push(String.fromCharCode(...bytes.subarray(offset, offset + chunkSize)));
  }
  return btoa(chunks.join(''));
}

function readU16(bytes: Uint8Array, offset: number): number {
  return bytes[offset] | (bytes[offset + 1] << 8);
}

function readU32(bytes: Uint8Array, offset: number): number {
  return (bytes[offset] |
    (bytes[offset + 1] << 8) |
    (bytes[offset + 2] << 16) |
    (bytes[offset + 3] << 24)) >>> 0;
}

function writeU32(bytes: Uint8Array, offset: number, value: number): void {
  bytes[offset] = value & 0xff;
  bytes[offset + 1] = (value >>> 8) & 0xff;
  bytes[offset + 2] = (value >>> 16) & 0xff;
  bytes[offset + 3] = (value >>> 24) & 0xff;
}

function srgbToLinear(value: number): number {
  const channel = value / 255;
  return channel <= 0.04045
    ? channel / 12.92
    : Math.pow((channel + 0.055) / 1.055, 2.4);
}

function clamp(value: number, minimum: number, maximum: number): number {
  return Math.min(maximum, Math.max(minimum, value));
}

function bilinearLinearRgba(pixels: ImagePixels, u: number, v: number): [number, number, number, number] {
  const x = clamp(u, 0, 1) * (pixels.width - 1);
  const y = clamp(v, 0, 1) * (pixels.height - 1);
  const x0 = Math.floor(x);
  const y0 = Math.floor(y);
  const x1 = Math.min(pixels.width - 1, x0 + 1);
  const y1 = Math.min(pixels.height - 1, y0 + 1);
  const tx = x - x0;
  const ty = y - y0;
  const sample = (px: number, py: number, channel: number): number => {
    const value = pixels.rgba[(py * pixels.width + px) * 4 + channel];
    return channel === 3 ? value / 255 : srgbToLinear(value);
  };
  const interpolate = (channel: number): number => {
    const top = sample(x0, y0, channel) * (1 - tx) + sample(x1, y0, channel) * tx;
    const bottom = sample(x0, y1, channel) * (1 - tx) + sample(x1, y1, channel) * tx;
    return top * (1 - ty) + bottom * ty;
  };
  return [interpolate(0), interpolate(1), interpolate(2), interpolate(3)];
}

/**
 * Replace the RGBA bytes in an OPC1 v2 payload with base-colour samples.
 *
 * OPC stores the source GLB UV before the OrientedSdfSurface node's optional
 * V flip. Canvas pixels use a top-left origin, so their Y coordinate is
 * `1 - sourceV`. This was verified against the original Tripo GLB rendered by
 * the Web material path; sampling at `sourceV` vertically mirrors every atlas
 * island and produces plausible-looking but incorrect colour patches.
 * RGB is converted from sRGB to linear because Three.js vertex colours are
 * linear inputs, while an sRGB base-colour texture is decoded by the GPU.
 */
export function bakeOpc1BaseColorPixels(
  pointCloud: string,
  pixels: ImagePixels,
): OrientedPointColorBakeResult {
  if (!Number.isInteger(pixels.width) || !Number.isInteger(pixels.height) ||
      pixels.width <= 0 || pixels.height <= 0 ||
      pixels.rgba.length !== pixels.width * pixels.height * 4) {
    throw new Error('Base-colour texture pixels have invalid dimensions.');
  }
  const bytes = decodeBase64(pointCloud);
  if (bytes.length < OPC_HEADER_BYTES || bytes[0] !== 0x4f || bytes[1] !== 0x50 ||
      bytes[2] !== 0x43 || bytes[3] !== 0x31) {
    throw new Error('Oriented point payload has invalid OPC1 magic.');
  }
  const version = readU32(bytes, 4);
  if (version !== 2) {
    throw new Error(`Texture-to-point colour baking requires OPC1 v2; received v${version}.`);
  }
  const flags = readU32(bytes, 8);
  if ((flags & OPC_UV_FLAG) === 0) {
    throw new Error('Oriented point payload does not contain source UV measurements.');
  }
  const sampleCount = readU32(bytes, 12);
  const expectedBytes = OPC_HEADER_BYTES + sampleCount * OPC_V2_RECORD_BYTES;
  if (sampleCount === 0 || bytes.length !== expectedBytes) {
    throw new Error('Oriented point payload byte count does not match its header.');
  }

  for (let sampleIndex = 0; sampleIndex < sampleCount; sampleIndex += 1) {
    const offset = OPC_HEADER_BYTES + sampleIndex * OPC_V2_RECORD_BYTES;
    const u = readU16(bytes, offset + 16) / 65535;
    const v = readU16(bytes, offset + 18) / 65535;
    const color = bilinearLinearRgba(pixels, u, 1 - v);
    bytes[offset + 12] = Math.round(clamp(color[0], 0, 1) * 255);
    bytes[offset + 13] = Math.round(clamp(color[1], 0, 1) * 255);
    bytes[offset + 14] = Math.round(clamp(color[2], 0, 1) * 255);
    bytes[offset + 15] = Math.round(clamp(color[3], 0, 1) * 255);
  }
  writeU32(bytes, 8, flags | OPC_COLOR_FLAG);
  return {
    pointCloud: encodeBase64(bytes),
    sampleCount,
    textureWidth: pixels.width,
    textureHeight: pixels.height,
  };
}

export async function loadImagePixels(storage: string): Promise<ImagePixels> {
  if (!storage.trim()) throw new Error('Base-colour texture is empty.');
  const image = new Image();
  image.decoding = 'async';
  image.src = storage;
  await image.decode();
  const canvas = document.createElement('canvas');
  canvas.width = image.naturalWidth;
  canvas.height = image.naturalHeight;
  const context = canvas.getContext('2d', { willReadFrequently: true });
  if (!context || canvas.width <= 0 || canvas.height <= 0) {
    throw new Error('Could not decode the embedded base-colour texture.');
  }
  context.drawImage(image, 0, 0);
  const data = context.getImageData(0, 0, canvas.width, canvas.height);
  return { width: canvas.width, height: canvas.height, rgba: data.data };
}

export async function bakeBaseColorTextureIntoOpc1(
  pointCloud: string,
  textureStorage: string,
): Promise<OrientedPointColorBakeResult> {
  return bakeOpc1BaseColorPixels(pointCloud, await loadImagePixels(textureStorage));
}

/** Remove UV-dependent maps after albedo has been baked into point colours. */
export function makeVertexColorMaterialData(
  source: Record<string, unknown>,
): Record<string, unknown> {
  return {
    ...source,
    __nodeTitle: 'Reconstructed Vertex-Colour Material',
    materialName: 'ReconstructedVertexColorMaterial',
    baseColor: '#ffffff',
    baseColorMap: '',
    metallic: 0,
    metallicMap: '',
    roughness: 0.85,
    roughnessMap: '',
    normalMap: '',
    aoMap: '',
  };
}
