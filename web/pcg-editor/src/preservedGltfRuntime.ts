import * as THREE from 'three';
import { GLTFLoader } from 'three/examples/jsm/loaders/GLTFLoader.js';
import type {
  ActionLoopSource,
  ActionRuntimeController,
  ActionRuntimeDiagnostics,
} from './actionRuntime';

export interface PreservedGltfRigMetadata {
  schemaVersion: 1;
  route: 'preservedGltf';
  sourceNode: string;
  path: string;
  projectRoot: string;
  scale: number;
  axisConversion: 'none' | 'zUpToYUp' | 'yUpToZUp';
  continuousShell: true;
  componentSplitting: false;
}

export interface BuiltPreservedGltfRuntime {
  root: THREE.Group;
  controller: ActionRuntimeController;
  sourceBytes: ArrayBuffer;
}

function record(value: unknown, label: string): Record<string, unknown> {
  if (!value || typeof value !== 'object' || Array.isArray(value)) {
    throw new Error(`${label} must be an object`);
  }
  return value as Record<string, unknown>;
}

export function parsePreservedGltfRigFromCookJson(cookJson: string): PreservedGltfRigMetadata | null {
  if (!cookJson) return null;
  const root = record(JSON.parse(cookJson), 'cook result');
  const meshMetadata = root.mesh_metadata;
  if (!meshMetadata || typeof meshMetadata !== 'object' || Array.isArray(meshMetadata)) return null;
  const raw = (meshMetadata as Record<string, unknown>).pcg_source_rig;
  if (raw === undefined) return null;
  const source = record(raw, 'pcg_source_rig');
  if (source.schemaVersion !== 1 || source.route !== 'preservedGltf') {
    throw new Error('pcg_source_rig must use preservedGltf schemaVersion 1');
  }
  const path = typeof source.path === 'string' ? source.path.trim() : '';
  const scale = typeof source.scale === 'number' ? source.scale : Number.NaN;
  const axisConversion = source.axisConversion === 'zUpToYUp' || source.axisConversion === 'yUpToZUp'
    ? source.axisConversion
    : 'none';
  if (!path || !Number.isFinite(scale) || scale <= 0) {
    throw new Error('pcg_source_rig requires a path and positive scale');
  }
  return {
    schemaVersion: 1,
    route: 'preservedGltf',
    sourceNode: typeof source.sourceNode === 'string' ? source.sourceNode : '',
    path,
    projectRoot: typeof source.projectRoot === 'string' ? source.projectRoot : '',
    scale,
    axisConversion,
    continuousShell: true,
    componentSplitting: false,
  };
}

