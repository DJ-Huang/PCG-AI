import * as THREE from 'three';
import type { ParsedMesh } from './cookResult';
import {
  computeActionSkinBinding,
  partitionActionComponents,
  pcgPositionsToThree,
  type ActionRuntimeMetadata,
} from './actionRuntime';
import {
  normalizePbrMaterial,
  type PbrMaterialDefinition,
  type PbrMaterialLibrary,
} from './preview/pbrMaterials';

export interface PreviewGlbExportOptions {
  name: string;
  graphPath: string;
  frontAxis?: string;
  parameters?: Record<string, unknown>;
  /** When present, emit real glTF skins, named component nodes, sockets, and animations. */
  actionRuntime?: ActionRuntimeMetadata | null;
}

interface BinaryPart {
  offset: number;
  bytes: Uint8Array;
}

interface BufferView {
  buffer: number;
  byteOffset: number;
  byteLength: number;
  target?: number;
}

const GLB_MAGIC = 0x46546c67;
const GLB_VERSION = 2;
const JSON_CHUNK = 0x4e4f534a;
const BIN_CHUNK = 0x004e4942;
const ARRAY_BUFFER = 34962;
const ELEMENT_ARRAY_BUFFER = 34963;
const FLOAT = 5126;
const UNSIGNED_SHORT = 5123;
const UNSIGNED_INT = 5125;

function align4(value: number): number {
  return (value + 3) & ~3;
}

function float32Bytes(values: Float32Array): Uint8Array {
  const bytes = new Uint8Array(values.length * 4);
  const view = new DataView(bytes.buffer);
  for (let i = 0; i < values.length; i++) view.setFloat32(i * 4, values[i], true);
  return bytes;
}

function uint32Bytes(values: Uint32Array): Uint8Array {
  const bytes = new Uint8Array(values.length * 4);
  const view = new DataView(bytes.buffer);
  for (let i = 0; i < values.length; i++) view.setUint32(i * 4, values[i], true);
  return bytes;
}

function uint16Bytes(values: Uint16Array): Uint8Array {
  const bytes = new Uint8Array(values.length * 2);
  const view = new DataView(bytes.buffer);
  for (let i = 0; i < values.length; i++) view.setUint16(i * 2, values[i], true);
  return bytes;
}

function positionBounds(values: Float32Array): { min: number[]; max: number[] } {
  const min = [Number.POSITIVE_INFINITY, Number.POSITIVE_INFINITY, Number.POSITIVE_INFINITY];
  const max = [Number.NEGATIVE_INFINITY, Number.NEGATIVE_INFINITY, Number.NEGATIVE_INFINITY];
  for (let i = 0; i < values.length; i += 3) {
    for (let axis = 0; axis < 3; axis++) {
      min[axis] = Math.min(min[axis], values[i + axis]);
      max[axis] = Math.max(max[axis], values[i + axis]);
    }
  }
  return { min, max };
}

interface GltfTextureRegistry {
  images: Array<{ name: string; uri: string }>;
  textures: Array<{ name: string; sampler: number; source: number }>;
  byUri: Map<string, number>;
}

function textureIndex(registry: GltfTextureRegistry, uri: string, name: string): number | null {
  if (!uri) return null;
  const existing = registry.byUri.get(uri);
  if (existing !== undefined) return existing;
  const imageIndex = registry.images.length;
  registry.images.push({ name, uri });
  const index = registry.textures.length;
  registry.textures.push({ name, sampler: 0, source: imageIndex });
  registry.byUri.set(uri, index);
  return index;
}

