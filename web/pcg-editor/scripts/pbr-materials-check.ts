import * as THREE from 'three';

import {
  createStandardPbrMaterial,
  disposePbrTextureCache,
  normalizePbrMaterial,
  parsePbrMaterialLibrary,
} from '../src/preview/pbrMaterials';
import {
  BUILTIN_ENVIRONMENTS,
  DEFAULT_BUILTIN_ENVIRONMENT,
  getBuiltinEnvironment,
} from '../src/preview/builtinEnvironments';
import { getNodeTypeDefs } from '../src/nodeManifest';

const failures: string[] = [];

function check(label: string, condition: boolean): void {
  if (!condition) failures.push(label);
}

function nearlyEqual(actual: number, expected: number): boolean {
  return Math.abs(actual - expected) <= 1e-6;
}

check('at least six built-in HDRIs are available', BUILTIN_ENVIRONMENTS.length >= 6);
check(
  'built-in HDRI IDs are unique',
  new Set(BUILTIN_ENVIRONMENTS.map((environment) => environment.id)).size === BUILTIN_ENVIRONMENTS.length,
);
check(
  'built-in HDRI URLs are unique',
  new Set(BUILTIN_ENVIRONMENTS.map((environment) => environment.url)).size === BUILTIN_ENVIRONMENTS.length,
);
check(
  'default built-in HDRI is part of the catalog',
  getBuiltinEnvironment(DEFAULT_BUILTIN_ENVIRONMENT.id) === DEFAULT_BUILTIN_ENVIRONMENT,
);
check('Material nodes opt out of isolated preview', getNodeTypeDefs('Material')?.supportsPreview === false);

const normalized = normalizePbrMaterial({
  name: '',
  metallic: 2,
  roughness: -1,
  normalScale: 8,
  aoIntensity: -2,
  emissiveIntensity: 99,
  opacity: -0.5,
  alphaMode: 'unexpected',
  alphaCutoff: 4,
  doubleSided: true,
}, 'paint');
check('normalization uses the fallback name', normalized.name === 'paint');
check('metallic is clamped', normalized.metallic === 1);
check('roughness is clamped', normalized.roughness === 0);
check('normal scale is clamped', normalized.normalScale === 4);
check('AO intensity is clamped', normalized.aoIntensity === 0);
check('emissive intensity is clamped', normalized.emissiveIntensity === 16);
check('opacity is clamped', normalized.opacity === 0);
check('unknown alpha mode falls back to opaque', normalized.alphaMode === 'opaque');
check('alpha cutoff is clamped', normalized.alphaCutoff === 1);
check('double-sided flag is preserved', normalized.doubleSided);

const metadataLibrary = parsePbrMaterialLibrary(JSON.stringify({
  mesh_metadata: {
    pbrMaterials: {
      paint: { metallic: 0.25, roughness: 0.75 },
    },
  },
}));
check('metadata material library is parsed', metadataLibrary.paint?.name === 'paint');
check('metadata PBR values are preserved', metadataLibrary.paint?.roughness === 0.75);

const topLevelLibrary = parsePbrMaterialLibrary(JSON.stringify({
  materials: { roof: { baseColor: '#ff0000' } },
  mesh_metadata: { pbrMaterials: { ignored: {} } },
}));
check('top-level material library takes precedence', Object.keys(topLevelLibrary).join(',') === 'roof');
check('malformed cook JSON is safe', Object.keys(parsePbrMaterialLibrary('{')).length === 0);

const originalLoadAsync = THREE.TextureLoader.prototype.loadAsync;
const loadedTextures: THREE.Texture[] = [];
THREE.TextureLoader.prototype.loadAsync = function loadAsync(url: string): Promise<THREE.Texture> {
  const texture = new THREE.Texture();
  texture.name = url;
  loadedTextures.push(texture);
  return Promise.resolve(texture);
};

try {
  const definition = normalizePbrMaterial({
    name: 'coated-metal',
    baseColor: '#804020',
    baseColorMap: 'shared-color.png',
    metallic: 0.8,
    metallicMap: 'metallic.png',
    roughness: 0.2,
    roughnessMap: 'roughness.png',
    normalMap: 'normal.png',
    normalScale: 1.75,
    aoMap: 'ao.png',
    aoIntensity: 0.6,
    emissiveColor: '#102030',
    emissiveMap: 'shared-color.png',
    emissiveIntensity: 2.5,
    opacity: 0.4,
    alphaMode: 'blend',
    alphaCutoff: 0.33,
    doubleSided: true,
  });
  let readyCount = 0;
  const material = createStandardPbrMaterial(definition, true, () => readyCount++);
  await new Promise((resolve) => setTimeout(resolve, 0));

  check('material name is applied', material.name === 'coated-metal');
  check('vertex colors are enabled', material.vertexColors);
  check('metalness is applied', nearlyEqual(material.metalness, 0.8));
  check('roughness is applied', nearlyEqual(material.roughness, 0.2));
  check('emissive intensity is applied', nearlyEqual(material.emissiveIntensity, 2.5));
  check('opacity is applied', nearlyEqual(material.opacity, 0.4));
  check('blend mode is transparent', material.transparent);
  check('double-sided mode is applied', material.side === THREE.DoubleSide);
  check('base color texture uses sRGB', material.map?.colorSpace === THREE.SRGBColorSpace);
  check('emissive texture reuses the sRGB cache entry', material.emissiveMap === material.map);
  check('metallic texture is data', material.metalnessMap?.colorSpace === THREE.NoColorSpace);
  check('roughness texture is data', material.roughnessMap?.colorSpace === THREE.NoColorSpace);
  check('normal texture is data', material.normalMap?.colorSpace === THREE.NoColorSpace);
  check('AO texture is data', material.aoMap?.colorSpace === THREE.NoColorSpace);
  check('normal scale is applied', nearlyEqual(material.normalScale.x, 1.75));
  check('AO intensity is applied', nearlyEqual(material.aoMapIntensity, 0.6));
  check('all six texture bindings report ready', readyCount === 6);
  check('same URL/color-space loads only once', loadedTextures.length === 5);

  const mask = createStandardPbrMaterial(normalizePbrMaterial({
    alphaMode: 'mask',
    alphaCutoff: 0.33,
  }), false);
  check('mask mode sets alpha test', nearlyEqual(mask.alphaTest, 0.33));
  check('mask mode remains opaque', !mask.transparent);

  let disposeCount = 0;
  for (const texture of loadedTextures) {
    texture.addEventListener('dispose', () => disposeCount++);
  }
  disposePbrTextureCache();
  await Promise.resolve();
  check('cached textures are disposed exactly once', disposeCount === loadedTextures.length);

  material.dispose();
  mask.dispose();
} finally {
  THREE.TextureLoader.prototype.loadAsync = originalLoadAsync;
  disposePbrTextureCache();
}

if (failures.length > 0) {
  console.error(`PBR MATERIAL CHECK FAILED (${failures.length} failure(s)):`);
  for (const failure of failures) console.error(`  ${failure}`);
  process.exit(1);
}

console.log('PBR material contract OK');
