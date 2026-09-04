import * as THREE from 'three';

import type { ActionBoneInfo, ActionRuntimeController } from '../actionRuntime';

const BONE_COLOR = 0xaeb9c5;
const ACTIVE_BONE_COLOR = 0xffa52f;

interface BoneVisual {
  info: ActionBoneInfo;
  bone: THREE.Bone;
  tailLocal: THREE.Vector3;
  shaft: THREE.Mesh<THREE.CylinderGeometry, THREE.MeshBasicMaterial>;
  joint: THREE.Mesh<THREE.SphereGeometry, THREE.MeshBasicMaterial>;
  picker: THREE.Mesh<THREE.CylinderGeometry, THREE.MeshBasicMaterial>;
}

export interface ActionRigPoseOverlay {
  readonly group: THREE.Group;
  readonly selectedBoneId: string | null;
  setSelectedBone(id: string | null): void;
  pickBone(camera: THREE.Camera, rect: DOMRect, clientX: number, clientY: number): string | null;
  update(): void;
  dispose(): void;
}

function orientSegment(
  object: THREE.Object3D,
  head: THREE.Vector3,
  tail: THREE.Vector3,
  radius: number,
) {
  const direction = tail.clone().sub(head);
  const length = Math.max(direction.length(), 1e-5);
  object.position.copy(head).addScaledVector(direction, 0.5);
  object.quaternion.setFromUnitVectors(
    new THREE.Vector3(0, 1, 0),
    direction.multiplyScalar(1 / length),
  );
  object.scale.set(radius, length, radius);
}

/**
 * Blender-inspired pose overlay: bones stay visible through the mesh, the
 * active bone is orange, and a wider invisible shaft makes selection usable.
 */
export function createActionRigPoseOverlay(
  controller: ActionRuntimeController,
): ActionRigPoseOverlay {
  const group = new THREE.Group();
  group.name = 'PCG_Action_Rig_Pose_Overlay';
  group.userData.kind = 'action-rig-overlay';

  const visuals: BoneVisual[] = [];
  const pickerMaterial = new THREE.MeshBasicMaterial({
    transparent: true,
    opacity: 0,
    depthWrite: false,
  });

  for (const info of controller.bones) {
    const bone = controller.getBoneObject(info.id);
    if (!bone) continue;
    const material = new THREE.MeshBasicMaterial({
      color: BONE_COLOR,
      transparent: true,
      opacity: 0.92,
      depthTest: false,
      depthWrite: false,
      toneMapped: false,
    });
    const jointMaterial = material.clone();
    const shaft = new THREE.Mesh(new THREE.CylinderGeometry(1, 1, 1, 8, 1), material);
    const joint = new THREE.Mesh(new THREE.SphereGeometry(1, 12, 8), jointMaterial);
    const picker = new THREE.Mesh(
      new THREE.CylinderGeometry(1, 1, 1, 8, 1),
      pickerMaterial,
    );
    shaft.name = `PCG_Bone_Overlay_${info.id}`;
    joint.name = `PCG_Bone_Joint_${info.id}`;
    picker.name = `PCG_Bone_Picker_${info.id}`;
    shaft.renderOrder = joint.renderOrder = 1000;
    picker.userData.boneId = info.id;
    group.add(shaft, joint, picker);
    visuals.push({
      info,
      bone,
      tailLocal: new THREE.Vector3(...info.tailOffset),
      shaft,
      joint,
      picker,
    });
  }

  let selectedBoneId: string | null = null;
  const head = new THREE.Vector3();
  const tail = new THREE.Vector3();
  const raycaster = new THREE.Raycaster();
  const pointer = new THREE.Vector2();

  const update = () => {
    for (const visual of visuals) {
      visual.bone.updateWorldMatrix(true, false);
      visual.bone.getWorldPosition(head);
      tail.copy(visual.tailLocal);
      visual.bone.localToWorld(tail);
      const length = Math.max(head.distanceTo(tail), 1e-5);
      const baseRadius = Math.max(0.006, Math.min(length * 0.09, visual.info.radius * 0.16));
      const active = visual.info.id === selectedBoneId;
      const displayRadius = baseRadius * (active ? 1.35 : 1);
      orientSegment(visual.shaft, head, tail, displayRadius);
      orientSegment(visual.picker, head, tail, Math.max(baseRadius * 3.5, length * 0.12));
      visual.joint.position.copy(head);
      visual.joint.scale.setScalar(displayRadius * 1.3);
      visual.shaft.material.color.setHex(active ? ACTIVE_BONE_COLOR : BONE_COLOR);
      visual.joint.material.color.setHex(active ? ACTIVE_BONE_COLOR : BONE_COLOR);
    }
    group.updateMatrixWorld(true);
  };

  const overlay: ActionRigPoseOverlay = {
    group,
    get selectedBoneId() { return selectedBoneId; },
    setSelectedBone: (id) => {
      selectedBoneId = visuals.some((visual) => visual.info.id === id) ? id : null;
      update();
    },
    pickBone: (camera, rect, clientX, clientY) => {
      if (rect.width <= 0 || rect.height <= 0) return null;
      update();
      pointer.set(
        ((clientX - rect.left) / rect.width) * 2 - 1,
        -((clientY - rect.top) / rect.height) * 2 + 1,
      );
      raycaster.setFromCamera(pointer, camera);
      const hit = raycaster.intersectObjects(visuals.map((visual) => visual.picker), false)[0];
      return typeof hit?.object.userData.boneId === 'string' ? hit.object.userData.boneId : null;
    },
    update,
    dispose: () => {
      for (const visual of visuals) {
        visual.shaft.geometry.dispose();
        visual.shaft.material.dispose();
        visual.joint.geometry.dispose();
        visual.joint.material.dispose();
        visual.picker.geometry.dispose();
      }
      pickerMaterial.dispose();
      group.clear();
    },
  };
  update();
  return overlay;
}
