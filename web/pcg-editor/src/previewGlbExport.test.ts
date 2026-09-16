import { describe, expect, it } from 'vitest';
import * as THREE from 'three';
import { GLTFLoader } from 'three/examples/jsm/loaders/GLTFLoader.js';
import type { ParsedMesh } from './cookResult';
import { exportPreviewMeshGlb } from './previewGlbExport';
import type { ActionRuntimeMetadata } from './actionRuntime';

function glbJson(buffer: ArrayBuffer) {
  const view = new DataView(buffer);
  const jsonLength = view.getUint32(12, true);
  const bytes = new Uint8Array(buffer, 20, jsonLength);
  return JSON.parse(new TextDecoder().decode(bytes).trim());
}

describe('exportPreviewMeshGlb', () => {
  it('exports a materialized hierarchy with source graph metadata', async () => {
    const mesh: ParsedMesh = {
      positions: new Float32Array([0, 0, 1, 1, 0, 1, 0, 1, 1]),
      indices: new Uint32Array([0, 1, 2]),
      normals: new Float32Array([0, 0, 1, 0, 0, 1, 0, 0, 1]),
      colors: null,
      uvs: new Float32Array([0, 0, 1, 0, 0, 1]),
      materialSlots: ['Harness'],
      triangleMaterials: new Uint32Array([0]),
      vertexCount: 3,
      indexCount: 3,
    };
    const result = exportPreviewMeshGlb(mesh, {
      Harness: {
        kind: 'pcg.material', version: 1, name: 'Harness', shaderId: 'pcg.standard-pbr',
        baseColor: '#e6aa18', baseColorMap: '', metallic: 0, metallicMap: '',
        roughness: 0.48, roughnessMap: '', normalMap: '', normalScale: 1,
        aoMap: '', aoIntensity: 1, emissiveColor: '#000000', emissiveMap: '',
        emissiveIntensity: 0, opacity: 1, alphaMode: 'opaque', alphaCutoff: 0.5,
        doubleSided: false, unityShaderGuid: '', unityShaderName: '', unityPropertiesJson: '{}',
      },
    }, {
      name: 'YOYO Puppy',
      graphPath: 'examples/showcases/tripo-yoyo-puppy/procedural.pcg',
      parameters: { 'Body Scale': 1 },
    });

    const view = new DataView(result);
    expect(view.getUint32(0, true)).toBe(0x46546c67);
    expect(view.getUint32(4, true)).toBe(2);
    expect(view.getUint32(8, true)).toBe(result.byteLength);
    const json = glbJson(result);
    expect(json.asset.generator).toBe('PICG Web GLB Exporter');
    expect(json.nodes[0].extras.pcgGraph).toBe('examples/showcases/tripo-yoyo-puppy/procedural.pcg');
    expect(json.nodes[0].children).toEqual([1]);
    expect(json.meshes[0].primitives[0].material).toBe(0);
    expect(json.materials[0].name).toBe('Harness');
    expect(json.materials[0].pbrMetallicRoughness.roughnessFactor).toBe(0.48);

    const loaded = await new Promise<Awaited<ReturnType<GLTFLoader['parseAsync']>>>((resolve, reject) => {
      new GLTFLoader().parse(result, '', resolve, reject);
    });
    expect(loaded.scene.children[0].name).toBe('YOYO_Puppy_Root');
    expect(loaded.scene.getObjectByName('YOYO_Puppy')).toBeTruthy();
  });

  it('preserves self-contained base-colour, ORM, normal, and AO texture bindings', () => {
    const mesh: ParsedMesh = {
      positions: new Float32Array([0, 0, 0, 1, 0, 0, 0, 1, 0]),
      indices: new Uint32Array([0, 1, 2]),
      normals: new Float32Array([0, 0, 1, 0, 0, 1, 0, 0, 1]),
      colors: null,
      uvs: new Float32Array([0, 0, 1, 0, 0, 1]),
      materialSlots: ['Baked'], triangleMaterials: new Uint32Array([0]),
      vertexCount: 3, indexCount: 3,
    };
    const result = exportPreviewMeshGlb(mesh, {
      Baked: {
        kind: 'pcg.material', version: 1, name: 'Baked', shaderId: 'pcg.standard-pbr',
        baseColor: '#ffffff', baseColorMap: 'data:image/jpeg;base64,YmFzZQ==',
        metallic: 1, metallicMap: 'data:image/png;base64,b3Jt',
        roughness: 1, roughnessMap: 'data:image/png;base64,b3Jt',
        normalMap: 'data:image/png;base64,bm9ybWFs', normalScale: 0.75,
        aoMap: 'data:image/png;base64,b3Jt', aoIntensity: 0.8,
        emissiveColor: '#000000', emissiveMap: '', emissiveIntensity: 0,
        opacity: 1, alphaMode: 'opaque', alphaCutoff: 0.5, doubleSided: false,
        unityShaderGuid: '', unityShaderName: '', unityPropertiesJson: '{}',
      },
    }, { name: 'Baked Puppy', graphPath: 'examples/baked.pcg' });
    const json = glbJson(result);
    expect(json.images).toHaveLength(3);
    expect(json.textures).toHaveLength(3);
    expect(json.materials[0].pbrMetallicRoughness.baseColorTexture.index).toBe(0);
    expect(json.materials[0].pbrMetallicRoughness.metallicRoughnessTexture.index).toBe(1);
    expect(json.materials[0].normalTexture).toMatchObject({ index: 2, scale: 0.75 });
    expect(json.materials[0].occlusionTexture).toMatchObject({ index: 1, strength: 0.8 });
  });

  it('exports standard glTF skins, component nodes, sockets, and animation channels', async () => {
    const mesh: ParsedMesh = {
      positions: new Float32Array([-0.2, 0, 0, 0.2, 0, 0, 0, 1, 0]),
      indices: new Uint32Array([0, 1, 2]),
      normals: new Float32Array([0, 0, 1, 0, 0, 1, 0, 0, 1]),
      colors: null,
      uvs: null,
      materialSlots: ['Puppy'],
      triangleMaterials: new Uint32Array([0]),
      vertexCount: 3,
      indexCount: 3,
    };
    const actionRuntime: ActionRuntimeMetadata = {
      schemaVersion: 1,
      sourceNode: 'puppy_action_rig',
      skinMode: 'distance',
      maxInfluences: 2,
      falloff: 4,
      geodesicResolution: 32,
      componentMode: 'dominantBone',
      splitComponents: true,
      autoplay: 'nod',
      playbackSpeed: 1,
      rig: {
        schemaVersion: 1,
        sourceRoute: 'explicit',
        componentTree: [],
        bones: [
          { id: 'body', parent: null, head: [0, 0, 0], tail: [0, 0.5, 0], radius: 0.5 },
          { id: 'head', parent: 'body', head: [0, 0.5, 0], tail: [0, 1, 0], radius: 0.35 },
        ],
        components: [{
          id: 'head', bone: 'head', parent: null, role: 'structural', skin: 'smooth',
          detachable: true, pivot: [0, 0.7, 0], ownershipPriority: 0,
        }],
        sockets: [{ id: 'hat', bone: 'head', position: [0, 0.4, 0] }],
        colliders: [{ id: 'body_hit', shape: 'capsule' }],
        destructionGroups: [{ id: 'head_break', components: ['head'] }],
        clips: [{
          name: 'nod', duration: 1, loop: true,
          tracks: [{
            bone: 'head', property: 'quaternion', interpolation: 'LINEAR',
            times: [0, 0.5, 1],
            values: [0, 0, 0, 1, 0.258819, 0, 0, 0.965926, 0, 0, 0, 1],
          }],
        }],
      },
    };
    const result = exportPreviewMeshGlb(mesh, {}, {
      name: 'Animated Puppy',
      graphPath: 'examples/animated.pcg',
      actionRuntime,
    });
    const json = glbJson(result);
    expect(json.skins).toHaveLength(1);
    expect(json.animations[0].name).toBe('nod');
    expect(json.meshes[0].primitives[0].attributes).toMatchObject({
      POSITION: expect.any(Number),
      JOINTS_0: expect.any(Number),
      WEIGHTS_0: expect.any(Number),
    });
    expect(json.nodes.some((node: { extras?: { pcgComponentId?: string } }) => (
      node.extras?.pcgComponentId === 'head'
    ))).toBe(true);
    expect(json.nodes.some((node: { extras?: { pcgSocketId?: string } }) => (
      node.extras?.pcgSocketId === 'hat'
    ))).toBe(true);

    const loaded = await new Promise<Awaited<ReturnType<GLTFLoader['parseAsync']>>>((resolve, reject) => {
      new GLTFLoader().parse(result, '', resolve, reject);
    });
    const skinnedMeshes: THREE.SkinnedMesh[] = [];
    loaded.scene.traverse((child) => {
      if (child instanceof THREE.SkinnedMesh) skinnedMeshes.push(child);
    });
    expect(skinnedMeshes).toHaveLength(1);
    expect(skinnedMeshes[0].parent?.name).toBe('Animated_Puppy_Root');
    expect(loaded.animations.map((clip) => clip.name)).toEqual(['nod']);
    const head = loaded.scene.getObjectByName('PCG_Bone_head');
    expect(head).toBeTruthy();
    const rest = head!.quaternion.clone();
    const mixer = new THREE.AnimationMixer(loaded.scene);
    mixer.clipAction(loaded.animations[0]).play();
    mixer.update(0.5);
    expect(Math.abs(rest.dot(head!.quaternion))).toBeLessThan(0.9999);
  });
});
