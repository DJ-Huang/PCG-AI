import * as THREE from 'three';

export interface PbrMaterialDefinition {
  kind: 'pcg.material';
  version: number;
  name: string;
  shaderId: string;
  baseColor: string;
  baseColorMap: string;
  metallic: number;
  metallicMap: string;
  roughness: number;
  roughnessMap: string;
  normalMap: string;
  normalScale: number;
  aoMap: string;
  aoIntensity: number;
  emissiveColor: string;
  emissiveMap: string;
  emissiveIntensity: number;
  opacity: number;
  alphaMode: 'opaque' | 'mask' | 'blend';
  alphaCutoff: number;
  doubleSided: boolean;
  unityShaderGuid: string;
  unityShaderName: string;
  unityPropertiesJson: string;
}

export type PbrMaterialLibrary = Record<string, PbrMaterialDefinition>;

export type PbrDebugView = 'lit' | 'albedo' | 'normal' | 'metallic' | 'smoothness' | 'ao' | 'emissive';

const textureLoader = new THREE.TextureLoader();
const textureCache = new Map<string, Promise<THREE.Texture>>();

function finiteNumber(value: unknown, fallback: number, minimum = -Infinity, maximum = Infinity): number {
  return typeof value === 'number' && Number.isFinite(value)
    ? Math.min(maximum, Math.max(minimum, value))
    : fallback;
}

function stringValue(value: unknown, fallback = ''): string {
  return typeof value === 'string' ? value : fallback;
}

export function normalizePbrMaterial(raw: unknown, fallbackName = 'Material'): PbrMaterialDefinition {
  const value = raw && typeof raw === 'object' ? raw as Record<string, unknown> : {};
  const alpha = stringValue(value.alphaMode, 'opaque');
  return {
    kind: 'pcg.material',
    version: finiteNumber(value.version, 1, 1),
    name: stringValue(value.name, fallbackName) || fallbackName,
    shaderId: stringValue(value.shaderId, 'pcg.standard-pbr'),
    baseColor: stringValue(value.baseColor, '#b8c2cc'),
    baseColorMap: stringValue(value.baseColorMap),
    metallic: finiteNumber(value.metallic, 0, 0, 1),
    metallicMap: stringValue(value.metallicMap),
    roughness: finiteNumber(value.roughness, 0.5, 0, 1),
    roughnessMap: stringValue(value.roughnessMap),
    normalMap: stringValue(value.normalMap),
    normalScale: finiteNumber(value.normalScale, 1, 0, 4),
    aoMap: stringValue(value.aoMap),
    aoIntensity: finiteNumber(value.aoIntensity, 1, 0, 4),
    emissiveColor: stringValue(value.emissiveColor, '#000000'),
    emissiveMap: stringValue(value.emissiveMap),
    emissiveIntensity: finiteNumber(value.emissiveIntensity, 0, 0, 16),
    opacity: finiteNumber(value.opacity, 1, 0, 1),
    alphaMode: alpha === 'mask' || alpha === 'blend' ? alpha : 'opaque',
    alphaCutoff: finiteNumber(value.alphaCutoff, 0.5, 0, 1),
    doubleSided: value.doubleSided === true,
    unityShaderGuid: stringValue(value.unityShaderGuid),
    unityShaderName: stringValue(value.unityShaderName),
    unityPropertiesJson: stringValue(value.unityPropertiesJson, '{}'),
  };
}

export function parsePbrMaterialLibrary(cookJson: string): PbrMaterialLibrary {
  if (!cookJson.trim()) return {};
  try {
    const root = JSON.parse(cookJson) as {
      mesh_metadata?: { pbrMaterials?: unknown };
      materials?: unknown;
    };
    const raw = root.materials ?? root.mesh_metadata?.pbrMaterials;
    if (!raw || typeof raw !== 'object' || Array.isArray(raw)) return {};
    const result: PbrMaterialLibrary = {};
    for (const [name, definition] of Object.entries(raw as Record<string, unknown>)) {
      result[name] = normalizePbrMaterial(definition, name);
    }
    return result;
  } catch {
    return {};
  }
}

function loadTexture(url: string, colorSpace: THREE.ColorSpace): Promise<THREE.Texture> {
  const key = `${colorSpace}:${url}`;
  let pending = textureCache.get(key);
  if (!pending) {
    pending = textureLoader.loadAsync(url).then((texture) => {
      texture.colorSpace = colorSpace;
      texture.wrapS = THREE.RepeatWrapping;
      texture.wrapT = THREE.RepeatWrapping;
      texture.needsUpdate = true;
      return texture;
    });
    textureCache.set(key, pending);
  }
  return pending;
}

