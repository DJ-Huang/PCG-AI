#!/usr/bin/env node

import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import * as THREE from 'three';
import { GLTFLoader } from 'three/examples/jsm/loaders/GLTFLoader.js';

globalThis.ProgressEvent ??= class ProgressEvent {
  constructor(type, init = {}) {
    this.type = type;
    Object.assign(this, init);
  }
};

const graphPath = process.argv[2];
const glbPath = process.argv[3];
if (!graphPath || !glbPath) {
  console.error('Usage: node scripts/validate-action-glb.mjs <graph.pcg> <asset.glb>');
  process.exit(2);
}

const failures = [];
const fail = (message) => failures.push(message);
const near = (a, b, epsilon = 1e-4) => Math.abs(a - b) <= epsilon;
const safeId = (id) => String(id).replace(/[^A-Za-z0-9_-]/g, '_');

function parseGlb(bytes) {
  const arrayBuffer = bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
  const view = new DataView(arrayBuffer);
  if (view.byteLength < 20 || view.getUint32(0, true) !== 0x46546c67) {
    throw new Error('Input is not a binary glTF file.');
  }
  if (view.getUint32(4, true) !== 2) throw new Error('Only glTF 2.0 is supported.');
  if (view.getUint32(8, true) !== view.byteLength) throw new Error('GLB header length is stale.');
  const jsonLength = view.getUint32(12, true);
  if (view.getUint32(16, true) !== 0x4e4f534a) throw new Error('GLB has no leading JSON chunk.');
  const json = JSON.parse(new TextDecoder().decode(new Uint8Array(arrayBuffer, 20, jsonLength)).trim());
  return { arrayBuffer, json };
}

const graph = JSON.parse(readFileSync(resolve(graphPath), 'utf8'));
const actionNodes = (graph.nodes ?? []).filter((node) => node.type === 'ActionRig');
if (actionNodes.length !== 1) {
  throw new Error(`Expected exactly one ActionRig node, found ${actionNodes.length}.`);
}
const actionData = actionNodes[0].data ?? {};
const rig = JSON.parse(actionData.rigJson ?? '{}');
const components = Array.isArray(rig.components)
  ? rig.components
  : Array.isArray(rig.componentTree)
    ? rig.componentTree
    : [];
const bones = Array.isArray(rig.bones)
  ? rig.bones
  : components.filter((component) => component.joint !== false);
const clips = Array.isArray(rig.clips) ? rig.clips : [];
if (components.length === 0) fail('ActionRig declares no semantic components.');
if (bones.length === 0) fail('ActionRig declares no bones.');
if (clips.length === 0) fail('ActionRig declares no animation clips.');

const bytes = readFileSync(resolve(glbPath));
const { arrayBuffer, json } = parseGlb(bytes);
const rigRoot = (json.nodes ?? []).find((node) => node.extras?.pcgActionRig);
if (!rigRoot) fail('GLB root is missing extras.pcgActionRig.');
if ((json.skins ?? []).length !== 1) fail(`Expected one shared skin, found ${(json.skins ?? []).length}.`);
if ((json.buffers ?? []).some((buffer) => typeof buffer.uri === 'string')) {
  fail('GLB depends on an external buffer URI.');
}

const expectedComponentIds = components.map((component) => String(component.id));
const metadataComponentIds = rigRoot?.extras?.pcgActionRig?.componentNames ?? [];
if (JSON.stringify(metadataComponentIds) !== JSON.stringify(expectedComponentIds)) {
  fail('GLB component metadata does not match the ActionRig component order.');
}
const logicalComponents = (json.nodes ?? []).filter((node) => node.extras?.logicalPivotOnly === true);
const visualComponents = (json.nodes ?? []).filter((node) => node.extras?.role === 'visual');
for (const id of expectedComponentIds) {
  if (!logicalComponents.some((node) => node.extras?.pcgComponentId === id)) {
    fail(`Missing logical component node ${id}.`);
  }
  if (!visualComponents.some((node) => node.extras?.pcgComponentId === id && node.skin === 0)) {
    fail(`Missing root-bound skinned visual for component ${id}.`);
  }
}
if (visualComponents.length !== expectedComponentIds.length) {
  fail(`Expected ${expectedComponentIds.length} component visuals, found ${visualComponents.length}.`);
}

const expectedBoneIds = bones.map((bone) => String(bone.id));
const glbBoneIds = (json.nodes ?? [])
  .filter((node) => typeof node.extras?.pcgBoneId === 'string')
  .map((node) => node.extras.pcgBoneId);
if (JSON.stringify(glbBoneIds) !== JSON.stringify(expectedBoneIds)) {
  fail('GLB bone IDs/order do not match the ActionRig contract.');
}

const jsonAnimations = new Map((json.animations ?? []).map((animation) => [animation.name, animation]));
for (const clip of clips) {
  const exported = jsonAnimations.get(clip.name);
  if (!exported) {
    fail(`Missing animation ${clip.name}.`);
    continue;
  }
  if (!near(Number(exported.extras?.duration), Number(clip.duration))) {
    fail(`Animation ${clip.name} duration drifted: graph=${clip.duration}, GLB=${exported.extras?.duration}.`);
  }
}
if (jsonAnimations.size !== clips.length) {
  fail(`Expected ${clips.length} animations, found ${jsonAnimations.size}.`);
}

