import { useEffect, useRef, useState } from 'react';

import {
  CAMERA_PRESET_CATEGORY_LABELS,
  getCameraPresetsByCategory,
  type CameraPreset,
} from './cameraPresets';

export type CameraSearchSelection =
  | { kind: 'preset'; preset: CameraPreset }
  | { kind: 'motionCurve' };

export interface CameraSearchPanelConfig {
  x: number;
  y: number;
  flowPosition?: { x: number; y: number };
  onSelect: (selection: CameraSearchSelection | null) => void;
}

interface CameraSearchPanelProps {
  config: CameraSearchPanelConfig;
}

export default function CameraSearchPanel({ config }: CameraSearchPanelProps) {
  const [query, setQuery] = useState('');
  const [expanded, setExpanded] = useState<Set<string>>(
    () => new Set(Object.values(CAMERA_PRESET_CATEGORY_LABELS)),
  );
  const inputRef = useRef<HTMLInputElement>(null);

  useEffect(() => {
    inputRef.current?.focus();
  }, []);

  const toggleCategory = (category: string) => {
    setExpanded((prev) => {
      const next = new Set(prev);
      if (next.has(category)) next.delete(category);
      else next.add(category);
      return next;
    });
  };

  const filtered = new Map<string, CameraPreset[]>();
  const needle = query.trim().toLowerCase();
  for (const [category, presets] of getCameraPresetsByCategory()) {
    const matched = needle
      ? presets.filter((preset) => (
        preset.name.toLowerCase().includes(needle)
        || preset.id.toLowerCase().includes(needle)
        || (preset.notes ?? '').toLowerCase().includes(needle)
      ))
      : presets;
    if (matched.length > 0) filtered.set(category, matched);
  }

  const handleKeyDown = (event: React.KeyboardEvent) => {
    if (event.key === 'Escape') config.onSelect(null);
  };

  const showMotionCurve = !needle || 'motion curve'.includes(needle) || 'path'.includes(needle);

  return (
    <div
      className="pcg-search-panel"
      style={{ left: config.x, top: config.y }}
      onKeyDown={handleKeyDown}
    >
      <input
        ref={inputRef}
        type="text"
        className="pcg-search-panel__input"
        placeholder="Search cameras..."
        value={query}
        onChange={(event) => setQuery(event.target.value)}
        onKeyDown={handleKeyDown}
      />
      <div className="pcg-search-panel__list">
        {filtered.size === 0 && !showMotionCurve && (
          <div className="pcg-search-panel__empty">No matching cameras</div>
        )}
        <div className="pcg-search-panel__group">
          <div className="pcg-search-panel__group-title">
            <span className="pcg-search-panel__chevron pcg-search-panel__chevron--open">▸</span>
            Nodes
            <span className="pcg-search-panel__count">1</span>
          </div>
          {showMotionCurve && (
            <button
              type="button"
              className="pcg-search-panel__item"
              title="Editable 3D path. Wire this into a camera input."
              onClick={() => config.onSelect({ kind: 'motionCurve' })}
            >
              Motion Curve
              <span className="pcg-search-panel__badge">path</span>
            </button>
          )}
        </div>
        {Array.from(filtered.entries()).map(([category, presets]) => {
          const isExpanded = needle !== '' || expanded.has(category);
          return (
            <div key={category} className="pcg-search-panel__group">
              <button
                type="button"
                className="pcg-search-panel__group-title"
                onClick={() => toggleCategory(category)}
              >
                <span className={`pcg-search-panel__chevron${isExpanded ? ' pcg-search-panel__chevron--open' : ''}`}>
                  ▸
                </span>
                {category}
                <span className="pcg-search-panel__count">{presets.length}</span>
              </button>
              {isExpanded &&
                presets.map((preset) => (
                  <button
                    key={preset.id}
                    type="button"
                    className="pcg-search-panel__item"
                    title={preset.notes}
                    onClick={() => config.onSelect({ kind: 'preset', preset })}
                  >
                    {preset.name}
                    <span className="pcg-search-panel__badge">
                      {preset.sensorWidthMm}×{preset.sensorHeightMm}
                    </span>
                  </button>
                ))}
            </div>
          );
        })}
      </div>
    </div>
  );
}
