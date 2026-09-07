import { fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';

import { createDefaultShot } from '../shot';
import ShotTransport, { shotFrameIndex } from './ShotTransport';

describe('ShotTransport', () => {
  it('maps continuous time onto the last integer frame of a 5s 24fps shot', () => {
    expect(shotFrameIndex(0, 24, 120)).toBe(0);
    expect(shotFrameIndex(119 / 24, 24, 120)).toBe(119);
    expect(shotFrameIndex(5, 24, 120)).toBe(119);
  });

  it('exposes playback, preset, timeline, and export controls', () => {
    const onTogglePlayback = vi.fn();
    const onStop = vi.fn();
    const onSeek = vi.fn();
    const onApplyPreset = vi.fn();
    const onExport = vi.fn();
    const onSettingsChange = vi.fn();
    const onSetKeyframe = vi.fn();
    render(
      <ShotTransport
        shot={createDefaultShot('Crosswalk')}
        timeSeconds={1}
        playing={false}
        selectedKeyframeId={null}
        preset="dolly"
        codecLabel="H.264 MP4"
        exporting={false}
        exportProgress={0}
        exportStatus=""
        onPresetChange={vi.fn()}
        onApplyPreset={onApplyPreset}
        onTogglePlayback={onTogglePlayback}
        onStop={onStop}
        onSeek={onSeek}
        onSelectKeyframe={vi.fn()}
        onSetKeyframe={onSetKeyframe}
        onDeleteKeyframe={vi.fn()}
        onMoveKeyframe={vi.fn()}
        onKeyframeInterpolation={vi.fn()}
        onSettingsChange={onSettingsChange}
        onExport={onExport}
        onCancelExport={vi.fn()}
      />,
    );

    expect(screen.getByText((_, node) => node?.classList.contains('pcg-shot__frames') === true && (node.textContent ?? '').replace(/\s+/g, '') === '25/120')).toBeTruthy();
    fireEvent.click(screen.getByRole('button', { name: 'Play shot' }));
    fireEvent.click(screen.getByRole('button', { name: 'Stop shot' }));
    fireEvent.click(screen.getByRole('button', { name: 'Set key' }));
    fireEvent.change(screen.getByLabelText('Shot timeline'), { target: { value: '2.5' } });
    fireEvent.change(screen.getByLabelText('Shot duration seconds'), { target: { value: '8' } });
    fireEvent.click(screen.getByRole('button', { name: 'Apply keys' }));
    fireEvent.click(screen.getByRole('button', { name: 'Export H.264 MP4' }));

    expect(onTogglePlayback).toHaveBeenCalledOnce();
    expect(onStop).toHaveBeenCalledOnce();
    expect(onSetKeyframe).toHaveBeenCalledOnce();
    expect(onSeek).toHaveBeenCalledWith(2.5);
    expect(onSettingsChange).toHaveBeenCalledWith({ durationSeconds: 8 });
    expect(onApplyPreset).toHaveBeenCalledOnce();
    expect(onExport).toHaveBeenCalledOnce();
  });
});
