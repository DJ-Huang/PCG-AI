import { useState } from 'react';

import {
  CAMERA_LIMITS,
  type CameraCommand,
  type PhysicalCameraState,
} from '../physicalCamera';
import type { CaptureOptions } from '../PreviewViewport';

interface CameraPopoverProps {
  state: PhysicalCameraState;
  onCameraChange: (command: CameraCommand) => void;
  onSnapshot: (options: CaptureOptions) => string | null;
}

const RESOLUTION_PRESETS = [
  { label: 'Viewport', width: 0, height: 0 },
  { label: '1024²', width: 1024, height: 1024 },
  { label: '2048²', width: 2048, height: 2048 },
  { label: '1920×1080', width: 1920, height: 1080 },
  { label: '4096²', width: 4096, height: 4096 },
] as const;

export default function CameraPopover({
  state,
  onCameraChange,
  onSnapshot,
}: CameraPopoverProps) {
  const [resolutionIndex, setResolutionIndex] = useState(0);
  const [transparent, setTransparent] = useState(false);

  const snapshot = () => {
    const preset = RESOLUTION_PRESETS[resolutionIndex];
    const options: CaptureOptions = { transparent };
    if (preset.width > 0) {
      options.width = preset.width;
      options.height = preset.height;
    }
    const dataUrl = onSnapshot(options);
    if (!dataUrl) return;
    const anchor = document.createElement('a');
    anchor.href = dataUrl;
    anchor.download = `pcg-camera-${Date.now()}.png`;
    anchor.click();
  };

  return (
    <div
      className="pcg-preview__shading-popover pcg-preview__camera-popover"
      role="dialog"
      aria-label="Camera"
    >
      <div className="pcg-preview__shading-popover-title">Physical Camera</div>

      <div className="pcg-preview__shading-section-label">Lens</div>
      <label className="pcg-preview__camera-row">
        <span>Focal</span>
        <input
          type="range"
          min={CAMERA_LIMITS.focalLengthMm.min}
          max={200}
          step={1}
          value={Math.min(state.focalLengthMm, 200)}
          onChange={(e) => onCameraChange({ focalLengthMm: Number(e.target.value) })}
        />
        <span className="pcg-preview__camera-value">{state.focalLengthMm.toFixed(0)}mm</span>
      </label>
      <label className="pcg-preview__camera-row">
        <span>Aperture</span>
        <input
          type="range"
          min={CAMERA_LIMITS.apertureFstop.min}
          max={22}
          step={0.1}
          value={Math.min(state.apertureFstop, 22)}
          onChange={(e) => onCameraChange({ apertureFstop: Number(e.target.value) })}
        />
        <span className="pcg-preview__camera-value">f/{state.apertureFstop.toFixed(1)}</span>
      </label>
      <label className="pcg-preview__camera-row">
        <span>Focus</span>
        <input
          type="number"
          min={CAMERA_LIMITS.focusDistance.min}
          step={0.1}
          value={Number(state.focusDistance.toFixed(2))}
          onChange={(e) => onCameraChange({ focusDistance: Number(e.target.value) })}
        />
      </label>
      <label className="pcg-preview__overlay-row">
        <input
          type="checkbox"
          checked={state.dofEnabled}
          onChange={(e) => onCameraChange({ dofEnabled: e.target.checked })}
        />
        <span>Depth of field</span>
      </label>
      <label className="pcg-preview__camera-row">
        <span>Exposure</span>
        <input
          type="range"
          min={0.1}
          max={4}
          step={0.05}
          value={Math.min(Math.max(state.exposure, 0.1), 4)}
          onChange={(e) => onCameraChange({ exposure: Number(e.target.value) })}
        />
        <span className="pcg-preview__camera-value">{state.exposure.toFixed(2)}×</span>
      </label>

      <div className="pcg-preview__shading-section-label">Snapshot</div>
      <label className="pcg-preview__camera-row">
        <span>Size</span>
        <select
          value={resolutionIndex}
          onChange={(e) => setResolutionIndex(Number(e.target.value))}
        >
          {RESOLUTION_PRESETS.map((preset, index) => (
            <option key={preset.label} value={index}>{preset.label}</option>
          ))}
        </select>
      </label>
      <label className="pcg-preview__overlay-row">
        <input
          type="checkbox"
          checked={transparent}
          onChange={(e) => setTransparent(e.target.checked)}
        />
        <span>Transparent background</span>
      </label>
      <button
        type="button"
        className="pcg-preview__btn pcg-preview__camera-snapshot"
        onClick={snapshot}
      >
        Snapshot PNG
      </button>
    </div>
  );
}