function materialJson(definition: PbrMaterialDefinition, registry: GltfTextureRegistry) {
  const base = new THREE.Color(definition.baseColor);
  const emissive = new THREE.Color(definition.emissiveColor)
    .multiplyScalar(definition.emissiveIntensity);
  const result: Record<string, unknown> = {
    name: definition.name,
    pbrMetallicRoughness: {
      baseColorFactor: [base.r, base.g, base.b, definition.opacity],
      metallicFactor: definition.metallic,
      roughnessFactor: definition.roughness,
    },
    emissiveFactor: [
      Math.min(1, emissive.r),
      Math.min(1, emissive.g),
      Math.min(1, emissive.b),
    ],
    alphaMode: definition.alphaMode.toUpperCase(),
    doubleSided: definition.doubleSided,
  };
  const pbr = result.pbrMetallicRoughness as Record<string, unknown>;
  const baseColorTexture = textureIndex(
    registry, definition.baseColorMap, `${definition.name} Base Color`,
  );
  if (baseColorTexture !== null) pbr.baseColorTexture = { index: baseColorTexture };
  // glTF deliberately packs roughness in G and metallic in B. When two
  // separate source maps are supplied, prefer roughness and keep factors; the
  // PCG bake path supplies one shared ORM image so it round-trips losslessly.
  const metallicRoughnessTexture = textureIndex(
    registry,
    definition.roughnessMap || definition.metallicMap,
    `${definition.name} Metallic Roughness`,
  );
  if (metallicRoughnessTexture !== null) {
    pbr.metallicRoughnessTexture = { index: metallicRoughnessTexture };
  }
  const normalTexture = textureIndex(
    registry, definition.normalMap, `${definition.name} Normal`,
  );
  if (normalTexture !== null) {
    result.normalTexture = { index: normalTexture, scale: definition.normalScale };
  }
  const aoTexture = textureIndex(registry, definition.aoMap, `${definition.name} Occlusion`);
  if (aoTexture !== null) {
    result.occlusionTexture = { index: aoTexture, strength: definition.aoIntensity };
  }
  const emissiveTexture = textureIndex(
    registry, definition.emissiveMap, `${definition.name} Emissive`,
  );
  if (emissiveTexture !== null) result.emissiveTexture = { index: emissiveTexture };
  if (definition.alphaMode === 'mask') result.alphaCutoff = definition.alphaCutoff;
  return result;
}

function triangleIndexGroups(mesh: ParsedMesh, slotCount: number): Uint32Array[] {
  const groups = Array.from({ length: Math.max(1, slotCount) }, () => [] as number[]);
  const triangleCount = Math.floor(mesh.indices.length / 3);
  for (let triangle = 0; triangle < triangleCount; triangle++) {
    const requestedSlot = mesh.triangleMaterials?.[triangle] ?? 0;
    const slot = requestedSlot < groups.length ? requestedSlot : 0;
    const offset = triangle * 3;
    groups[slot].push(
      mesh.indices[offset],
      mesh.indices[offset + 2],
      mesh.indices[offset + 1],
    );
  }
  return groups.map((indices) => Uint32Array.from(indices));
}

function indexBounds(indices: Uint32Array): { min: number; max: number } {
  let min = Number.POSITIVE_INFINITY;
  let max = Number.NEGATIVE_INFINITY;
  for (const index of indices) {
    min = Math.min(min, index);
    max = Math.max(max, index);
  }
  return { min, max };
}

function actionNodeName(prefix: string, id: string, index: number): string {
  const clean = id.replace(/[^A-Za-z0-9_-]/g, '_');
  return `${prefix}_${clean || index}`;
}

function gltfQuaternionValues(values: readonly number[]): Float32Array {
  const result = Float32Array.from(values);
  for (let i = 0; i < result.length; i += 4) {
    result[i] = -result[i];
    result[i + 1] = -result[i + 1];
  }
  return result;
}

function gltfVectorValues(values: readonly number[]): Float32Array {
  const result = Float32Array.from(values);
  for (let i = 2; i < result.length; i += 3) result[i] = -result[i];
  return result;
}

/**
 * Export a cooked PCGM preview mesh as a self-contained GLB.
 *
 * PCG geometry is authored in Unity coordinates. The Web renderer mirrors Z,
 * so the exporter performs the same handedness conversion and reverses each
 * triangle winding. Constant PBR factors and material slots are preserved.
 */
