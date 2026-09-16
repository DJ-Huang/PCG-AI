import * as THREE from 'three';
import { sampleShotCameraWorld } from './cameraPath';
import { findCameraTransform, moveCameraRig, setCameraTransform } from './cameraTransform';
import { findKeyframeAtTime, upsertCameraKeyframe } from './cameraTrack';
import { mergeCameraCommand, type CameraCommand, type PhysicalCameraState } from './physicalCamera';
import type { ShotDocument } from './shot';

/** Convert a displayed world pose back to animation channels before the rig. */
export function cameraPoseBeforeRig(shot: ShotDocument, camera: PhysicalCameraState): CameraCommand {
  const rig = findCameraTransform(shot, shot.activeCameraId);
  if (!rig) return { position: camera.position, target: camera.target, up: camera.up };
  const inverse = new THREE.Quaternion().setFromEuler(new THREE.Euler(
    ...rig.rotationEulerDeg.map(THREE.MathUtils.degToRad) as [number, number, number], 'YXZ',
  )).invert();
  const position = new THREE.Vector3(...camera.position).sub(new THREE.Vector3(...rig.translation));
  const direction = new THREE.Vector3(...camera.target).sub(new THREE.Vector3(...camera.position)).applyQuaternion(inverse);
  return { position: position.toArray(), target: position.clone().add(direction).toArray(), up: new THREE.Vector3(...camera.up).applyQuaternion(inverse).toArray() };
}

export function editCameraAtTime(shot: ShotDocument, command: CameraCommand, time: number, autoKey: boolean): ShotDocument {
  const current = sampleShotCameraWorld(shot, shot.activeCameraId, time);
  const effective = mergeCameraCommand(current, command, shot.width / shot.height);
  const pose = command.position !== undefined || command.target !== undefined || command.up !== undefined;
  let next = shot;
  const key = findKeyframeAtTime(shot, time);
  if (pose) {
    next = setCameraTransform(shot, shot.activeCameraId, {});
    if (!autoKey) return moveCameraRig(next, shot.activeCameraId, current, effective);
  }
  if (autoKey || key) {
    return upsertCameraKeyframe(next, { id: key?.id, timeSeconds: time, interpolation: key?.interpolation ?? 'ease-in-out',
      value: pose ? { ...command, ...cameraPoseBeforeRig(next, effective) } : command });
  }
  return { ...shot, camera: mergeCameraCommand(shot.camera, command, shot.width / shot.height) };
}
