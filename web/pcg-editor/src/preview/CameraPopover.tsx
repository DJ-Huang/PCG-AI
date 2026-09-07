import { useState } from 'react';

import {
  CAMERA_LIMITS,
  CAMERA_SENSOR_FITS,
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

      <label className="pcg-preview__camera-row">
        <span>Type</span>
        <select
          value={state.projection}
          onChange={(e) => onCameraChange({
            projection: e.target.value as PhysicalCameraState['projection'],
          })}
        >
          <option value="perspective">Perspective</option>
          <option value="orthographic">Orthographic</option>
        </select>
      </label>

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
        <span>Sensor Fit</span>
        <select
          value={state.sensorFit}
          onChange={(e) => onCameraChange({
            sensorFit: e.target.value as PhysicalCameraState['sensorFit'],
          })}
        >
          {CAMERA_SENSOR_FITS.map((fit) => (
            <option key={fit} value={fit}>{fit.toUpperCase()}</option>
          ))}
        </select>
      </label>
      <label className="pcg-preview__camera-row">
        <span>Sensor W</span>
        <input
          type="number"
          min={CAMERA_LIMITS.sensorWidthMm.min}
          max={CAMERA_LIMITS.sensorWidthMm.max}
          step={0.1}
          value={state.sensorWidthMm}
          onChange={(e) => onCameraChange({ sensorWidthMm: Number(e.target.value) })}
        />
        <span className="pcg-preview__camera-value">mm</span>
      </label>
      <label className="pcg-preview__camera-row">
        <span>Sensor H</span>
        <input
          type="number"
          min={CAMERA_LIMITS.sensorHeightMm.min}
          max={CAMERA_LIMITS.sensorHeightMm.max}
          step={0.1}
          value={state.sensorHeightMm}
          onChange={(e) => onCameraChange({ sensorHeightMm: Number(e.target.value) })}
        />
        <span className="pcg-preview__camera-value">mm</span>
      </label>
      <label className="pcg-preview__camera-row">
        <span>Shift X</span>
        <input
          type="number"
          min={CAMERA_LIMITS.shift.min}
          max={CAMERA_LIMITS.shift.max}
          step={0.01}
          value={state.shiftX}
          onChange={(e) => onCameraChange({ shiftX: Number(e.target.value) })}
        />
      </label>
      <label className="pcg-preview__camera-row">
        <span>Shift Y</span>
        <input
          type="number"
          min={CAMERA_LIMITS.shift.min}
          max={CAMERA_LIMITS.shift.max}
          step={0.01}
          value={state.shiftY}
          onChange={(e) => onCameraChange({ shiftY: Number(e.target.value) })}
        />
      </label>
      {state.projection === 'orthographic' && (
        <label className="pcg-preview__camera-row">
          <span>Ortho Scale</span>
          <input
            type="number"
            min={CAMERA_LIMITS.orthographicScale.min}
            max={CAMERA_LIMITS.orthographicScale.max}
            step={0.1}
            value={state.orthographicScale}
            onChange={(e) => onCameraChange({ orthographicScale: Number(e.target.value) })}
          />
        </label>
      )}
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
        <span>Blades</span>
        <input
          type="number"
          min={CAMERA_LIMITS.apertureBlades.min}
          max={CAMERA_LIMITS.apertureBlades.max}
          step={1}
          value={state.apertureBlades}
          onChange={(e) => onCameraChange({ apertureBlades: Number(e.target.value) })}
        />
      </label>
      <label className="pcg-preview__camera-row">
        <span>Blade Rot.</span>
        <input
          type="number"
          min={CAMERA_LIMITS.apertureRotationDeg.min}
          max={CAMERA_LIMITS.apertureRotationDeg.max}
          step={1}
          value={state.apertureRotationDeg}
          onChange={(e) => onCameraChange({ apertureRotationDeg: Number(e.target.value) })}
        />
        <span className="pcg-preview__camera-value">°</span>
      </label>
      <label className="pcg-preview__camera-row">
        <span>Aperture Ratio</span>
        <input
          type="number"
          min={CAMERA_LIMITS.apertureRatio.min}
          max={CAMERA_LIMITS.apertureRatio.max}
          step={0.01}
          value={state.apertureRatio}
          onChange={(e) => onCameraChange({ apertureRatio: Number(e.target.value) })}
        />
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
      <button
        type="button"
        className="pcg-preview__btn"
        onClick={() => onCameraChange({ focusOnTarget: true })}
      >
        Focus on Target
      </button>
      <label className="pcg-preview__overlay-row">
        <input
          type="checkbox"
          checked={state.dofEnabled}
          onChange={(e) => onCameraChange({ dofEnabled: e.target.checked })}
        />
        <span>Depth of field</span>
      </label>
      <p className="pcg-preview__camera-hint">
        Blades, rotation, and ratio are stored for Blender parity. Preview DOF is an approximation.
      </p>
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

      <div className="pcg-preview__shading-section-label">Clipping</div>
      <label className="pcg-preview__camera-row">
        <span>Start</span>
        <input
          type="number"
          min={CAMERA_LIMITS.near.min}
          max={CAMERA_LIMITS.near.max}
          step={0.01}
          value={state.near}
          onChange={(e) => onCameraChange({ near: Number(e.target.value) })}
        />
      </label>
      <label className="pcg-preview__camera-row">
        <span>End</span>
        <input
          type="number"
          min={CAMERA_LIMITS.far.min}
          max={CAMERA_LIMITS.far.max}
          step={1}
          value={state.far}
          onChange={(e) => onCameraChange({ far: Number(e.target.value) })}
        />
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
