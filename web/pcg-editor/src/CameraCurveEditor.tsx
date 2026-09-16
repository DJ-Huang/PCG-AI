import { useMemo, useRef, useState, type PointerEvent } from 'react';
import { sampleCameraTrack, samplePathProgress } from './cameraMotion';
import { editableCameraKeyframes, editableMotionCurve, moveCameraChannelKey, patchKeyframeValue, removeCameraChannelKey,
  setKeyframeInterpolation, snapShotTime, upsertCameraKeyframe } from './cameraTrack';
import { mergeCameraCommand, type CameraCommand } from './physicalCamera';
import type { ShotDocument, ShotInterpolation } from './shot';

const CAMERA_CHANNELS = [
  ...[['position', 'Position'], ['target', 'Look at'], ['up', 'Up vector']].flatMap(([field, label]) => ['X', 'Y', 'Z'].map((axis, i) => ({ id: `${field}.${i}`, label: `${label} ${axis}` }))),
  ...[['focalLengthMm', 'Focal length (mm)'], ['focusDistance', 'Focus distance (m)'], ['apertureFstop', 'Aperture (f-stop)'],
    ['exposure', 'Exposure'], ['shiftX', 'Lens shift X'], ['shiftY', 'Lens shift Y'], ['sensorWidthMm', 'Sensor width'],
    ['sensorHeightMm', 'Sensor height'], ['near', 'Near clip'], ['far', 'Far clip'], ['orthographicScale', 'Orthographic scale'],
    ['apertureBlades', 'Aperture blades'], ['apertureRotationDeg', 'Aperture rotation'], ['apertureRatio', 'Aperture ratio'],
    ['pathProgress', 'Path progress (0–1)']].map(([id, label]) => ({ id, label })),
];

function channelValue(shot: ShotDocument, channel: string, time: number): number {
  const keys = editableCameraKeyframes(shot);
  if (channel === 'pathProgress') return samplePathProgress(keys, time, shot.durationSeconds);
  const state = sampleCameraTrack(shot.camera, keys, time, shot.width / shot.height);
  const [field, axis] = channel.split('.');
  const value = state[field as keyof typeof state];
  return typeof value === 'number' ? value : Array.isArray(value) ? value[Number(axis)] : 0;
}

function channelCommand(shot: ShotDocument, channel: string, time: number, value: number): CameraCommand {
  if (!Number.isFinite(value)) return {};
  if (channel === 'pathProgress') return { pathProgress: Math.min(1, Math.max(0, value)) };
  const [field, axis] = channel.split('.');
  const sampled = sampleCameraTrack(shot.camera, editableCameraKeyframes(shot), time, shot.width / shot.height);
  if (axis !== undefined && ['position', 'target', 'up'].includes(field)) {
    const vector: [number, number, number] = [...sampled[field as 'position' | 'target' | 'up']];
    vector[Number(axis)] = value;
    return { [field]: vector };
  }
  const clamped = mergeCameraCommand(sampled, { [field]: value });
  return { [field]: clamped[field as keyof typeof clamped] };
}

interface Props { shot: ShotDocument; time: number; onShotChange: (shot: ShotDocument) => void; onSeek: (time: number) => void }

