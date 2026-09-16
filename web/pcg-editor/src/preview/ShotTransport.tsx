import type { CSSProperties, PointerEvent } from 'react';

import {
  CAMERA_MOTION_PRESETS,
  type CameraMotionPreset,
} from '../cameraMotion';
import { previsFrameCount } from '../previsExport';
import type { ShotDocument, ShotInterpolation } from '../shot';
import { formatAnimationTime } from './animationTime';

interface ShotTransportProps {
  shot: ShotDocument;
  timeSeconds: number;
  playing: boolean;
  selectedKeyframeId: string | null;
  preset: CameraMotionPreset;
  codecLabel: string;
  exporting: boolean;
  exportProgress: number;
  exportStatus: string;
  exportHref?: string;
  exportFilename?: string;
  exportPath?: string;
  onPresetChange: (preset: CameraMotionPreset) => void;
  onApplyPreset: () => void;
  onTogglePlayback: () => void;
  onStop: () => void;
  onSeek: (timeSeconds: number) => void;
  onSelectKeyframe: (keyframeId: string | null) => void;
  onSetKeyframe: () => void;
  onDeleteKeyframe: () => void;
  onMoveKeyframe: (keyframeId: string, timeSeconds: number) => void;
  onKeyframeInterpolation: (interpolation: ShotInterpolation) => void;
  onSettingsChange: (
    patch: Partial<Pick<ShotDocument, 'durationSeconds' | 'fps' | 'width' | 'height'>>,
  ) => void;
  onExport: () => void;
  onCancelExport: () => void;
}

const PRESET_LABELS: Record<CameraMotionPreset, string> = {
  static: 'Static',
  'pan-tilt': 'Pan/Tilt',
  dolly: 'Dolly',
  truck: 'Truck',
  pedestal: 'Pedestal',
  crane: 'Crane',
  orbit: 'Orbit',
  handheld: 'Handheld',
};

const INTERPOLATION_LABELS: Record<ShotInterpolation, string> = {
  step: 'Hold / cut',
  linear: 'Linear',
  'ease-in': 'Ease in',
  'ease-out': 'Ease out',
  'ease-in-out': 'Ease in-out',
};

export function shotFrameIndex(timeSeconds: number, fps: number, frameCount: number): number {
  if (frameCount <= 1) return 0;
  return Math.min(Math.max(Math.floor(timeSeconds * fps + 1e-9), 0), frameCount - 1);
}