function bindTexture(
  material: THREE.MeshStandardMaterial,
  url: string,
  colorSpace: THREE.ColorSpace,
  assign: (texture: THREE.Texture) => void,
  onReady?: () => void,
): void {
  if (!url) return;
  void loadTexture(url, colorSpace).then((texture) => {
    assign(texture);
    material.needsUpdate = true;
    onReady?.();
  }).catch((error) => {
    console.warn(`[PCG] Failed to load material texture "${url}"`, error);
  });
}

export function createStandardPbrMaterial(
  definition: PbrMaterialDefinition,
  hasVertexColors: boolean,
  onTextureReady?: () => void,
): THREE.MeshStandardMaterial {
  const alphaMode = definition.alphaMode;
  const material = new THREE.MeshStandardMaterial({
    name: definition.name,
    color: new THREE.Color(definition.baseColor),
    vertexColors: hasVertexColors,
    metalness: definition.metallic,
    roughness: definition.roughness,
    emissive: new THREE.Color(definition.emissiveColor),
    emissiveIntensity: definition.emissiveIntensity,
    opacity: definition.opacity,
    transparent: alphaMode === 'blend' || definition.opacity < 1,
    alphaTest: alphaMode === 'mask' ? definition.alphaCutoff : 0,
    side: definition.doubleSided ? THREE.DoubleSide : THREE.FrontSide,
  });
  bindTexture(material, definition.baseColorMap, THREE.SRGBColorSpace, (texture) => {
    material.map = texture;
  }, onTextureReady);
  bindTexture(material, definition.metallicMap, THREE.NoColorSpace, (texture) => {
    material.metalnessMap = texture;
  }, onTextureReady);
  bindTexture(material, definition.roughnessMap, THREE.NoColorSpace, (texture) => {
    material.roughnessMap = texture;
  }, onTextureReady);
  bindTexture(material, definition.normalMap, THREE.NoColorSpace, (texture) => {
    material.normalMap = texture;
    material.normalScale.set(definition.normalScale, definition.normalScale);
  }, onTextureReady);
  bindTexture(material, definition.aoMap, THREE.NoColorSpace, (texture) => {
    material.aoMap = texture;
    material.aoMapIntensity = definition.aoIntensity;
  }, onTextureReady);
  bindTexture(material, definition.emissiveMap, THREE.SRGBColorSpace, (texture) => {
    material.emissiveMap = texture;
  }, onTextureReady);
  return material;
}

/** Creates an unlit view of one material input without changing the source PBR data. */
export function createPbrDebugMaterial(
  definition: PbrMaterialDefinition,
  view: Exclude<PbrDebugView, 'lit'>,
  onTextureReady?: () => void,
): THREE.ShaderMaterial {
  const textureUrl = view === 'albedo'
    ? definition.baseColorMap
    : view === 'normal'
      ? definition.normalMap
      : view === 'metallic'
        ? definition.metallicMap
      : view === 'smoothness'
        ? definition.roughnessMap
        : view === 'ao'
          ? definition.aoMap
          : definition.emissiveMap;
  const colorSpace = view === 'albedo' || view === 'emissive' ? THREE.SRGBColorSpace : THREE.NoColorSpace;
  const fallback = view === 'albedo'
    ? new THREE.Color(definition.baseColor)
    : view === 'normal'
      ? new THREE.Color(0x8080ff)
      : view === 'emissive'
        ? new THREE.Color(definition.emissiveColor).multiplyScalar(definition.emissiveIntensity)
        : new THREE.Color().setScalar(
          view === 'metallic' ? definition.metallic : view === 'smoothness' ? 1 - definition.roughness : 1,
        );
  const material = new THREE.ShaderMaterial({
    name: `${definition.name} (${view})`,
    uniforms: {
      inputMap: { value: null as THREE.Texture | null },
      fallbackColor: { value: fallback },
      invert: { value: view === 'smoothness' },
      hasInputMap: { value: false },
    },
    vertexShader: `varying vec2 vUv; void main() { vUv = uv; gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0); }`,
    fragmentShader: `uniform sampler2D inputMap; uniform vec3 fallbackColor; uniform bool invert; uniform bool hasInputMap; varying vec2 vUv; void main() { vec3 value = fallbackColor; if (hasInputMap) value = texture2D(inputMap, vUv).rgb; if (invert) value = vec3(1.0) - value; gl_FragColor = vec4(value, 1.0); }`,
    side: definition.doubleSided ? THREE.DoubleSide : THREE.FrontSide,
  });
  if (textureUrl) {
    void loadTexture(textureUrl, colorSpace).then((texture) => {
      material.uniforms.inputMap.value = texture;
      material.uniforms.hasInputMap.value = true;
      material.needsUpdate = true;
      onTextureReady?.();
    }).catch((error) => {
      console.warn(`[PCG] Failed to load material texture "${textureUrl}"`, error);
    });
  }
  return material;
}

export function disposePbrTextureCache(): void {
  for (const pending of textureCache.values()) {
    void pending.then((texture) => texture.dispose()).catch(() => undefined);
  }
  textureCache.clear();
}
