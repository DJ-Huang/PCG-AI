import { useEffect, useRef } from 'react';

import type { GraphParameter } from '../graphSchema';
import type { PreviewParameterValue, PreviewParameterValues } from '../previewParameters';

interface PreviewParametersPopoverProps {
  parameters: GraphParameter[];
  nodeIds: ReadonlySet<string>;
  values: PreviewParameterValues;
  open: boolean;
  onOpenChange: (open: boolean) => void;
  onValueChange: (parameterId: string, value: PreviewParameterValue) => void;
  onReset: () => void;
  onSaveDefaults: () => void;
}

export default function PreviewParametersPopover({
  parameters,
  nodeIds,
  values,
  open,
  onOpenChange,
  onValueChange,
  onReset,
  onSaveDefaults,
}: PreviewParametersPopoverProps) {
  const rootRef = useRef<HTMLDivElement>(null);
  const exposed = parameters.filter((parameter) => parameter.exposed);

  useEffect(() => {
    if (!open) return;
    const onPointerDown = (event: PointerEvent) => {
      if (rootRef.current?.contains(event.target as Node)) return;
      onOpenChange(false);
    };
    const onKeyDown = (event: KeyboardEvent) => {
      if (event.key === 'Escape') onOpenChange(false);
    };
    window.addEventListener('pointerdown', onPointerDown);
    window.addEventListener('keydown', onKeyDown);
    return () => {
      window.removeEventListener('pointerdown', onPointerDown);
      window.removeEventListener('keydown', onKeyDown);
    };
  }, [open, onOpenChange]);

  return (
    <div className="pcg-preview-parameters" ref={rootRef}>
      <button
        type="button"
        className={`pcg-preview__btn pcg-preview-parameters__toggle${open ? ' is-active' : ''}`}
        aria-expanded={open}
        title={exposed.length === 0 ? 'No exposed parameters in this graph' : 'Adjust exposed preview parameters'}
        onClick={() => onOpenChange(!open)}
      >
        Parameters {exposed.length}
      </button>
      {open && (
        <div className="pcg-preview-parameters__popover" role="dialog" aria-label="Preview parameters">
          <div className="pcg-preview-parameters__header">
            <span>Preview Parameters</span>
            <button type="button" onClick={() => onOpenChange(false)} aria-label="Collapse preview parameters">⌄</button>
          </div>
          <div className="pcg-preview-parameters__list">
            {exposed.length === 0 && (
              <div className="pcg-preview-parameters__empty">
                No exposed parameters in this graph. Expose node properties from the editor first.
              </div>
            )}
            {exposed.map((parameter) => {
              const bound = Boolean(
                parameter.targetNode &&
                parameter.targetProperty &&
                nodeIds.has(parameter.targetNode),
              );
              const value = values[parameter.id] ?? parameter.default;
              return (
                <div key={parameter.id} className={`pcg-preview-parameters__row${bound ? '' : ' is-unbound'}`}>
                  <div className="pcg-preview-parameters__label">
                    <span title={parameter.name}>{parameter.name}</span>
                    {!bound && <span className="pcg-preview-parameters__unbound">Unbound</span>}
                  </div>
                  <ParameterControl
                    parameter={parameter}
                    value={value}
                    disabled={!bound}
                    onChange={(next) => onValueChange(parameter.id, next)}
                  />
                </div>
              );
            })}
          </div>
          {exposed.length > 0 && (
            <div className="pcg-preview-parameters__actions">
              <button type="button" onClick={onReset}>Reset</button>
              <button type="button" className="pcg-preview-parameters__save" onClick={onSaveDefaults}>Save as Defaults</button>
            </div>
          )}
        </div>
      )}
    </div>
  );
}

function ParameterControl({
  parameter,
  value,
  disabled,
  onChange,
}: {
  parameter: GraphParameter;
  value: PreviewParameterValue;
  disabled: boolean;
  onChange: (value: PreviewParameterValue) => void;
}) {
  if (parameter.type === 'boolean') {
    const checked = value === true;
    return (
      <label className="pcg-preview-parameters__boolean">
        <input type="checkbox" checked={checked} disabled={disabled} onChange={(event) => onChange(event.target.checked)} />
        <span>{checked ? 'On' : 'Off'}</span>
      </label>
    );
  }

  if (parameter.type === 'string') {
    return <input type="text" value={String(value)} disabled={disabled} onChange={(event) => onChange(event.target.value)} />;
  }

  if (parameter.type === 'vector3') {
    const components = Array.isArray(value) && value.length === 3 ? value : [0, 0, 0];
    return (
      <div className="pcg-preview-parameters__vector3">
        {(['X', 'Y', 'Z'] as const).map((axis, index) => (
          <label key={axis}>
            <span>{axis}</span>
            <input
              type="number"
              step="0.1"
              value={components[index]}
              disabled={disabled}
              onChange={(event) => {
                const next: [number, number, number] = [...components] as [number, number, number];
                next[index] = Number(event.target.value);
                onChange(next);
              }}
            />
          </label>
        ))}
      </div>
    );
  }

  const numericValue = Number(value);
  const updateNumber = (raw: string) => {
    const parsed = Number(raw);
    onChange(parameter.type === 'integer' ? Math.round(parsed) : parsed);
  };
  return (
    <div className={`pcg-preview-parameters__number${parameter.hasRange ? ' has-range' : ''}`}>
      {parameter.hasRange && (
        <input
          type="range"
          min={parameter.min}
          max={parameter.max}
          step={parameter.type === 'integer' ? 1 : 0.01}
          value={numericValue}
          disabled={disabled}
          onChange={(event) => updateNumber(event.target.value)}
        />
      )}
      <input
        type="number"
        step={parameter.type === 'integer' ? 1 : 0.1}
        value={numericValue}
        disabled={disabled}
        onChange={(event) => updateNumber(event.target.value)}
      />
    </div>
  );
}
