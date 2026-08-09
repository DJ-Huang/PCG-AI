import { useId } from 'react';

import {
  BUILTIN_ENVIRONMENTS,
  getBuiltinEnvironment,
} from './builtinEnvironments';

interface MaterialEnvironmentPopoverProps {
  environmentId: string;
  rotation: number;
  intensity: number;
  exposure: number;
  keyLightIntensity: number;
  backgroundVisible: boolean;
  onBuiltinEnvironment: (id: string) => void;
  onEnvironmentFile: (file: File) => void;
  onRotationChange: (value: number) => void;
  onIntensityChange: (value: number) => void;
  onExposureChange: (value: number) => void;
  onKeyLightIntensityChange: (value: number) => void;
  onBackgroundVisibleChange: (value: boolean) => void;
}

function SliderRow({
  label,
  value,
  min,
  max,
  step,
  onChange,
}: {
  label: string;
  value: number;
  min: number;
  max: number;
  step: number;
  onChange: (value: number) => void;
}) {
  return (
    <label className="pcg-preview__environment-row">
      <span>{label}</span>
      <input type="range" min={min} max={max} step={step} value={value} onChange={(e) => onChange(Number(e.target.value))} />
      <input type="number" min={min} max={max} step={step} value={value} onChange={(e) => onChange(Number(e.target.value))} />
    </label>
  );
}

export default function MaterialEnvironmentPopover({
  environmentId,
  rotation,
  intensity,
  exposure,
  keyLightIntensity,
  backgroundVisible,
  onBuiltinEnvironment,
  onEnvironmentFile,
  onRotationChange,
  onIntensityChange,
  onExposureChange,
  onKeyLightIntensityChange,
  onBackgroundVisibleChange,
}: MaterialEnvironmentPopoverProps) {
  const inputId = useId();
  const selectedEnvironment = getBuiltinEnvironment(environmentId);
  return (
    <div className="pcg-preview__environment-popover" role="dialog" aria-label="Material Preview Environment">
      <div className="pcg-preview__shading-popover-title">Material Preview</div>
      <details className="pcg-preview__environment-picker">
        <summary aria-label="Choose built-in HDRI">
          {selectedEnvironment ? (
            <img src={selectedEnvironment.previewUrl} alt="" />
          ) : (
            <span className="pcg-preview__environment-external">EXT</span>
          )}
          <span className="pcg-preview__environment-picker-chevron" aria-hidden>▾</span>
        </summary>
        <div className="pcg-preview__environment-grid" role="listbox" aria-label="Built-in HDRIs">
          {BUILTIN_ENVIRONMENTS.map((environment) => (
            <button
              key={environment.id}
              type="button"
              role="option"
              aria-label={environment.name}
              aria-selected={environment.id === environmentId}
              title={environment.name}
              onClick={(event) => {
                onBuiltinEnvironment(environment.id);
                event.currentTarget.closest('details')?.removeAttribute('open');
              }}
            >
              <img src={environment.previewUrl} alt="" loading="lazy" />
            </button>
          ))}
        </div>
      </details>
      <div className="pcg-preview__environment-file">
        <div>
          <small>Shift + RMB drag rotates the IBL</small>
        </div>
        <input
          id={inputId}
          type="file"
          accept=".hdr,.exr,image/vnd.radiance,image/x-exr"
          hidden
          onChange={(e) => {
            const file = e.target.files?.[0];
            if (file) onEnvironmentFile(file);
            e.target.value = '';
          }}
        />
        <label htmlFor={inputId}>Open</label>
      </div>
      <SliderRow label="Rotation" value={rotation} min={-180} max={180} step={1} onChange={onRotationChange} />
      <SliderRow label="IBL" value={intensity} min={0} max={4} step={0.01} onChange={onIntensityChange} />
      <SliderRow label="Exposure" value={exposure} min={0.1} max={4} step={0.01} onChange={onExposureChange} />
      <SliderRow label="Key Light" value={keyLightIntensity} min={0} max={6} step={0.01} onChange={onKeyLightIntensityChange} />
      <label className="pcg-preview__environment-toggle">
        <input type="checkbox" checked={backgroundVisible} onChange={(e) => onBackgroundVisibleChange(e.target.checked)} />
        <span>Show HDRI background</span>
      </label>
    </div>
  );
}
