import { describe, expect, it } from 'vitest';
import * as THREE from 'three';
import type { ParsedMesh } from './cookResult';
import {
  buildActionRigObject,
  computeActionSkinBinding,
  parseActionRuntimeFromCookJson,
  partitionActionComponents,
  pcgPositionsToThree,
  type ActionRuntimeMetadata,
} from './actionRuntime';

function runtime(): ActionRuntimeMetadata {
  return {
    schemaVersion: 1,
    sourceNode: 'rig',
    skinMode: 'distance',
    maxInfluences: 2,
    falloff: 4,
    geodesicResolution: 32,
    componentMode: 'dominantBone',
    splitComponents: true,
    autoplay: '',
    playbackSpeed: 1,
    rig: {
      schemaVersion: 1,
      sourceRoute: 'explicit',
      componentTree: [],
      bones: [
        { id: 'body', parent: null, head: [0, 0, 0], tail: [0, 1, 0], radius: 0.45 },
        { id: 'head', parent: 'body', head: [0, 1, 0], tail: [0, 2, 0], radius: 0.45 },
      ],
      components: [
        {
          id: 'body', bone: 'body', parent: null, role: 'structural', skin: 'smooth',
          detachable: false, ownershipPriority: 0,
        },
        {
          id: 'head', bone: 'head', parent: 'body', role: 'structural', skin: 'smooth',
          detachable: true, pivot: [0, 1.4, 0], ownershipPriority: 0,
        },
      ],
      clips: [{
        name: 'nod', duration: 1, loop: true,
        tracks: [{
          bone: 'head', property: 'quaternion', interpolation: 'LINEAR',
          times: [0, 0.5, 1],
          values: [
            0, 0, 0, 1,
            0.258819, 0, 0, 0.965926,
            0, 0, 0, 1,
          ],
        }],
      }],
      sockets: [{ id: 'hat', bone: 'head', position: [0, 0.5, 0] }],
      colliders: [{ id: 'body_hit', shape: 'capsule' }],
      destructionGroups: [{ id: 'head_break', components: ['head'] }],
    },
  };
}

function mesh(): ParsedMesh {
  return {
    positions: new Float32Array([
      -0.25, 0.1, 0, 0.25, 0.1, 0, 0, 0.8, 0,
      -0.25, 1.2, 0, 0.25, 1.2, 0, 0, 1.9, 0,
    ]),
    indices: new Uint32Array([0, 1, 2, 3, 4, 5]),
    normals: new Float32Array([
      0, 0, 1, 0, 0, 1, 0, 0, 1,
      0, 0, 1, 0, 0, 1, 0, 0, 1,
    ]),
    colors: null,
    uvs: null,
    materialSlots: ['Puppy'],
    triangleMaterials: new Uint32Array([0, 0]),
    vertexCount: 6,
    indexCount: 6,
  };
}

function twoCloseBoxes(): { positions: Float32Array; indices: Uint32Array; chestCorner: number } {
  const positions: number[] = [];
  const indices: number[] = [];
  const addBox = (min: [number, number, number], max: [number, number, number]) => {
    const base = positions.length / 3;
    positions.push(
      min[0], min[1], min[2], max[0], min[1], min[2], max[0], max[1], min[2], min[0], max[1], min[2],
      min[0], min[1], max[2], max[0], min[1], max[2], max[0], max[1], max[2], min[0], max[1], max[2],
    );
    const faces = [
      0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7,
      0, 1, 5, 0, 5, 4, 3, 7, 6, 3, 6, 2,
      0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6, 5,
    ];
    indices.push(...faces.map((index) => base + index));
  };
  addBox([-0.5, -0.5, -0.2], [0.45, 0.5, 0.2]);
  addBox([0.6, -0.45, -0.2], [0.8, 0.45, 0.2]);
  return { positions: new Float32Array(positions), indices: new Uint32Array(indices), chestCorner: 1 };
}

