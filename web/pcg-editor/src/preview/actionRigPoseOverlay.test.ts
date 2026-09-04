import { describe, expect, it } from 'vitest';
import * as THREE from 'three';

import type { ActionRuntimeController } from '../actionRuntime';
import { createActionRigPoseOverlay } from './actionRigPoseOverlay';

describe('ActionRig pose overlay', () => {
  it('tracks posed bones and highlights the active bone', () => {
    const root = new THREE.Group();
    const body = new THREE.Bone();
    const head = new THREE.Bone();
    body.add(head);
    head.position.set(0, 1, 0);
    root.add(body);
    root.updateMatrixWorld(true);
    const byId = new Map([['body', body], ['head', head]]);
    const controller = {
      bones: [
        { id: 'body', name: 'body', parent: null, tailOffset: [0, 1, 0], radius: 0.4 },
        { id: 'head', name: 'head', parent: 'body', tailOffset: [0, 0.5, 0], radius: 0.3 },
      ],
      getBoneObject: (id: string) => byId.get(id) ?? null,
    } as unknown as ActionRuntimeController;

    const overlay = createActionRigPoseOverlay(controller);
    overlay.setSelectedBone('head');
    const shaft = overlay.group.getObjectByName('PCG_Bone_Overlay_head') as THREE.Mesh;
    expect((shaft.material as THREE.MeshBasicMaterial).color.getHex()).toBe(0xffa52f);
    const before = shaft.quaternion.clone();
    body.rotation.z = Math.PI / 4;
    root.updateMatrixWorld(true);
    overlay.update();
    expect(Math.abs(before.dot(shaft.quaternion))).toBeLessThan(0.999);
    overlay.dispose();
  });
});
