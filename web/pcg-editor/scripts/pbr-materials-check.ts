import * as THREE from 'three';

import {
  createPbrDebugMaterial,
  createStandardPbrMaterial,
  disposePbrTextureCache,
  normalizePbrMaterial,
  parsePbrMaterialLibrary,
  resolvePbrTextureUrl,
} from '../src/preview/pbrMaterials';
import {
  BUILTIN_ENVIRONMENTS,
  DEFAULT_BUILTIN_ENVIRONMENT,
  getBuiltinEnvironment,
} from '../src/preview/builtinEnvironments';
import {
  BLENDER_SOLID_COLOR,
  BLENDER_STUDIO_LIGHTS,
  createBlenderStudioMaterial,
  evaluateBlenderStudioDiffuse,
} from '../src/preview/blenderStudio';
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
check('Blender Studio has the expected three-light rig', BLENDER_STUDIO_LIGHTS.length === 3);
check('Blender Studio keeps its key-light calibration', nearlyEqual(BLENDER_STUDIO_LIGHTS[0].diffuse[0], 0.723042));
check('Blender Studio keeps its key-light wrap', nearlyEqual(BLENDER_STUDIO_LIGHTS[0].smooth, 0.2));
const frontDiffuse = evaluateBlenderStudioDiffuse([0, 0, 1]);
check('Blender Studio evaluates the front-facing diffuse oracle', nearlyEqual(frontDiffuse[0], 0.373065584));
const studioMaterial = createBlenderStudioMaterial(false);
check('Blender Studio bypasses Three.js tone mapping', studioMaterial.toneMapped === false);
check('Blender Studio uses Blender solid color', studioMaterial.fragmentShader.includes(`vec3(${BLENDER_SOLID_COLOR})`));
studioMaterial.dispose();

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
check(
  'portable resource locators resolve to Web public assets',
  resolvePbrTextureUrl('pcg-resource://textures/wooden-cabin/cedar.png') ===
    '/assets/textures/wooden-cabin/cedar.png',
);
check('ordinary URLs remain unchanged', resolvePbrTextureUrl('/custom/cedar.png') === '/custom/cedar.png');

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
    baseColorMap: 'pcg-resource://textures/shared-color.png',
    metallic: 0.8,
    metallicMap: 'metallic.png',
    roughness: 0.2,
    roughnessMap: 'roughness.png',
    normalMap: 'normal.png',
    normalScale: 1.75,
    aoMap: 'ao.png',
    aoIntensity: 0.6,
    emissiveColor: '#102030',
    emissiveMap: 'pcg-resource://textures/shared-color.png',
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

  const smoothnessDebug = createPbrDebugMaterial(definition, 'smoothness', () => readyCount++);
  await new Promise((resolve) => setTimeout(resolve, 0));
  check('smoothness debug material is unlit', smoothnessDebug.lights === false);
  check('smoothness debug view inverts roughness', smoothnessDebug.uniforms.invert.value === true);
  check('portable locator is resolved before loading', material.map?.name === '/assets/textures/shared-color.png');
  check('smoothness debug view uses the roughness texture', smoothnessDebug.uniforms.inputMap.value?.name === 'roughness.png');
  check('debug texture binding reports ready', readyCount === 7);

  let disposeCount = 0;
  for (const texture of loadedTextures) {
    texture.addEventListener('dispose', () => disposeCount++);
  }
  disposePbrTextureCache();
  await Promise.resolve();
  check('cached textures are disposed exactly once', disposeCount === loadedTextures.length);

  material.dispose();
  mask.dispose();
  smoothnessDebug.dispose();
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