const gltf = await new GLTFLoader().parseAsync(arrayBuffer, '');
const skinnedMeshes = [];
gltf.scene.traverse((object) => {
  if (object instanceof THREE.SkinnedMesh) skinnedMeshes.push(object);
});
const skeletons = [...new Set(skinnedMeshes.map((mesh) => mesh.skeleton))];
if (skinnedMeshes.length !== expectedComponentIds.length) {
  fail(`Runtime loaded ${skinnedMeshes.length} SkinnedMeshes; expected ${expectedComponentIds.length}.`);
}
if (skeletons.length !== 1) fail(`Runtime loaded ${skeletons.length} skeletons; expected one shared skeleton.`);
if ((skeletons[0]?.bones.length ?? 0) !== expectedBoneIds.length) {
  fail(`Runtime skeleton has ${skeletons[0]?.bones.length ?? 0} bones; expected ${expectedBoneIds.length}.`);
}

const loadedClips = new Map(gltf.animations.map((clip) => [clip.name, clip]));
const motionChecks = [];
const mixer = new THREE.AnimationMixer(gltf.scene);
for (const clipSpec of clips) {
  const clip = loadedClips.get(clipSpec.name);
  if (!clip) continue;
  if (!near(clip.duration, Number(clipSpec.duration))) {
    fail(`Loaded animation ${clipSpec.name} duration drifted: graph=${clipSpec.duration}, loaded=${clip.duration}.`);
  }
  const trackMap = new Map(clip.tracks.map((track) => [track.name, track]));
  for (const trackSpec of clipSpec.tracks ?? []) {
    const property = trackSpec.property === 'quaternion' ? 'quaternion' : trackSpec.property;
    const trackName = `PCG_Bone_${safeId(trackSpec.bone)}.${property}`;
    const track = trackMap.get(trackName);
    if (!track) {
      fail(`Animation ${clipSpec.name} is missing track ${trackName}.`);
      continue;
    }
    if (track.times.length !== trackSpec.times.length ||
        trackSpec.times.some((time, index) => !near(track.times[index], Number(time)))) {
      fail(`Animation ${clipSpec.name} track ${trackName} key times drifted from the graph.`);
    }
  }

  const targets = (clipSpec.tracks ?? []).map((track) => ({
    object: gltf.scene.getObjectByName(`PCG_Bone_${safeId(track.bone)}`),
    property: track.property,
  })).filter((target) => target.object);
  const rest = new Map(targets.map(({ object }) => [object, {
    position: object.position.clone(),
    quaternion: object.quaternion.clone(),
    scale: object.scale.clone(),
  }]));
  const probeTime = Math.min(0.25, Number(clipSpec.duration) * 0.25);
  const times = [...new Set([
    probeTime,
    ...(clipSpec.tracks ?? []).flatMap((track) => track.times ?? []),
  ])]
    .map(Number)
    .filter((time) => time >= 0 && time < Number(clipSpec.duration) - 1e-6);
  let changedAt = null;
  for (const time of times) {
    mixer.stopAllAction();
    for (const [object, transform] of rest) {
      object.position.copy(transform.position);
      object.quaternion.copy(transform.quaternion);
      object.scale.copy(transform.scale);
    }
    mixer.clipAction(clip).reset().setLoop(THREE.LoopOnce, 1).play();
    mixer.setTime(time);
    const changed = targets.some(({ object, property }) => {
      const transform = rest.get(object);
      if (property === 'quaternion') return 1 - Math.abs(transform.quaternion.dot(object.quaternion)) > 1e-6;
      if (property === 'position') return transform.position.distanceTo(object.position) > 1e-6;
      return transform.scale.distanceTo(object.scale) > 1e-6;
    });
    if (changed) {
      changedAt = time;
      break;
    }
  }
  if (changedAt === null) fail(`Animation ${clipSpec.name} never changes an authored target at its key times.`);
  motionChecks.push({ name: clipSpec.name, duration: clip.duration, changedAt });
}

let sampledVertexCount = 0;
let nonFiniteSampleCount = 0;
let maxWeightError = 0;
for (const mesh of skinnedMeshes) {
  const weights = mesh.geometry.getAttribute('skinWeight');
  const positions = mesh.geometry.getAttribute('position');
  if (!weights || !positions) {
    fail(`${mesh.name} is missing skinWeight or position attributes.`);
    continue;
  }
  const step = Math.max(1, Math.floor(weights.count / 128));
  for (let vertex = 0; vertex < weights.count; vertex += step) {
    let sum = 0;
    for (let slot = 0; slot < 4; slot++) sum += weights.getComponent(vertex, slot);
    maxWeightError = Math.max(maxWeightError, Math.abs(1 - sum));
    const position = mesh.getVertexPosition(vertex, new THREE.Vector3());
    if (![position.x, position.y, position.z].every(Number.isFinite)) nonFiniteSampleCount++;
    sampledVertexCount++;
  }
}
if (nonFiniteSampleCount > 0) fail(`${nonFiniteSampleCount} sampled skinned vertices are non-finite.`);
if (maxWeightError > 1e-5) fail(`Maximum sampled skin-weight error ${maxWeightError} exceeds 1e-5.`);

const report = {
  ok: failures.length === 0,
  graph: resolve(graphPath),
  glb: resolve(glbPath),
  bytes: bytes.length,
  components: expectedComponentIds.length,
  bones: expectedBoneIds.length,
  skins: skeletons.length,
  skinnedMeshes: skinnedMeshes.length,
  animations: motionChecks,
  sampledVertexCount,
  nonFiniteSampleCount,
  maxWeightError,
  failures,
};
console.log(JSON.stringify(report, null, 2));
if (failures.length > 0) process.exit(1);
