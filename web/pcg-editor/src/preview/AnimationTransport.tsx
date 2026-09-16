import type { CSSProperties } from 'react';

import type {
  ActionAnimationClipInfo,
  ActionPlaybackState,
} from '../actionRuntime';
import { formatAnimationTime } from './animationTime';

interface AnimationTransportProps {
  clips: readonly ActionAnimationClipInfo[];
  selectedClip: string;
  playback: ActionPlaybackState;
  onSelectClip: (name: string) => void;
  onTogglePlayback: () => void;
  onStop: () => void;
  onSeek: (timeSeconds: number) => void;
  onSpeedChange: (speed: number) => void;
  onLoopChange: (loop: boolean) => void;
  onClose: () => void;
}

const SPEED_PRESETS = [-2, -1, -0.5, 0.25, 0.5, 1, 1.5, 2];

export default function AnimationTransport({
  clips,
  selectedClip,
  playback,
  onSelectClip,
  onTogglePlayback,
  onStop,
  onSeek,
  onSpeedChange,
  onLoopChange,
  onClose,
}: AnimationTransportProps) {
  const clip = clips.find((candidate) => candidate.name === selectedClip) ?? clips[0];
  if (!clip) return null;

  const isActiveClip = playback.clipName === clip.name;
  const currentTime = isActiveClip ? Math.min(playback.currentTime, clip.duration) : 0;
  const loop = isActiveClip ? playback.loop : clip.loop;
  const loopSource = isActiveClip ? playback.loopSource : clip.loopSource;
  const progress = clip.duration > 0 ? (currentTime / clip.duration) * 100 : 0;
  const speedOptions = SPEED_PRESETS.includes(playback.speed)
    ? SPEED_PRESETS
    : [...SPEED_PRESETS, playback.speed].sort((a, b) => a - b);
  const timelineStyle = {
    '--pcg-animation-progress': `${Math.max(0, Math.min(100, progress))}%`,
  } as CSSProperties;

  return (
    <section className="pcg-animation" aria-label="Animation mode">
      <div className="pcg-animation__header">
        <span className="pcg-animation__mode">Animation</span>
        <select
          className="pcg-animation__clip"
          aria-label="Animation clip"
          value={clip.name}
          onChange={(event) => onSelectClip(event.target.value)}
        >
          {clips.map((candidate) => (
            <option key={candidate.name} value={candidate.name}>{candidate.name}</option>
          ))}
        </select>
        <button
          type="button"
          className="pcg-animation__close"
          aria-label="Close animation mode"
          title="Close animation mode"
          onClick={onClose}
        >
          ×
        </button>
      </div>

      <div className="pcg-animation__transport">
        <button
          type="button"
          className="pcg-animation__transport-button pcg-animation__transport-button--primary"
          aria-label={playback.playing && isActiveClip ? 'Pause animation' : 'Play animation'}
          title={playback.playing && isActiveClip ? 'Pause (Space)' : 'Play (Space)'}
          onClick={onTogglePlayback}
        >
          {playback.playing && isActiveClip ? (
            <svg viewBox="0 0 16 16" aria-hidden><path d="M4 3h3v10H4zm5 0h3v10H9z" /></svg>
          ) : (
            <svg viewBox="0 0 16 16" aria-hidden><path d="M4 2.5 13 8l-9 5.5z" /></svg>
          )}
        </button>
        <button
          type="button"
          className="pcg-animation__transport-button"
          aria-label="Stop animation"
          title="Stop and restore bind pose"
          onClick={onStop}
        >
          <svg viewBox="0 0 16 16" aria-hidden><rect x="4" y="4" width="8" height="8" rx="1" /></svg>
        </button>

        <span className="pcg-animation__time" aria-label="Animation time">
          {formatAnimationTime(currentTime)} <span>/</span> {formatAnimationTime(clip.duration)}
        </span>

        <label className="pcg-animation__speed">
          <span>Speed</span>
          <select
            aria-label="Animation speed"
            value={playback.speed}
            onChange={(event) => onSpeedChange(Number(event.target.value))}
          >
            {speedOptions.map((speed) => (
              <option key={speed} value={speed}>{speed}×</option>
            ))}
          </select>
        </label>

        <button
          type="button"
          className={`pcg-animation__loop${loop ? ' is-active' : ''}`}
          aria-label="Loop animation"
          aria-pressed={loop}
          title={loopSource === 'unmeasured'
            ? 'Loop current clip — source loop was not measured, so playback defaults to once'
            : loopSource === 'previewOverride'
              ? 'Loop current clip — preview override'
              : 'Loop current clip — authored default'}
          onClick={() => onLoopChange(!loop)}
        >
          ↻ <span>Loop</span>
        </button>
      </div>

      <label className="pcg-animation__timeline" style={timelineStyle}>
        <span className="pcg-animation__timeline-track" aria-hidden>
          <span className="pcg-animation__timeline-fill" />
        </span>
        <input
          type="range"
          aria-label="Animation timeline"
          min={0}
          max={clip.duration}
          step={Math.max(clip.duration / 600, 0.001)}
          value={currentTime}
          onChange={(event) => onSeek(Number(event.target.value))}
        />
      </label>
    </section>
  );
}
