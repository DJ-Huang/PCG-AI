import { fireEvent, render, screen } from '@testing-library/react';
import { useState } from 'react';
import { describe, expect, it, vi } from 'vitest';
import CameraCurveEditor from './CameraCurveEditor';
import { createDefaultShot, syncShotCameras, type ShotDocument } from './shot';
import { sampleShotCameraWorld } from './cameraPath';

describe('property curve editor', () => {
  it('authors and edits lens keys through the visible controls and updates deterministic playback', () => {
    let current = createDefaultShot();
    const onSeek = vi.fn();
    function Editor() {
      const [shot, setShot] = useState(current);
      return <CameraCurveEditor shot={shot} time={0} onSeek={onSeek} onShotChange={(next: ShotDocument) => {
        current = syncShotCameras(next); setShot(current);
      }} />;
    }
    render(<Editor />);
    fireEvent.click(screen.getByRole('button', { name: '+ Channel key' }));
    fireEvent.change(screen.getByLabelText('Keyframe channel value'), { target: { value: '24' } });
    expect(sampleShotCameraWorld(current, 'cam_a', 0).focalLengthMm).toBe(24);
    fireEvent.change(screen.getByLabelText('Keyframe frame'), { target: { value: '48' } });
    fireEvent.change(screen.getByLabelText('Keyframe channel value'), { target: { value: '72' } });
    fireEvent.change(screen.getByLabelText('Curve interpolation'), { target: { value: 'linear' } });
    expect(current.cameraKeyframes[0].timeSeconds).toBe(2);
    expect(sampleShotCameraWorld(current, 'cam_a', 1).focalLengthMm).toBe(49);
    expect(onSeek).toHaveBeenCalledWith(2);
    fireEvent.click(screen.getByRole('button', { name: 'Delete key' }));
    expect(current.cameraKeyframes).toHaveLength(0);
  });
});
