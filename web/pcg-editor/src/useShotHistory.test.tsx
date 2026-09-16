import { act, renderHook } from '@testing-library/react';
import { describe, expect, it } from 'vitest';
import { createDefaultShot, selectShotCamera } from './shot';
import { addShotCamera } from './cameraGraph';
import { setCameraTransform } from './cameraTransform';
import { useShotHistory } from './useShotHistory';

describe('shot undo', () => {
  it('groups a complete gizmo drag into one step and keeps selection out of history', () => {
    const initial = addShotCamera(createDefaultShot());
    const { result } = renderHook(() => useShotHistory(initial));
    act(() => result.current.apply(selectShotCamera(result.current.shot, 'cam_a')));
    expect(result.current.canUndo).toBe(false);
    act(() => window.dispatchEvent(new Event('pointerdown')));
    for (let x = 1; x <= 4; x++) act(() => result.current.apply(setCameraTransform(result.current.shot, 'cam_a', { translation: [x, 0, 0] })));
    act(() => window.dispatchEvent(new Event('pointerup')));
    expect(result.current.shot.cameraTransforms[0].translation[0]).toBe(4);
    act(() => result.current.undo());
    expect(result.current.shot.cameraTransforms).toHaveLength(0);
    expect(result.current.canUndo).toBe(false);
    act(() => result.current.redo());
    expect(result.current.shot.cameraTransforms[0].translation[0]).toBe(4);
  });
});