export async function loadPreservedGltfRig(
  metadata: PreservedGltfRigMetadata,
): Promise<BuiltPreservedGltfRuntime> {
  const response = await fetch(
    `/api/editor-bridge/assets/preserved-gltf?path=${encodeURIComponent(metadata.path)}`,
  );
  if (!response.ok) throw new Error(`Could not load preserved rig source (${response.status}).`);
  const sourceBytes = await response.arrayBuffer();
  const gltf = await new GLTFLoader().parseAsync(sourceBytes, '');
  const root = new THREE.Group();
  root.name = 'PCG_Preserved_GLTF_Root';
  root.userData.kind = 'action-root';
  root.scale.setScalar(metadata.scale);
  if (metadata.axisConversion === 'zUpToYUp') root.rotation.x = -Math.PI * 0.5;
  if (metadata.axisConversion === 'yUpToZUp') root.rotation.x = Math.PI * 0.5;
  root.add(gltf.scene);

  const skinnedMeshes: THREE.SkinnedMesh[] = [];
  gltf.scene.traverse((object) => {
    if (object instanceof THREE.SkinnedMesh) skinnedMeshes.push(object);
  });
  if (skinnedMeshes.length === 0) {
    throw new Error('PreserveGltfRig source contains no glTF skin/SkinnedMesh. Use ImportMesh or ActionRig instead.');
  }
  const skeletons = [...new Set(skinnedMeshes.map((mesh) => mesh.skeleton))];
  const objects: THREE.Object3D[] = [];
  gltf.scene.traverse((object) => objects.push(object));
  const restPose = objects.map((object) => ({
    position: object.position.clone(),
    quaternion: object.quaternion.clone(),
    scale: object.scale.clone(),
  }));
  const restoreRestPose = () => {
    objects.forEach((object, index) => {
      object.position.copy(restPose[index].position);
      object.quaternion.copy(restPose[index].quaternion);
      object.scale.copy(restPose[index].scale);
    });
    skeletons.forEach((skeleton) => skeleton.pose());
    root.updateMatrixWorld(true);
  };
  const mixer = new THREE.AnimationMixer(gltf.scene);
  // glTF has no loop flag. Match Img2Threejs's conservative contract: play
  // once until a measured authoring decision or a preview override exists.
  const clipLoops = new Map(gltf.animations.map((clip) => [clip.name, false]));
  const clipLoopSources = new Map<string, ActionLoopSource>(
    gltf.animations.map((clip) => [clip.name, 'unmeasured']),
  );
  let currentAnimation: string | null = null;
  let activeAction: THREE.AnimationAction | null = null;
  let playbackSpeed = 1;
  const play = (name: string): boolean => {
    const clip = THREE.AnimationClip.findByName(gltf.animations, name);
    if (!clip) return false;
    mixer.stopAllAction();
    restoreRestPose();
    activeAction = mixer.clipAction(clip).reset();
    activeAction.enabled = true;
    activeAction.paused = false;
    const loop = clipLoops.get(name) ?? false;
    activeAction.clampWhenFinished = !loop;
    activeAction.setLoop(loop ? THREE.LoopRepeat : THREE.LoopOnce, loop ? Infinity : 1);
    if (playbackSpeed < 0) activeAction.time = clip.duration;
    activeAction.play();
    mixer.timeScale = playbackSpeed;
    currentAnimation = name;
    return true;
  };
  const controller: ActionRuntimeController = {
    animationNames: gltf.animations.map((clip) => clip.name),
    get clips() {
      return gltf.animations.map((clip) => ({
        name: clip.name,
        duration: clip.duration,
        loop: clipLoops.get(clip.name) ?? false,
        loopSource: clipLoopSources.get(clip.name) ?? 'unmeasured',
      }));
    },
    componentNames: [],
    get currentAnimation() { return currentAnimation; },
    play,
    pause: () => {
      if (activeAction) activeAction.paused = true;
    },
    resume: () => {
      if (!activeAction || !currentAnimation) return false;
      const clip = THREE.AnimationClip.findByName(gltf.animations, currentAnimation);
      if (!clip) return false;
      if (
        activeAction.time >= clip.duration - 1e-6 ||
        (playbackSpeed < 0 && activeAction.time <= 1e-6)
      ) return play(currentAnimation);
      activeAction.paused = false;
      activeAction.enabled = true;
      activeAction.play();
      return true;
    },
    stop: () => {
      mixer.stopAllAction();
      activeAction = null;
      currentAnimation = null;
      restoreRestPose();
    },
    seek: (name, timeSeconds) => {
      if (currentAnimation !== name || !activeAction) {
        if (!play(name)) return false;
      }
      const clip = THREE.AnimationClip.findByName(gltf.animations, name)!;
      activeAction!.paused = true;
      activeAction!.time = THREE.MathUtils.clamp(timeSeconds, 0, clip.duration);
      mixer.update(0);
      root.updateMatrixWorld(true);
      return true;
    },
    advance: (deltaSeconds) => {
      if (Number.isFinite(deltaSeconds) && deltaSeconds > 0) mixer.update(deltaSeconds);
    },
    setPlaybackSpeed: (speed) => {
      if (!Number.isFinite(speed)) return;
      playbackSpeed = speed;
      mixer.timeScale = speed;
    },
    setLoop: (loop) => {
      if (!activeAction || !currentAnimation) return;
      clipLoops.set(currentAnimation, loop);
      clipLoopSources.set(currentAnimation, 'previewOverride');
      activeAction.clampWhenFinished = !loop;
      activeAction.setLoop(loop ? THREE.LoopRepeat : THREE.LoopOnce, loop ? Infinity : 1);
    },
    getPlaybackState: () => {
      const clip = currentAnimation
        ? THREE.AnimationClip.findByName(gltf.animations, currentAnimation)
        : null;
      return {
        clipName: currentAnimation,
        currentTime: activeAction?.time ?? 0,
        duration: clip?.duration ?? 0,
        playing: activeAction?.isRunning() ?? false,
        paused: activeAction?.paused ?? false,
        loop: currentAnimation ? (clipLoops.get(currentAnimation) ?? false) : false,
        loopSource: currentAnimation
          ? (clipLoopSources.get(currentAnimation) ?? 'unmeasured')
          : 'unmeasured',
        speed: playbackSpeed,
      };
    },
    // A preserved continuous shell deliberately has no semantic part explosion.
    setExplode: () => {},
    inspect: (): ActionRuntimeDiagnostics => {
      root.updateMatrixWorld(true);
      let visibleMeshCount = 0;
      let visibleSkinnedMeshCount = 0;
      let sampledVertexCount = 0;
      let nonFiniteSampleCount = 0;
      let maxSkinIndex = 0;
      let maxWeightError = 0;
      let maxBoneScaleDelta = 0;
      let restDelta = 0;
      objects.forEach((object, index) => {
        restDelta = Math.max(
          restDelta,
          object.position.distanceTo(restPose[index].position),
          1 - Math.abs(object.quaternion.dot(restPose[index].quaternion)),
          object.scale.distanceTo(restPose[index].scale),
        );
      });
      skeletons.forEach((skeleton) => skeleton.bones.forEach((bone) => {
        const index = objects.indexOf(bone);
        if (index >= 0) maxBoneScaleDelta = Math.max(
          maxBoneScaleDelta, bone.scale.distanceTo(restPose[index].scale),
        );
      }));
      const atRestPose = restDelta <= 1e-10;
      const source = new THREE.Vector3();
      const deformed = new THREE.Vector3();
      let sampledRestVertexMaxDelta = 0;
      gltf.scene.traverse((object) => {
        if (!(object instanceof THREE.Mesh) || !object.visible) return;
        visibleMeshCount++;
        if (!(object instanceof THREE.SkinnedMesh)) return;
        visibleSkinnedMeshCount++;
        const skinIndex = object.geometry.getAttribute('skinIndex');
        const skinWeight = object.geometry.getAttribute('skinWeight');
        const position = object.geometry.getAttribute('position');
        if (!skinIndex || !skinWeight || !position) return;
        for (let vertex = 0; vertex < skinWeight.count; vertex++) {
          let sum = 0;
          for (let slot = 0; slot < Math.min(4, skinWeight.itemSize); slot++) {
            const weight = skinWeight.getComponent(vertex, slot);
            sum += weight;
            if (weight > 0) maxSkinIndex = Math.max(maxSkinIndex, skinIndex.getComponent(vertex, slot));
          }
          maxWeightError = Math.max(maxWeightError, Math.abs(1 - sum));
        }
        const step = Math.max(1, Math.floor(position.count / 256));
        for (let vertex = 0; vertex < position.count; vertex += step) {
          source.fromBufferAttribute(position, vertex);
          object.getVertexPosition(vertex, deformed);
          sampledVertexCount++;
          if (![deformed.x, deformed.y, deformed.z].every(Number.isFinite)) nonFiniteSampleCount++;
          else if (atRestPose) sampledRestVertexMaxDelta = Math.max(
            sampledRestVertexMaxDelta, deformed.distanceTo(source),
          );
        }
      });
      return {
        bindingMethod: 'preserved',
        unreachableVertexCount: 0,
        rigidVertexCount: 0,
        visibleMeshCount,
        visibleSkinnedMeshCount,
        allVisibleMeshesBound: visibleMeshCount === visibleSkinnedMeshCount,
        sampledVertexCount,
        nonFiniteSampleCount,
        maxSkinIndex,
        maxWeightError,
        atRestPose,
        sampledRestVertexMaxDelta: atRestPose ? sampledRestVertexMaxDelta : null,
        maxBoneScaleDelta,
      };
    },
    dispose: () => {
      mixer.stopAllAction();
      mixer.uncacheRoot(gltf.scene);
    },
  };
  root.userData.sculptRuntime = {
    nodes: Object.fromEntries(objects.map((object, index) => [`node_${index}`, object])),
    components: {},
    sockets: {},
    colliders: [],
    destructionGroups: [],
  };
  root.userData.rig = {
    preserved: true,
    route: metadata.route,
    continuousShell: true,
    componentSplitting: false,
    skins: skeletons.map((skeleton) => ({
      bones: skeleton.bones,
      boneInverses: skeleton.boneInverses,
    })),
    bound: true,
  };
  root.userData.actionController = controller;
  root.animations = gltf.animations;
  return { root, controller, sourceBytes };
}