export default function ShotTransport({
  shot,
  timeSeconds,
  playing,
  selectedKeyframeId,
  preset,
  codecLabel,
  exporting,
  exportProgress,
  exportStatus,
  exportHref,
  exportFilename,
  exportPath,
  onPresetChange,
  onApplyPreset,
  onTogglePlayback,
  onStop,
  onSeek,
  onSelectKeyframe,
  onSetKeyframe,
  onDeleteKeyframe,
  onMoveKeyframe,
  onKeyframeInterpolation,
  onSettingsChange,
  onExport,
  onCancelExport,
}: ShotTransportProps) {
  const frameCount = previsFrameCount(shot);
  const frame = shotFrameIndex(timeSeconds, shot.fps, frameCount);
  const progress = shot.durationSeconds > 0
    ? (Math.min(timeSeconds, shot.durationSeconds) / shot.durationSeconds) * 100
    : 0;
  const timelineStyle = {
    '--pcg-animation-progress': `${Math.max(0, Math.min(100, progress))}%`,
  } as CSSProperties;
  const selected = shot.cameraKeyframes.find((keyframe) => keyframe.id === selectedKeyframeId);

  const dragKeyframe = (keyframeId: string, event: PointerEvent<HTMLButtonElement>) => {
    const track = event.currentTarget.parentElement;
    if (!track) return;
    event.preventDefault();
    event.stopPropagation();
    onSelectKeyframe(keyframeId);
    const bounds = track.getBoundingClientRect();
    const move = (clientX: number) => {
      const t = (clientX - bounds.left) / Math.max(bounds.width, 1);
      onMoveKeyframe(keyframeId, t * shot.durationSeconds);
    };
    move(event.clientX);
    const onMove = (next: globalThis.PointerEvent) => move(next.clientX);
    const onUp = () => {
      window.removeEventListener('pointermove', onMove);
      window.removeEventListener('pointerup', onUp);
    };
    window.addEventListener('pointermove', onMove);
    window.addEventListener('pointerup', onUp);
  };

  return (
    <section className="pcg-animation pcg-shot" aria-label="Shot previs">
      <div className="pcg-animation__header">
        <span className="pcg-animation__mode">Shot</span>
        <span className="pcg-shot__name" title={shot.name}>{shot.name}</span>
        <span className="pcg-animation__time" aria-label="Shot time">
          {formatAnimationTime(timeSeconds)} <span>/</span> {formatAnimationTime(shot.durationSeconds)}
          <span className="pcg-shot__frames">{frame + 1} / {frameCount}</span>
        </span>
      </div>

      <div className="pcg-animation__transport">
        <button type="button" aria-label="Previous frame" disabled={exporting} onClick={() => onSeek(Math.max(0, timeSeconds - 1 / shot.fps))}>|‹</button>
        <button type="button" aria-label="Next frame" disabled={exporting} onClick={() => onSeek(Math.min(shot.durationSeconds, timeSeconds + 1 / shot.fps))}>›|</button>
        <button
          type="button"
          className="pcg-animation__transport-button pcg-animation__transport-button--primary"
          aria-label={playing ? 'Pause shot' : 'Play shot'}
          title={playing ? 'Pause shot' : 'Play shot'}
          onClick={onTogglePlayback}
          disabled={exporting}
        >
          {playing ? (
            <svg viewBox="0 0 16 16" aria-hidden><path d="M4 3h3v10H4zm5 0h3v10H9z" /></svg>
          ) : (
            <svg viewBox="0 0 16 16" aria-hidden><path d="M4 2.5 13 8l-9 5.5z" /></svg>
          )}
        </button>
        <button
          type="button"
          className="pcg-animation__transport-button"
          aria-label="Stop shot"
          title="Stop and return to the first frame"
          onClick={onStop}
          disabled={exporting}
        >
          <svg viewBox="0 0 16 16" aria-hidden><rect x="4" y="4" width="8" height="8" rx="1" /></svg>
        </button>

        <label className="pcg-animation__speed">
          <span>Move</span>
          <select
            aria-label="Camera motion preset"
            value={preset}
            onChange={(event) => onPresetChange(event.target.value as CameraMotionPreset)}
            disabled={exporting}
          >
            {CAMERA_MOTION_PRESETS.map((name) => (
              <option key={name} value={name}>{PRESET_LABELS[name]}</option>
            ))}
          </select>
        </label>
        <button
          type="button"
          className="pcg-shot__apply"
          onClick={onApplyPreset}
          disabled={exporting}
        >
          Apply keys
        </button>
        <button
          type="button"
          className="pcg-shot__apply"
          onClick={onSetKeyframe}
          disabled={exporting}
        >
          Set key
        </button>
        <button
          type="button"
          className="pcg-shot__apply"
          onClick={onDeleteKeyframe}
          disabled={exporting || !selected}
        >
          Delete key
        </button>
        {selected && (
          <label className="pcg-animation__speed">
            <span>Ease</span>
            <select
              aria-label="Keyframe interpolation"
              value={selected.interpolation}
              onChange={(event) => onKeyframeInterpolation(event.target.value as ShotInterpolation)}
              disabled={exporting}
            >
              {Object.entries(INTERPOLATION_LABELS).map(([value, label]) => (
                <option key={value} value={value}>{label}</option>
              ))}
            </select>
          </label>
        )}
      </div>

      <label className="pcg-animation__timeline pcg-shot__timeline" style={timelineStyle}>
        <span className="pcg-animation__timeline-track" aria-hidden>
          <span className="pcg-animation__timeline-fill" />
        </span>
        {shot.cameraKeyframes.map((keyframe) => (
          <button
            key={keyframe.id}
            type="button"
            className={`pcg-shot__key${keyframe.id === selectedKeyframeId ? ' is-selected' : ''}`}
            aria-label={`Camera keyframe at ${keyframe.timeSeconds.toFixed(2)} seconds`}
            aria-pressed={keyframe.id === selectedKeyframeId}
            style={{ left: `${(keyframe.timeSeconds / Math.max(shot.durationSeconds, 0.001)) * 100}%` }}
            disabled={exporting}
            onPointerDown={(event) => dragKeyframe(keyframe.id, event)}
            onClick={(event) => {
              event.preventDefault();
              onSelectKeyframe(keyframe.id);
              onSeek(keyframe.timeSeconds);
            }}
          />
        ))}
        <input
          type="range"
          aria-label="Shot timeline"
          min={0}
          max={shot.durationSeconds}
          step={1 / shot.fps}
          value={Math.min(timeSeconds, shot.durationSeconds)}
          onChange={(event) => onSeek(Number(event.target.value))}
          disabled={exporting}
        />
      </label>

      <div className="pcg-shot__settings">
        <label><span>Frame</span><input type="number" aria-label="Shot playhead frame" min={0} max={Math.round(shot.durationSeconds * shot.fps)}
          value={Math.round(timeSeconds * shot.fps)} onChange={(event) => onSeek(Number(event.target.value) / shot.fps)} disabled={exporting} /></label>
        <label>
          <span>Sec</span>
          <input
            type="number"
            min={0.1}
            max={3600}
            step={0.1}
            value={shot.durationSeconds}
            aria-label="Shot duration seconds"
            onChange={(event) => onSettingsChange({ durationSeconds: Number(event.target.value) })}
            disabled={exporting}
          />
        </label>
        <label>
          <span>FPS</span>
          <input
            type="number"
            min={1}
            max={120}
            step={1}
            value={shot.fps}
            aria-label="Shot frame rate"
            onChange={(event) => onSettingsChange({ fps: Number(event.target.value) })}
            disabled={exporting}
          />
        </label>
        <label>
          <span>W</span>
          <input
            type="number"
            min={16}
            max={8192}
            step={1}
            value={shot.width}
            aria-label="Export width"
            onChange={(event) => onSettingsChange({ width: Number(event.target.value) })}
            disabled={exporting}
          />
        </label>
        <label>
          <span>H</span>
          <input
            type="number"
            min={16}
            max={8192}
            step={1}
            value={shot.height}
            aria-label="Export height"
            onChange={(event) => onSettingsChange({ height: Number(event.target.value) })}
            disabled={exporting}
          />
        </label>
        {exporting ? (
          <button type="button" className="pcg-shot__export is-busy" onClick={onCancelExport}>
            Cancel {Math.round(exportProgress * 100)}%
          </button>
        ) : (
          <button
            type="button"
            className="pcg-shot__export"
            onClick={onExport}
            disabled={!codecLabel}
          >
            Export {codecLabel || 'video'}
          </button>
        )}
      </div>
      {exportStatus && <p className="pcg-shot__status">{exportStatus}</p>}
      {exportHref && <p className="pcg-shot__status" title={exportPath}>
        Last export: {' '}
        <a href={exportHref} download={exportFilename}>Download video</a>{' · '}
        <a href={exportHref} target="_blank" rel="noreferrer">Watch video</a>
      </p>}
    </section>
  );
}
