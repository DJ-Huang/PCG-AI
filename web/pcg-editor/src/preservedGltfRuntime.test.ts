import { afterEach, describe, expect, it, vi } from 'vitest';
import * as THREE from 'three';
import type { ParsedMesh } from './cookResult';
import type { ActionRuntimeMetadata } from './actionRuntime';
import { exportPreviewMeshGlb } from './previewGlbExport';
import {
  loadPreservedGltfRig,
  parsePreservedGltfRigFromCookJson,
  type PreservedGltfRigMetadata,
} from './preservedGltfRuntime';

function animatedGlb(): ArrayBuffer {
  const mesh: ParsedMesh = {
    positions: new Float32Array([
      -0.2, 0, 0, 0.2, 0, 0, 0, 0.8, 0,
      -0.2, 1, 0, 0.2, 1, 0, 0, 1.8, 0,
    ]),
    indices: new Uint32Array([0, 1, 2, 3, 4, 5]),
    normals: new Float32Array([
      0, 0, 1, 0, 0, 1, 0, 0, 1,
      0, 0, 1, 0, 0, 1, 0, 0, 1,
    ]),
    colors: null,
    uvs: null,
    materialSlots: ['Default'],
    triangleMaterials: new Uint32Array([0, 0]),
    vertexCount: 6,
    indexCount: 6,
  };
  const actionRuntime: ActionRuntimeMetadata = {
    schemaVersion: 1,
    sourceNode: 'source-rig',
    skinMode: 'distance',
    maxInfluences: 2,
    falloff: 4,
    geodesicResolution: 32,
    componentMode: 'none',
    splitComponents: false,
    autoplay: '',
    playbackSpeed: 1,
    rig: {
      schemaVersion: 1,
      sourceRoute: 'explicit',
      componentTree: [],
      bones: [
        { id: 'body', parent: null, head: [0, 0, 0], tail: [0, 1, 0], radius: 0.45 },
        { id: 'head', parent: 'body', head: [0, 1, 0], tail: [0, 2, 0], radius: 0.4 },
      ],
      components: [{
        id: 'body', bone: 'body', parent: null, role: 'structural', skin: 'smooth',
        detachable: false, ownershipPriority: 0,
      }],
      clips: [{
        name: 'nod', duration: 1, loop: true,
        tracks: [{
          bone: 'head', property: 'quaternion', interpolation: 'LINEAR',
          times: [0, 0.5, 1],
          values: [0, 0, 0, 1, 0.258819, 0, 0, 0.965926, 0, 0, 0, 1],
        }],
      }],
      sockets: [], colliders: [], destructionGroups: [],
    },
  };
  return exportPreviewMeshGlb(mesh, {}, {
    name: 'Preserved Fixture',
    graphPath: 'examples/preserved-fixture.pcg',
    actionRuntime,
  });
}

const metadata: PreservedGltfRigMetadata = {
  schemaVersion: 1,
  route: 'preservedGltf',
  sourceNode: 'preserve',
  path: 'fixtures/rigged.glb',
  projectRoot: '',
  scale: 1,
  axisConversion: 'none',
  continuousShell: true,
  componentSplitting: false,
};

afterEach(() => vi.restoreAllMocks());

describe('PreserveGltfRig Three.js runtime', () => {
  it('parses the independent source-rig contract', () => {
    expect(parsePreservedGltfRigFromCookJson(JSON.stringify({
      mesh_metadata: { pcg_source_rig: metadata },
    }))).toEqual(metadata);
    expect(parsePreservedGltfRigFromCookJson(JSON.stringify({ mesh_metadata: {} }))).toBeNull();
  });

  it('keeps the original skin, inverse binds, weights, animation, and source bytes', async () => {
    const source = animatedGlb();
    vi.spyOn(globalThis, 'fetch').mockResolvedValue(new Response(source, {
      status: 200,
      headers: { 'Content-Type': 'model/gltf-binary' },
    }));
    const built = await loadPreservedGltfRig(metadata);
    expect(new Uint8Array(built.sourceBytes)).toEqual(new Uint8Array(source));
    expect(built.root.userData.rig).toMatchObject({
      preserved: true,
      route: 'preservedGltf',
      continuousShell: true,
      componentSplitting: false,
      bound: true,
    });
    const meshes: THREE.SkinnedMesh[] = [];
    built.root.traverse((object) => {
      if (object instanceof THREE.SkinnedMesh) meshes.push(object);
    });
    expect(meshes).toHaveLength(1);
    expect(meshes[0].skeleton.bones.map((bone) => bone.name)).toEqual([
      'PCG_Bone_body', 'PCG_Bone_head',
    ]);
    expect(meshes[0].skeleton.boneInverses).toHaveLength(2);
    expect(meshes[0].geometry.getAttribute('skinIndex')).toBeTruthy();
    expect(meshes[0].geometry.getAttribute('skinWeight')).toBeTruthy();
    expect(built.controller.animationNames).toEqual(['nod']);
    expect(built.controller.clips).toEqual([{
      name: 'nod', duration: 1, loop: false, loopSource: 'unmeasured',
    }]);
    expect(built.controller.play('nod')).toBe(true);
    built.controller.pause();
    expect(built.controller.getPlaybackState()).toMatchObject({
      clipName: 'nod', playing: false, paused: true,
      loop: false, loopSource: 'unmeasured',
    });
    built.controller.setLoop(false);
    expect(built.controller.resume()).toBe(true);
    expect(built.controller.getPlaybackState()).toMatchObject({
      playing: true, loop: false, loopSource: 'previewOverride',
    });
    expect(built.controller.seek('nod', 0.5)).toBe(true);
    expect(built.controller.getPlaybackState()).toMatchObject({
      currentTime: 0.5, duration: 1, playing: false, paused: true,
    });
    expect(built.controller.inspect()).toMatchObject({
      bindingMethod: 'preserved',
      visibleMeshCount: 1,
      visibleSkinnedMeshCount: 1,
      allVisibleMeshesBound: true,
      nonFiniteSampleCount: 0,
    });
    built.controller.stop();
    expect(built.controller.inspect().atRestPose).toBe(true);
    built.controller.setPlaybackSpeed(-1);
    expect(built.controller.play('nod')).toBe(true);
    expect(built.controller.getPlaybackState().currentTime).toBeCloseTo(1, 6);
    built.controller.advance(0.1);
    expect(built.controller.getPlaybackState().currentTime).toBeLessThan(1);
    built.controller.stop();
    built.controller.dispose();
  });
});
