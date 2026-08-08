import * as THREE from 'three';
import { EXRLoader } from 'three/addons/loaders/EXRLoader.js';

/** Blender default MATCAP studio_lights ids (without .exr suffix). */
export const MATCAP_ENTRIES = [
  { id: 'basic_1', label: 'Basic 1' },
  { id: 'basic_2', label: 'Basic 2' },
  { id: 'basic_dark', label: 'Basic Dark' },
  { id: 'basic_side', label: 'Basic Side' },
  { id: 'ceramic_dark', label: 'Ceramic Dark' },
  { id: 'ceramic_lightbulb', label: 'Ceramic Lightbulb' },
  { id: 'normal', label: 'Normal' },
  { id: 'check_rim_dark', label: 'Check Rim Dark' },
  { id: 'check_rim_light', label: 'Check Rim Light' },
  { id: 'clay_brown', label: 'Clay Brown' },
  { id: 'clay_muddy', label: 'Clay Muddy' },
  { id: 'clay_studio', label: 'Clay Studio' },
  { id: 'jade', label: 'Jade' },
  { id: 'metal_anisotropic', label: 'Metal Anisotropic' },
  { id: 'metal_carpaint', label: 'Metal Carpaint' },
  { id: 'metal_lead', label: 'Metal Lead' },
  { id: 'metal_shiny', label: 'Metal Shiny' },
  { id: 'pearl', label: 'Pearl' },
  { id: 'reflection_check_horizontal', label: 'Reflection Check H' },
  { id: 'reflection_check_vertical', label: 'Reflection Check V' },
  { id: 'resin', label: 'Resin' },
  { id: 'skin', label: 'Skin' },
  { id: 'toon', label: 'Toon' },
] as const;

export type MatcapId = (typeof MATCAP_ENTRIES)[number]['id'];

export const DEFAULT_MATCAP_ID: MatcapId = 'basic_1';

const cache = new Map<string, THREE.Texture>();
const exrLoader = new EXRLoader();

function matcapUrl(id: string): string {
  return `/matcaps/${id}.exr`;
}

/** Picker thumbnails: browsers cannot display EXR, PNGs exist only for this. */
export function getMatcapPreviewUrl(id: string): string {
  return `/matcaps/${id}.png`;
}

// UNPACK_FLIP_Y is ignored for DataTexture uploads, so flip scanlines manually
// to match DOM-image textures (flipY=true) and Blender's matcap orientation.
function flipDataTextureVertically(texture: THREE.DataTexture): void {
  const image = texture.image as { data: ArrayBufferView; width: number; height: number };
  const channels = 4; // EXRLoader always outputs RGBA
  const rowLength = image.width * channels;
  const bytesPerElement = image.data.byteLength / (rowLength * image.height);
  const row = new Uint8Array(rowLength * bytesPerElement);
  const bytes = new Uint8Array(
    image.data.buffer,
    image.data.byteOffset,
    image.data.byteLength,
  );
  for (let y = 0; y < image.height / 2; y++) {
    const top = y * rowLength * bytesPerElement;
    const bottom = (image.height - 1 - y) * rowLength * bytesPerElement;
    row.set(bytes.subarray(top, top + row.byteLength));
    bytes.copyWithin(top, bottom, bottom + row.byteLength);
    bytes.set(row, bottom);
  }
}

export function loadMatcapTexture(id: string): Promise<THREE.Texture> {
  const cached = cache.get(id);
  if (cached) return Promise.resolve(cached);

  return new Promise((resolve, reject) => {
    exrLoader.load(
      matcapUrl(id),
      (texture) => {
        flipDataTextureVertically(texture as THREE.DataTexture);
        // EXR data is linear HDR; no sRGB decode. ACES tone mapping in the
        // renderer handles the >1.0 highlight range like Blender does.
        texture.colorSpace = THREE.LinearSRGBColorSpace;
        cache.set(id, texture);
        resolve(texture);
      },
      undefined,
      (err) => reject(err),
    );
  });
}

export function preloadMatcap(id: string): void {
  loadMatcapTexture(id).catch(() => {
    // ponytail: optional toast if asset missing
  });
}

export function disposeMatcapLibrary(): void {
  for (const tex of cache.values()) tex.dispose();
  cache.clear();
}
