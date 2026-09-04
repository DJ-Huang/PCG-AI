import { fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';

import AnimationTransport from './AnimationTransport';
import { formatAnimationTime } from './animationTime';

const clips = [
  { name: 'idle', duration: 2, loop: true, loopSource: 'authored' as const },
  { name: 'wave', duration: 1.25, loop: false, loopSource: 'authored' as const },
];

describe('AnimationTransport', () => {
  it('formats animation time without leaking invalid values', () => {
    expect(formatAnimationTime(0)).toBe('0:00.00');
    expect(formatAnimationTime(62.345)).toBe('1:02.34');
    expect(formatAnimationTime(Number.NaN)).toBe('0:00.00');
  });

  it('exposes clip, transport, timeline, speed, and loop controls', () => {
    const onSelectClip = vi.fn();
    const onTogglePlayback = vi.fn();
    const onStop = vi.fn();
    const onSeek = vi.fn();
    const onSpeedChange = vi.fn();
    const onLoopChange = vi.fn();
    render(
      <AnimationTransport
        clips={clips}
        selectedClip="idle"
        playback={{
          clipName: 'idle', currentTime: 0.5, duration: 2,
          playing: true, paused: false, loop: true, loopSource: 'authored', speed: 1,
        }}
        onSelectClip={onSelectClip}
        onTogglePlayback={onTogglePlayback}
        onStop={onStop}
        onSeek={onSeek}
        onSpeedChange={onSpeedChange}
        onLoopChange={onLoopChange}
        onClose={vi.fn()}
      />,
    );

    expect(screen.getByRole('button', { name: 'Pause animation' })).toBeTruthy();
    fireEvent.change(screen.getByLabelText('Animation clip'), { target: { value: 'wave' } });
    fireEvent.change(screen.getByLabelText('Animation timeline'), { target: { value: '1.1' } });
    fireEvent.change(screen.getByLabelText('Animation speed'), { target: { value: '1.5' } });
    fireEvent.click(screen.getByRole('button', { name: 'Loop animation' }));
    fireEvent.click(screen.getByRole('button', { name: 'Stop animation' }));

    expect(onSelectClip).toHaveBeenCalledWith('wave');
    expect(onSeek).toHaveBeenCalledWith(1.1);
    expect(onSpeedChange).toHaveBeenCalledWith(1.5);
    expect(onLoopChange).toHaveBeenCalledWith(false);
    expect(onStop).toHaveBeenCalledOnce();
  });
});