describe('ActionRig Three.js runtime', () => {
  it('parses cook metadata, normalizes top-four weights, and partitions every triangle once', () => {
    const expected = runtime();
    const parsed = parseActionRuntimeFromCookJson(JSON.stringify({
      mesh_metadata: { pcg_action_runtime: expected },
    }));
    expect(parsed?.rig.bones.map((bone) => bone.id)).toEqual(['body', 'head']);
    const unmeasured = structuredClone(expected) as ActionRuntimeMetadata;
    unmeasured.rig.clips[0].loop = null;
    expect(parseActionRuntimeFromCookJson(JSON.stringify({
      mesh_metadata: { pcg_action_runtime: unmeasured },
    }))?.rig.clips[0].loop).toBeNull();
    const sourceMesh = mesh();
    const positions = pcgPositionsToThree(sourceMesh.positions);
    const binding = computeActionSkinBinding(positions, expected);
    for (let vertex = 0; vertex < sourceMesh.vertexCount; vertex++) {
      const sum = binding.weights.slice(vertex * 4, vertex * 4 + 4)
        .reduce((total, value) => total + value, 0);
      expect(sum).toBeCloseTo(1, 6);
      expect(Math.max(...binding.joints.slice(vertex * 4, vertex * 4 + 4))).toBeLessThan(2);
    }
    const components = partitionActionComponents(sourceMesh, expected, binding);
    expect(components.map((entry) => entry.component.id).sort()).toEqual(['body', 'head']);
    expect(components.reduce((total, entry) => total + entry.indices.length, 0)).toBe(6);
    expect(components.flatMap((entry) => [...entry.sourceTriangles]).sort()).toEqual([0, 1]);
  });

  it('builds shared-skeleton component meshes and samples a real clip', () => {
    const built = buildActionRigObject(mesh(), runtime());
    const skinnedMeshes: THREE.SkinnedMesh[] = [];
    built.root.traverse((child) => {
      if (child instanceof THREE.SkinnedMesh) skinnedMeshes.push(child);
    });
    expect(skinnedMeshes).toHaveLength(2);
    expect(skinnedMeshes.every((child) => child.skeleton === built.skeleton)).toBe(true);
    expect(skinnedMeshes.every((child) => child.parent === built.root)).toBe(true);
    expect(built.controller.splitComponents).toBe(true);
    expect(built.controller.bones.map((bone) => bone.id)).toEqual(['body', 'head']);
    expect(built.controller.getBoneObject('head')).toBe(built.bones[1]);
    expect(built.controller.components).toMatchObject([
      { id: 'body', parent: null, visible: true, triangleCount: 1 },
      { id: 'head', parent: 'body', visible: true, triangleCount: 1 },
    ]);
    expect(built.root.userData.rig.bound).toBe(true);
    expect(built.root.userData.sculptRuntime.sockets.hat.parent).toBe(built.bones[1]);
    for (const skinnedMesh of skinnedMeshes) {
      const positions = skinnedMesh.geometry.getAttribute('position');
      const source = new THREE.Vector3().fromBufferAttribute(positions, 0);
      const bound = skinnedMesh.getVertexPosition(0, new THREE.Vector3());
      expect(bound.distanceTo(source)).toBeLessThan(1e-7);
    }
    const headMesh = skinnedMeshes.find((child) => child.userData.componentId === 'head');
    expect(headMesh).toBeDefined();
    const restHeadVertex = headMesh!.getVertexPosition(0, new THREE.Vector3()).clone();

    const rest = built.bones[1].quaternion.clone();
    expect(built.controller.clips).toEqual([{
      name: 'nod', duration: 1, loop: true, loopSource: 'authored',
    }]);
    expect(built.controller.play('nod')).toBe(true);
    built.controller.advance(0.2);
    expect(built.controller.getPlaybackState()).toMatchObject({
      clipName: 'nod', playing: true, paused: false,
      loop: true, loopSource: 'authored', speed: 1,
    });
    built.controller.pause();
    const pausedAt = built.controller.getPlaybackState().currentTime;
    built.controller.advance(0.2);
    expect(built.controller.getPlaybackState()).toMatchObject({ playing: false, paused: true });
    expect(built.controller.getPlaybackState().currentTime).toBeCloseTo(pausedAt, 6);
    expect(built.controller.resume()).toBe(true);
    built.controller.setPlaybackSpeed(1.5);
    built.controller.setLoop(false);
    expect(built.controller.getPlaybackState()).toMatchObject({
      playing: true, paused: false,
      loop: false, loopSource: 'previewOverride', speed: 1.5,
    });
    expect(built.controller.seek('nod', 0.5)).toBe(true);
    expect(built.controller.getPlaybackState()).toMatchObject({
      clipName: 'nod', currentTime: 0.5, duration: 1, playing: false, paused: true,
    });
    expect(Math.abs(rest.dot(built.bones[1].quaternion))).toBeLessThan(0.9999);
    const animatedHeadVertex = headMesh!.getVertexPosition(0, new THREE.Vector3());
    expect(animatedHeadVertex.distanceTo(restHeadVertex)).toBeGreaterThan(0.01);
    built.controller.setExplode(0.2);
    expect(built.componentPivots.head.position.length()).toBeCloseTo(0.2, 5);
    expect(skinnedMeshes.find((child) => child.userData.componentId === 'head')?.position.length())
      .toBeCloseTo(0.2, 5);
    expect(built.controller.setComponentVisible('head', false)).toBe(true);
    expect(headMesh!.visible).toBe(false);
    expect(built.controller.components.find((component) => component.id === 'head')?.visible).toBe(false);
    expect(built.controller.setComponentVisible('missing', false)).toBe(false);
    built.controller.setComponentVisible('head', true);
    built.controller.stop();
    expect(Math.abs(rest.dot(built.bones[1].quaternion))).toBeCloseTo(1, 6);
    built.controller.setPlaybackSpeed(-1);
    expect(built.controller.play('nod')).toBe(true);
    expect(built.controller.getPlaybackState().currentTime).toBeCloseTo(1, 6);
    built.controller.advance(0.1);
    expect(built.controller.getPlaybackState().currentTime).toBeLessThan(1);
    built.controller.resetPose();
    expect(built.controller.getPlaybackState().clipName).toBeNull();
    built.controller.dispose();
  });

  it('uses solid geodesic distance to stop a nearby detached limb from claiming torso vertices', () => {
    const geometry = twoCloseBoxes();
    const baseRuntime = runtime();
    baseRuntime.rig.bones = [
      { id: 'body', parent: null, head: [0, -0.4, 0], tail: [0, 0.4, 0], radius: 0.2 },
      { id: 'arm', parent: 'body', head: [0.7, -0.35, 0], tail: [0.7, 0.35, 0], radius: 0.2 },
    ];
    baseRuntime.rig.components = [
      {
        id: 'body', bone: 'body', parent: null, role: 'structural', skin: 'smooth',
        detachable: false, ownershipPriority: 0,
      },
      {
        id: 'arm', bone: 'arm', parent: 'body', role: 'structural', skin: 'smooth',
        detachable: true, ownershipPriority: 0,
      },
    ];
    const euclidean = computeActionSkinBinding(geometry.positions, baseRuntime, geometry.indices);
    const geodesicRuntime = { ...baseRuntime, skinMode: 'geodesic' as const };
    const geodesic = computeActionSkinBinding(geometry.positions, geodesicRuntime, geometry.indices);
    const offset = geometry.chestCorner * 4;
    const armWeight = (binding: ReturnType<typeof computeActionSkinBinding>) => {
      let weight = 0;
      for (let slot = 0; slot < 4; slot++) {
        if (binding.joints[offset + slot] === 1) weight += binding.weights[offset + slot];
      }
      return weight;
    };
    expect(armWeight(euclidean)).toBeGreaterThan(0.5);
    expect(armWeight(geodesic)).toBeLessThan(0.01);
    expect(geodesic.unreachableVertexCount).toBe(0);
  });

  it('derives a parents-first skeleton and rigid attachment from schema v2 componentTree', () => {
    const parsed = parseActionRuntimeFromCookJson(JSON.stringify({
      mesh_metadata: {
        pcg_action_runtime: {
          schemaVersion: 1,
          sourceNode: 'tree-rig',
          skinMode: 'distance',
          maxInfluences: 4,
          falloff: 4,
          geodesicResolution: 32,
          componentMode: 'semanticRegion',
          splitComponents: true,
          autoplay: '',
          playbackSpeed: 1,
          rig: {
            schemaVersion: 2,
            componentTree: [
              {
                id: 'root', parent: null, pivot: [0, 0, 0], tip: [0, 1, 0],
                radius: 0.45, role: 'structural', joint: true,
              },
              {
                id: 'head', parent: 'root', pivot: [0, 1, 0], tip: [0, 2, 0],
                radius: 0.4, role: 'structural', joint: true, detachable: true,
              },
              {
                id: 'hair', parent: 'head', pivot: [0, 1.6, 0], radius: 0.3,
                role: 'hair', joint: false, detachable: true, ownershipPriority: 10,
                region: { type: 'sphere', center: [0, 1.55, 0], radius: 0.7 },
              },
            ],
            clips: [], sockets: [], colliders: [], destructionGroups: [],
          },
        },
      },
    }));
    expect(parsed).not.toBeNull();
    expect(parsed!.rig.sourceRoute).toBe('componentTree');
    expect(parsed!.rig.bones.map((bone) => bone.id)).toEqual(['root', 'head']);
    expect(parsed!.rig.bones[1].parent).toBe('root');
    expect(parsed!.rig.components.find((component) => component.id === 'hair')).toMatchObject({
      bone: 'head', skin: 'rigid', role: 'hair', parent: 'head',
    });

    const sourceMesh = mesh();
    const binding = computeActionSkinBinding(
      pcgPositionsToThree(sourceMesh.positions), parsed!, sourceMesh.indices,
    );
    expect(binding.rigidVertexCount).toBe(3);
    for (const vertex of [3, 4, 5]) {
      expect(binding.joints[vertex * 4]).toBe(1);
      expect(binding.weights[vertex * 4]).toBe(1);
      expect(binding.weights.slice(vertex * 4 + 1, vertex * 4 + 4)).toEqual(new Float32Array(3));
    }
  });
});
