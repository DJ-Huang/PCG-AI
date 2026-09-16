import { describe, expect, it } from 'vitest';
import * as THREE from 'three';

import {
  computeReviewCameraPose,
  objectFrame,
  viewOffsetAndUp,
} from './reviewCamera';

function boxPositions(min: THREE.Vector3, max: THREE.Vector3): Float32Array {
  const corners = [
    [min.x, min.y, min.z],
    [max.x, min.y, min.z],
    [min.x, max.y, min.z],
    [max.x, max.y, min.z],
    [min.x, min.y, max.z],
    [max.x, min.y, max.z],
    [min.x, max.y, max.z],
    [max.x, max.y, max.z],
  ];
  return new Float32Array(corners.flat());
}

describe('reviewCamera', () => {
  it('maps Unity +Z front to the three.js -Z camera offset', () => {
    const { front, right } = objectFrame('+z', 'right');
    expect(front.x).toBeCloseTo(0);
    expect(front.y).toBeCloseTo(0);
    expect(front.z).toBeCloseTo(-1);
    expect(right.x).toBeCloseTo(1);
    expect(right.z).toBeCloseTo(0);
  });

  it('places front/side/top cameras on the matching object-space axes', () => {
    const positions = boxPositions(new THREE.Vector3(-1, 0, -0.5), new THREE.Vector3(1, 2, 0.5));
    const front = computeReviewCameraPose(positions, { view: 'front', frontAxis: '+z' }, 1);
    const side = computeReviewCameraPose(positions, { view: 'side', frontAxis: '+z', sideView: 'right' }, 1);
    const top = computeReviewCameraPose(positions, { view: 'top', frontAxis: '+z' }, 1);
    expect(front?.projection).toBe('orthographic');
    expect(side?.projection).toBe('orthographic');
    expect(top?.projection).toBe('orthographic');

    const frontOffset = new THREE.Vector3().fromArray(front!.position).sub(new THREE.Vector3().fromArray(front!.target));
    const sideOffset = new THREE.Vector3().fromArray(side!.position).sub(new THREE.Vector3().fromArray(side!.target));
    const topOffset = new THREE.Vector3().fromArray(top!.position).sub(new THREE.Vector3().fromArray(top!.target));
    expect(frontOffset.z).toBeLessThan(0);
    expect(Math.abs(frontOffset.x)).toBeLessThan(1e-6);
    expect(sideOffset.x).toBeGreaterThan(0);
    expect(Math.abs(sideOffset.z)).toBeLessThan(1e-6);
    expect(topOffset.y).toBeGreaterThan(0);
    expect(top!.up[2]).toBeGreaterThan(0);
  });

  it('uses an orthographic frustum large enough to contain the fitted AABB', () => {
    const positions = boxPositions(new THREE.Vector3(-2, 0, -0.25), new THREE.Vector3(2, 1, 0.25));
    const pose = computeReviewCameraPose(positions, { view: 'front', frontAxis: '+z', margin: 1 }, 1);
    expect(pose).not.toBeNull();
    expect(pose!.frustumHeight).toBeGreaterThanOrEqual(2);
  });

  it('keeps three-quarter as perspective', () => {
    const { offset } = viewOffsetAndUp('three-quarter', '+z', 'right');
    const positions = boxPositions(new THREE.Vector3(-1, 0, -1), new THREE.Vector3(1, 1, 1));
    const pose = computeReviewCameraPose(positions, { view: 'three-quarter' }, 16 / 9);
    expect(offset.length()).toBeGreaterThan(0.9);
    expect(pose?.projection).toBe('perspective');
  });
});
