import { describe, expect, it, vi } from 'vitest';

import {
  PROCEDURAL_REFERENCE_VIEWS,
  PROCEDURAL_REFERENCE_PASSES,
  captureProceduralReferenceBundle,
} from './proceduralReference';
import {
  defaultPhysicalCamera,
  mergeCameraCommand,
  type CameraCommand,
  type PhysicalCameraState,
} from './physicalCamera';
import type { CaptureOptions, PreviewCapture } from './PreviewViewport';

function fakePngBase64(): string {
  const bytes = new Uint8Array(1200);
  bytes.set([137, 80, 78, 71, 13, 10, 26, 10]);
  let binary = '';
  for (const byte of bytes) binary += String.fromCharCode(byte);
  return btoa(binary);
}

function captureMetadata(state: PhysicalCameraState): PreviewCapture['metadata'] {
  return {
    camera: {
      position: [...state.position],
      target: [...state.target],
      up: [...state.up],
      fov: 27,
      near: state.near,
      far: state.far,
      projection: state.projection,
    },
    physicalCamera: state,
    viewport: { width: 1024, height: 1024, pixelRatio: 1 },
    shadingMode: 'rendered',
    solidLighting: 'studio',
    matcapId: 'clay_brown',
    wireframeOverlay: false,
    xrayEnabled: false,
    meshBandProfile: null,
  };
}

describe('captureProceduralReferenceBundle', () => {
  it('captures six views with one shared camera profile and queues image evidence', () => {
    const initial = {
      ...defaultPhysicalCamera(),
      position: [3, 2, 4] as [number, number, number],
      target: [0, 0.5, 0] as [number, number, number],
      focalLengthMm: 48,
      near: 0.02,
      far: 200,
      exposure: 1.25,
    };
    let current = initial;
    const commands: CameraCommand[] = [];
    const captures: CaptureOptions[] = [];
    const viewport = {
      getCameraState: vi.fn(() => current),
      applyCameraCommand: vi.fn((command: CameraCommand) => {
        commands.push(command);
        current = mergeCameraCommand(current, command);
        return current;
      }),
      captureFrame: vi.fn((options?: CaptureOptions) => {
        captures.push(options ?? {});
        current = mergeCameraCommand(current, options?.camera ?? {});
        return { pngBase64: fakePngBase64(), metadata: captureMetadata(current) };
      }),
    };

    const { request, manifest } = captureProceduralReferenceBundle(
      viewport,
      {
        nodeId: 'tripo-dog',
        bakedSurfaceNodeId: 'dog-sdf',
        bakedMaterialNodeId: 'dog-material',
        referencePath: 'library/web-cache/TripoCache/89c1f4938b0f9af67153cb16bda4c272c946d7a97d03f1714abf0cff115e9c4e.glb',
        sourceImage: 'pcg-resource://assets/uploads/dog.png',
        sourceImageFile: new File(['source'], 'tripo-source-reference.png', { type: 'image/png' }),
        graphPath: 'dog-reference.pcg',
      },
      { requestId: 'request-1', capturedAt: '2026-09-04T00:00:00.000Z' },
    );

    expect(captures).toHaveLength(PROCEDURAL_REFERENCE_VIEWS.length * PROCEDURAL_REFERENCE_PASSES.length);
    expect(captures.filter((capture) => capture.renderPass === 'beauty').map((capture) => capture.camera?.azimuth)).toEqual(
      PROCEDURAL_REFERENCE_VIEWS.map((view) => view.azimuth),
    );
    for (const capture of captures) {
      expect(capture).toMatchObject({
        width: 1024,
        height: 1024,
        dof: false,
        shadingMode: 'material',
      });
      expect(capture.camera).toMatchObject({
        target: initial.target,
        focalLengthMm: 48,
        near: 0.02,
        far: 200,
        exposure: 1.25,
      });
    }
    expect(commands.at(-1)).toMatchObject({ position: initial.position, target: initial.target });
    expect(captures.map((capture) => capture.renderPass).slice(0, PROCEDURAL_REFERENCE_PASSES.length)).toEqual(
      PROCEDURAL_REFERENCE_PASSES,
    );
    expect(request.files).toHaveLength(
      PROCEDURAL_REFERENCE_VIEWS.length * PROCEDURAL_REFERENCE_PASSES.length + 2,
    );
    expect(request.files.at(-1)?.name).toBe('tripo-reference-manifest.json');
    expect(request.files.at(-2)?.name).toBe('tripo-source-reference.png');
    expect(request.message).toContain('不得复制源 faces/indices');
    expect(request.message).toContain('dog-sdf');
    expect(request.message).toContain('dog-material');
    expect(request.message).toContain('silhouette IoU 目标 ≥ 0.98');
    expect(request.message).toContain('pcg_get_node_types');
    expect(request.message).toContain('FINAL_ACCEPTED');
    expect(manifest.reference.cacheKey).toHaveLength(64);
    expect(manifest.reference.contentSha256).toBeNull();
    expect(manifest.reference.bakedSurfaceNodeId).toBe('dog-sdf');
    expect(manifest.reference.bakedMaterialNodeId).toBe('dog-material');
    expect(manifest.reference.sourceImageFile).toBe('tripo-source-reference.png');
    expect(manifest.reference.meshBandProfile).toBeNull();
    expect(manifest.targetGraphPath).toBe('examples/tripo-89c1f4938b0f-procedural.pcg');
    expect(manifest.captureProfile.sharedCamera.distance).toBeCloseTo(5.220153, 5);
    expect(manifest.captureProfile.shadingMode).toBe('material');
    expect(manifest.captureProfile.passes).toEqual(PROCEDURAL_REFERENCE_PASSES);
    expect(manifest.views.every((view) => view.passes.length === PROCEDURAL_REFERENCE_PASSES.length)).toBe(true);
  });

  it('rejects an unreadable capture and restores the original camera', () => {
    const initial = defaultPhysicalCamera();
    const restore = vi.fn((_command: CameraCommand) => initial);
    const viewport = {
      getCameraState: () => initial,
      applyCameraCommand: restore,
      captureFrame: () => ({
        pngBase64: btoa('not a png'),
        metadata: captureMetadata(initial),
      }),
    };

    expect(() => captureProceduralReferenceBundle(viewport, {
      nodeId: 'tripo-bad',
      referencePath: 'bad.glb',
    })).toThrow('empty or unreadable');
    expect(restore).toHaveBeenCalledOnce();
    expect(restore.mock.calls[0][0]).toMatchObject({ position: initial.position });
  });
});
