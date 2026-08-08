import * as THREE from 'three';

/** Blender default MATCAP studio_lights ids (without .exr suffix). */
export const MATCAP_ENTRIES = [
  { id: 'basic_1', label: 'Basic 1' },
  { id: 'basic_2', label: 'Basic 2' },
  { id: 'basic_dark', label: 'Basic Dark' },
  { id: 'basic_side', label: 'Basic Side' },
  { id: 'ceramic_dark', label: 'Ceramic Dark' },
  { id: 'ceramic_lightbulb', label: 'Ceramic Lightbulb' },
  { id: 'normal', label: 'Normal' },
  { id: 'check_reflection', label: 'Check Reflection' },
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
  { id: 'resin', label: 'Resin' },
  { id: 'skin', label: 'Skin' },
  { id: 'toon', label: 'Toon' },
] as const;

export type MatcapId = (typeof MATCAP_ENTRIES)[number]['id'];

export const DEFAULT_MATCAP_ID: MatcapId = 'basic_1';

const cache = new Map<string, THREE.Texture>();
const loader = new THREE.TextureLoader();

function matcapUrl(id: string): string {
  return `/matcaps/${id}.png`;
}

export function getMatcapPreviewUrl(id: string): string {
  return matcapUrl(id);
}

export function loadMatcapTexture(id: string): Promise<THREE.Texture> {
  const cached = cache.get(id);
  if (cached) return Promise.resolve(cached);

  return new Promise((resolve, reject) => {
    loader.load(
      matcapUrl(id),
      (texture) => {
        texture.colorSpace = THREE.SRGBColorSpace;
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