export function exportPreviewMeshGlb(
  mesh: ParsedMesh,
  materialLibrary: PbrMaterialLibrary,
  options: PreviewGlbExportOptions,
): ArrayBuffer {
  if (mesh.vertexCount <= 0 || mesh.indexCount <= 0) {
    throw new Error('Cannot export an empty preview mesh.');
  }

  const positions = pcgPositionsToThree(mesh.positions);
  const normals = mesh.normals ? pcgPositionsToThree(mesh.normals) : null;
  const slotNames = mesh.materialSlots.length > 0 ? mesh.materialSlots : ['Material'];
  const materialDefinitions = slotNames.map((slot) => (
    materialLibrary[slot] ?? normalizePbrMaterial({ name: slot }, slot)
  ));
  const textureRegistry: GltfTextureRegistry = {
    images: [],
    textures: [],
    byUri: new Map<string, number>(),
  };
  const actionRuntime = options.actionRuntime ?? null;

  const parts: BinaryPart[] = [];
  const bufferViews: BufferView[] = [];
  let binaryLength = 0;
  const addBufferView = (bytes: Uint8Array, target?: number): number => {
    binaryLength = align4(binaryLength);
    const index = bufferViews.length;
    parts.push({ offset: binaryLength, bytes });
    bufferViews.push({ buffer: 0, byteOffset: binaryLength, byteLength: bytes.length, target });
    binaryLength += bytes.length;
    return index;
  };

  const accessors: Record<string, unknown>[] = [];
  const attributes: Record<string, number> = {};
  const bounds = positionBounds(positions);
  attributes.POSITION = accessors.length;
  accessors.push({
    bufferView: addBufferView(float32Bytes(positions), ARRAY_BUFFER),
    componentType: FLOAT,
    count: mesh.vertexCount,
    type: 'VEC3',
    min: bounds.min,
    max: bounds.max,
  });
  if (normals) {
    attributes.NORMAL = accessors.length;
    accessors.push({
      bufferView: addBufferView(float32Bytes(normals), ARRAY_BUFFER),
      componentType: FLOAT,
      count: mesh.vertexCount,
      type: 'VEC3',
    });
  }
  if (mesh.uvs) {
    attributes.TEXCOORD_0 = accessors.length;
    accessors.push({
      bufferView: addBufferView(float32Bytes(mesh.uvs), ARRAY_BUFFER),
      componentType: FLOAT,
      count: mesh.vertexCount,
      type: 'VEC2',
    });
  }
  if (mesh.colors) {
    attributes.COLOR_0 = accessors.length;
    accessors.push({
      bufferView: addBufferView(float32Bytes(mesh.colors), ARRAY_BUFFER),
      componentType: FLOAT,
      count: mesh.vertexCount,
      type: 'VEC4',
    });
  }

  let skinBinding = null as ReturnType<typeof computeActionSkinBinding> | null;
  if (actionRuntime) {
    if (actionRuntime.rig.bones.length > 65535) throw new Error('ActionRig exceeds glTF joint index range.');
    skinBinding = computeActionSkinBinding(positions, actionRuntime, mesh.indices);
    attributes.JOINTS_0 = accessors.length;
    accessors.push({
      bufferView: addBufferView(uint16Bytes(skinBinding.joints), ARRAY_BUFFER),
      componentType: UNSIGNED_SHORT,
      count: mesh.vertexCount,
      type: 'VEC4',
    });
    attributes.WEIGHTS_0 = accessors.length;
    accessors.push({
      bufferView: addBufferView(float32Bytes(skinBinding.weights), ARRAY_BUFFER),
      componentType: FLOAT,
      count: mesh.vertexCount,
      type: 'VEC4',
    });
  }

  const addIndexAccessor = (indices: Uint32Array): number => {
    const bounds = indexBounds(indices);
    const accessor = accessors.length;
    accessors.push({
      bufferView: addBufferView(uint32Bytes(indices), ELEMENT_ARRAY_BUFFER),
      componentType: UNSIGNED_INT,
      count: indices.length,
      type: 'SCALAR',
      min: [bounds.min],
      max: [bounds.max],
    });
    return accessor;
  };

  const nodes: Record<string, unknown>[] = [{
    name: `${options.name} Root`,
    children: [] as number[],
    extras: {
      pcgGraph: options.graphPath,
      frontAxis: options.frontAxis ?? '+z',
      parameters: options.parameters ?? {},
      ...(actionRuntime ? {
        pcgActionRig: {
          schemaVersion: actionRuntime.schemaVersion,
          rigSchemaVersion: actionRuntime.rig.schemaVersion,
          sourceRoute: actionRuntime.rig.sourceRoute,
          sourceNode: actionRuntime.sourceNode,
          componentNames: actionRuntime.rig.components.map((component) => component.id),
          componentTree: actionRuntime.rig.componentTree,
          animationNames: actionRuntime.rig.clips.map((clip) => clip.name),
          colliders: actionRuntime.rig.colliders,
          destructionGroups: actionRuntime.rig.destructionGroups,
        },
      } : {}),
    },
  }];
  const rootChildren = nodes[0].children as number[];
  const meshes: Record<string, unknown>[] = [];
  const skins: Record<string, unknown>[] = [];
  const animations: Record<string, unknown>[] = [];

  if (!actionRuntime || !skinBinding) {
    const primitives: Record<string, unknown>[] = [];
    const indexGroups = triangleIndexGroups(mesh, materialDefinitions.length);
    for (let slot = 0; slot < indexGroups.length; slot++) {
      const indices = indexGroups[slot];
      if (indices.length === 0) continue;
      primitives.push({ attributes, indices: addIndexAccessor(indices), material: slot, mode: 4 });
    }
    meshes.push({ name: options.name, primitives });
    nodes.push({ name: options.name, mesh: 0 });
    rootChildren.push(1);
  } else {
    const boneNodeIndices = new Map<string, number>();
    actionRuntime.rig.bones.forEach((bone, index) => {
      const nodeIndex = nodes.length;
      boneNodeIndices.set(bone.id, nodeIndex);
      const parent = bone.parent
        ? actionRuntime.rig.bones.find((candidate) => candidate.id === bone.parent)
        : null;
      const head = [bone.head[0], bone.head[1], -bone.head[2]];
      const translation = parent
        ? [head[0] - parent.head[0], head[1] - parent.head[1], head[2] + parent.head[2]]
        : head;
      nodes.push({
        name: actionNodeName('PCG_Bone', bone.id, index),
        translation,
        children: [] as number[],
        extras: { pcgBoneId: bone.id, radius: bone.radius },
      });
    });
    actionRuntime.rig.bones.forEach((bone) => {
      const nodeIndex = boneNodeIndices.get(bone.id)!;
      if (bone.parent) {
        const parentNode = nodes[boneNodeIndices.get(bone.parent)!];
        (parentNode.children as number[]).push(nodeIndex);
      } else {
        rootChildren.push(nodeIndex);
      }
    });

    actionRuntime.rig.sockets.forEach((socket, index) => {
      const nodeIndex = nodes.length;
      const quaternion = socket.rotation
        ? [-socket.rotation[0], -socket.rotation[1], socket.rotation[2], socket.rotation[3]]
        : undefined;
      nodes.push({
        name: actionNodeName('PCG_Socket', socket.id, index),
        ...(socket.position ? {
          translation: [socket.position[0], socket.position[1], -socket.position[2]],
        } : {}),
        ...(quaternion ? { rotation: quaternion } : {}),
        extras: { pcgSocketId: socket.id, bone: socket.bone },
      });
      (nodes[boneNodeIndices.get(socket.bone)!].children as number[]).push(nodeIndex);
    });

    const inverseBindMatrices = new Float32Array(actionRuntime.rig.bones.length * 16);
    actionRuntime.rig.bones.forEach((bone, index) => {
      new THREE.Matrix4()
        .makeTranslation(-bone.head[0], -bone.head[1], bone.head[2])
        .toArray(inverseBindMatrices, index * 16);
    });
    const inverseBindAccessor = accessors.length;
    accessors.push({
      bufferView: addBufferView(float32Bytes(inverseBindMatrices)),
      componentType: FLOAT,
      count: actionRuntime.rig.bones.length,
      type: 'MAT4',
    });
    const jointNodes = actionRuntime.rig.bones.map((bone) => boneNodeIndices.get(bone.id)!);
    const skeletonRoot = actionRuntime.rig.bones.find((bone) => bone.parent === null) ?? actionRuntime.rig.bones[0];
    skins.push({
      name: `${options.name} Skeleton`,
      inverseBindMatrices: inverseBindAccessor,
      joints: jointNodes,
      skeleton: boneNodeIndices.get(skeletonRoot.id),
    });

    const componentNodeIndices = new Map<string, number>();
    actionRuntime.rig.components.forEach((component, index) => {
      const componentNodeIndex = nodes.length;
      componentNodeIndices.set(component.id, componentNodeIndex);
      nodes.push({
        name: actionNodeName('PCG_Component', component.id, index),
        children: [] as number[],
        extras: {
          pcgComponentId: component.id,
          parent: component.parent,
          bone: component.bone,
          role: component.role,
          skin: component.skin,
          detachable: component.detachable,
          pivot: component.pivot ?? null,
          region: component.region ?? null,
          ownershipPriority: component.ownershipPriority,
          logicalPivotOnly: true,
        },
      });
    });
    actionRuntime.rig.components.forEach((component) => {
      const nodeIndex = componentNodeIndices.get(component.id)!;
      const parentIndex = component.parent ? componentNodeIndices.get(component.parent) : undefined;
      if (parentIndex === undefined) rootChildren.push(nodeIndex);
      else (nodes[parentIndex].children as number[]).push(nodeIndex);
    });

    const componentSets = partitionActionComponents(mesh, actionRuntime, skinBinding);
    componentSets.forEach((entry, index) => {
      const primitives = entry.materialGroups.map((group) => {
        const indices = entry.indices.slice(group.start, group.start + group.count);
        return {
          attributes,
          indices: addIndexAccessor(indices),
          material: Math.min(group.materialIndex, materialDefinitions.length - 1),
          mode: 4,
        };
      });
      const meshIndex = meshes.length;
      meshes.push({ name: `${entry.component.id} Visual`, primitives });
      const visualNodeIndex = nodes.length;
      nodes.push({
        name: `${actionNodeName('PCG_Component', entry.component.id, index)}_Visual`,
        mesh: meshIndex,
        skin: 0,
        extras: {
          pcgComponentId: entry.component.id,
          role: 'visual',
          bakedRootSpace: true,
          logicalPivotNode: componentNodeIndices.get(entry.component.id),
        },
      });
      // Identity-bind contract: visuals live directly under the rig root.
      // Component nodes are logical pivots only, preventing double transforms.
      rootChildren.push(visualNodeIndex);
    });

    actionRuntime.rig.clips.forEach((clip) => {
      const samplers: Record<string, unknown>[] = [];
      const channels: Record<string, unknown>[] = [];
      for (const track of clip.tracks) {
        const times = Float32Array.from(track.times);
        const tupleSize = track.property === 'quaternion' ? 4 : 3;
        const values = track.property === 'quaternion'
          ? gltfQuaternionValues(track.values)
          : track.property === 'position'
            ? gltfVectorValues(track.values)
            : Float32Array.from(track.values);
        const inputAccessor = accessors.length;
        accessors.push({
          bufferView: addBufferView(float32Bytes(times)),
          componentType: FLOAT,
          count: times.length,
          type: 'SCALAR',
          min: [Math.min(...times)],
          max: [Math.max(...times)],
        });
        const outputAccessor = accessors.length;
        accessors.push({
          bufferView: addBufferView(float32Bytes(values)),
          componentType: FLOAT,
          count: track.times.length,
          type: tupleSize === 4 ? 'VEC4' : 'VEC3',
        });
        const samplerIndex = samplers.length;
        samplers.push({
          input: inputAccessor,
          output: outputAccessor,
          interpolation: track.interpolation,
        });
        channels.push({
          sampler: samplerIndex,
          target: {
            node: boneNodeIndices.get(track.bone),
            path: track.property === 'quaternion'
              ? 'rotation'
              : track.property === 'position'
                ? 'translation'
                : 'scale',
          },
        });
      }
      animations.push({
        name: clip.name,
        samplers,
        channels,
        extras: { loop: clip.loop, duration: clip.duration },
      });
    });
  }

  const binary = new Uint8Array(align4(binaryLength));
  for (const part of parts) binary.set(part.bytes, part.offset);

  const gltf = {
    asset: { version: '2.0', generator: 'PICG Web GLB Exporter' },
    scene: 0,
    scenes: [{ name: options.name, nodes: [0] }],
    nodes,
    meshes,
    ...(skins.length > 0 ? { skins } : {}),
    ...(animations.length > 0 ? { animations } : {}),
    materials: materialDefinitions.map((definition) => materialJson(definition, textureRegistry)),
    ...(textureRegistry.images.length > 0 ? {
      samplers: [{ magFilter: 9729, minFilter: 9987, wrapS: 10497, wrapT: 10497 }],
      images: textureRegistry.images,
      textures: textureRegistry.textures,
    } : {}),
    buffers: [{ byteLength: binaryLength }],
    bufferViews,
    accessors,
  };

  const jsonBytes = new TextEncoder().encode(JSON.stringify(gltf));
  const jsonChunkLength = align4(jsonBytes.length);
  const binChunkLength = binary.length;
  const totalLength = 12 + 8 + jsonChunkLength + 8 + binChunkLength;
  const output = new Uint8Array(totalLength);
  const outputView = new DataView(output.buffer);
  outputView.setUint32(0, GLB_MAGIC, true);
  outputView.setUint32(4, GLB_VERSION, true);
  outputView.setUint32(8, totalLength, true);
  outputView.setUint32(12, jsonChunkLength, true);
  outputView.setUint32(16, JSON_CHUNK, true);
  output.fill(0x20, 20, 20 + jsonChunkLength);
  output.set(jsonBytes, 20);
  const binHeader = 20 + jsonChunkLength;
  outputView.setUint32(binHeader, binChunkLength, true);
  outputView.setUint32(binHeader + 4, BIN_CHUNK, true);
  output.set(binary, binHeader + 8);
  return output.buffer;
}
