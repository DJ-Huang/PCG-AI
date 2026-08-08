import { useEffect, useRef, useState } from 'react';

import {
  DEFAULT_MATCAP_ID,
  MATCAP_ENTRIES,
  type MatcapId,
} from './matcapLibrary';

export type SolidLighting = 'studio' | 'matcap' | 'flat';

interface SolidShadingPopoverProps {
  solidLighting: SolidLighting;
  matcapId: MatcapId;
  onSolidLightingChange: (lighting: SolidLighting) => void;
  onMatcapIdChange: (id: MatcapId) => void;
}

const LIGHTING_OPTIONS: { id: SolidLighting; label: string }[] = [
  { id: 'studio', label: 'Studio' },
  { id: 'matcap', label: 'MatCap' },
  { id: 'flat', label: 'Flat' },
];

export default function SolidShadingPopover({
  solidLighting,
  matcapId,
  onSolidLightingChange,
  onMatcapIdChange,
}: SolidShadingPopoverProps) {
  const [gridOpen, setGridOpen] = useState(false);
  const gridRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (!gridOpen) return;
    const onPointerDown = (e: PointerEvent) => {
      if (gridRef.current?.contains(e.target as Node)) return;
      setGridOpen(false);
    };
    window.addEventListener('pointerdown', onPointerDown);
    return () => window.removeEventListener('pointerdown', onPointerDown);
  }, [gridOpen]);

  return (
    <div className="pcg-preview__shading-popover" role="dialog" aria-label="Viewport Shading">
      <div className="pcg-preview__shading-popover-title">Viewport Shading</div>

      <div className="pcg-preview__shading-section-label">Lighting</div>
      <div className="pcg-preview__lighting-segment" role="radiogroup" aria-label="Lighting">
        {LIGHTING_OPTIONS.map((opt) => (
          <button
            key={opt.id}
            type="button"
            role="radio"
            aria-checked={solidLighting === opt.id}
            className={solidLighting === opt.id ? 'is-active' : ''}
            onClick={() => {
              onSolidLightingChange(opt.id);
              if (opt.id !== 'matcap') setGridOpen(false);
            }}
          >
            {opt.label}
          </button>
        ))}
      </div>

      {solidLighting === 'matcap' && (
        <div className="pcg-preview__matcap-picker">
          <button
            type="button"
            className="pcg-preview__matcap-preview-btn"
            aria-expanded={gridOpen}
            aria-label="Select MatCap"
            onClick={() => setGridOpen((v) => !v)}
          >
            <img
              src={`/matcaps/${matcapId}.png`}
              alt=""
              className="pcg-preview__matcap-preview-img"
            />
          </button>

          {gridOpen && (
            <div
              ref={gridRef}
              className="pcg-preview__matcap-grid-panel"
              role="listbox"
              aria-label="MatCap library"
            >
              <div className="pcg-preview__matcap-grid">
                {MATCAP_ENTRIES.map((entry) => (
                  <button
                    key={entry.id}
                    type="button"
                    role="option"
                    aria-selected={matcapId === entry.id}
                    className={`pcg-preview__matcap-thumb${matcapId === entry.id ? ' is-active' : ''}`}
                    title={entry.label}
                    onClick={() => {
                      onMatcapIdChange(entry.id);
                      setGridOpen(false);
                    }}
                  >
                    <img src={`/matcaps/${entry.id}.png`} alt={entry.label} />
                  </button>
                ))}
              </div>
            </div>
          )}
        </div>
      )}
    </div>
  );
}

export { DEFAULT_MATCAP_ID };