export default function CameraCurveEditor({ shot, time, onShotChange, onSeek }: Props) {
  const [channel, setChannel] = useState('focalLengthMm');
  const [selectedId, setSelectedId] = useState<string | null>(null);
  const live = useRef(shot);
  live.current = shot;
  const curve = editableMotionCurve(shot);
  const keys = editableCameraKeyframes(shot);
  const field = channel.split('.')[0] as keyof CameraCommand;
  const channelKeys = keys.filter((key) => key.value[field as keyof CameraCommand] !== undefined);
  const selected = channelKeys.find((key) => key.id === selectedId);
  const bounds = useMemo(() => {
    const values = Array.from({ length: 61 }, (_, i) => channelValue(shot, channel, shot.durationSeconds * i / 60));
    const min = Math.min(...values), max = Math.max(...values);
    const pad = Math.max((max - min) * 0.18, Math.abs(max) * 0.05, 0.5);
    return { min: min - pad, max: max + pad, values };
  }, [shot, channel]);
  const x = (t: number) => 50 + t / shot.durationSeconds * 900;
  const y = (v: number) => 158 - (v - bounds.min) / (bounds.max - bounds.min) * 136;
  const drag = useRef<{ id: string; min: number; max: number; left: number; top: number; width: number; height: number } | null>(null);
  const startDrag = (id: string, event: PointerEvent<SVGCircleElement>) => {
    event.preventDefault();
    const svg = event.currentTarget.ownerSVGElement!;
    const rect = svg.getBoundingClientRect();
    drag.current = { id, min: bounds.min, max: bounds.max, left: rect.left, top: rect.top, width: rect.width, height: rect.height };
    svg.setPointerCapture(event.pointerId);
    setSelectedId(id);
    const key = keys.find((entry) => entry.id === id);
    if (key) onSeek(key.timeSeconds);
  };
  const changeValue = (value: number) => {
    if (!selected) return;
    onShotChange(patchKeyframeValue(shot, selected.id, channelCommand(shot, channel, selected.timeSeconds, value)));
  };
  return <section className="picg-curve-editor" aria-label="Camera animation curves">
    <div className="picg-curve-editor__toolbar">
      <strong>{curve ? curve.name : 'Camera'} · Animation</strong>
      <select aria-label="Animation channel" value={channel} onChange={(event) => setChannel(event.target.value)}>
        {CAMERA_CHANNELS.map((item) => <option key={item.id} value={item.id}>{item.label}</option>)}
      </select>
      <button onClick={() => {
        const next = upsertCameraKeyframe(shot, { timeSeconds: time, interpolation: 'ease-in-out', value: channelCommand(shot, channel, time, channelValue(shot, channel, time)) });
        onShotChange(next);
        setSelectedId(editableCameraKeyframes(next).find((key) => Math.abs(key.timeSeconds - time) < 0.5 / shot.fps)?.id ?? null);
      }}>+ Channel key</button>
      <button disabled={!selected} title="Delete this property key; preserve other properties at this frame" onClick={() => { if (selected) onShotChange(removeCameraChannelKey(shot, selected.id, field)); }}>Delete key</button>
    </div>
    <svg viewBox="0 0 1000 184" preserveAspectRatio="none" role="img" aria-label={`${channel} animation curve`} onPointerMove={(event) => {
      const gesture = drag.current;
      if (!gesture) return;
      const doc = live.current;
      const t = snapShotTime(doc, (((event.clientX - gesture.left) / gesture.width * 1000 - 50) / 900) * doc.durationSeconds);
      const value = gesture.min + (158 - (event.clientY - gesture.top) / gesture.height * 184) / 136 * (gesture.max - gesture.min);
      const result = moveCameraChannelKey(doc, gesture.id, field, t);
      gesture.id = result.keyframeId; setSelectedId(result.keyframeId);
      const moved = result.shot;
      const actualTime = editableCameraKeyframes(moved).find((key) => key.id === gesture.id)?.timeSeconds ?? t;
      onShotChange(patchKeyframeValue(moved, gesture.id, channelCommand(moved, channel, actualTime, value)));
      onSeek(actualTime);
    }} onPointerUp={(event) => { drag.current = null; if (event.currentTarget.hasPointerCapture(event.pointerId)) event.currentTarget.releasePointerCapture(event.pointerId); }}
    onPointerCancel={() => { drag.current = null; }}>
      {[0, 0.25, 0.5, 0.75, 1].map((ratio) => <g key={ratio}>
        <line x1="50" x2="950" y1={22 + ratio * 136} y2={22 + ratio * 136} className="picg-curve-grid" />
        <text x="3" y={26 + ratio * 136}>{(bounds.max - ratio * (bounds.max - bounds.min)).toFixed(1)}</text>
        <line x1={50 + ratio * 900} x2={50 + ratio * 900} y1="22" y2="158" className="picg-curve-grid" />
        <text x={50 + ratio * 900} y="177">{Math.round(shot.durationSeconds * shot.fps * ratio)}f</text>
      </g>)}
      <path d={bounds.values.map((v, i) => `${i ? 'L' : 'M'}${x(shot.durationSeconds * i / 60)},${y(v)}`).join(' ')} className="picg-curve-line" />
      <line x1={x(time)} x2={x(time)} y1="10" y2="164" className="picg-curve-playhead" />
      {channelKeys.map((key) => <circle key={key.id} cx={x(key.timeSeconds)} cy={y(channelValue(shot, channel, key.timeSeconds))} r="6"
        className={`picg-curve-key${key.id === selectedId ? ' is-selected' : ''}`} tabIndex={0} role="button"
        aria-label={`Edit ${channel} key at frame ${Math.round(key.timeSeconds * shot.fps)}`}
        onPointerDown={(event) => startDrag(key.id, event)} onKeyDown={(event) => {
          if (event.key === 'Enter') { setSelectedId(key.id); onSeek(key.timeSeconds); }
          if (event.key === 'Delete' || event.key === 'Backspace') { event.stopPropagation(); onShotChange(removeCameraChannelKey(shot, key.id, field)); }
        }} />)}
    </svg>
    <div className="picg-curve-editor__toolbar">
      {selected ? <>
        <label>Frame <input type="number" aria-label="Keyframe frame" value={Math.round(selected.timeSeconds * shot.fps)} min={0} max={Math.round(shot.durationSeconds * shot.fps)}
          onChange={(event) => {
            const result = moveCameraChannelKey(shot, selected.id, field, Number(event.target.value) / shot.fps);
            onShotChange(result.shot); setSelectedId(result.keyframeId);
            onSeek(editableCameraKeyframes(result.shot).find((key) => key.id === result.keyframeId)?.timeSeconds ?? time);
          }} /></label>
        <label>Value <input type="number" aria-label="Keyframe channel value" step="0.1" value={Number(channelValue(shot, channel, selected.timeSeconds).toFixed(4))}
          onChange={(event) => changeValue(Number(event.target.value))} /></label>
        <select aria-label="Curve interpolation" value={selected.interpolation} onChange={(event) => onShotChange(setKeyframeInterpolation(shot, selected.id, event.target.value as ShotInterpolation))}>
          {['linear', 'ease-in', 'ease-out', 'ease-in-out', 'step'].map((value) => <option key={value}>{value}</option>)}
        </select>
      </> : <span>选择通道，添加关键帧；拖动点调整时间和数值。关键帧自动对齐帧。</span>}
    </div>
  </section>;
}
