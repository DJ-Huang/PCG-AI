// NodeSearchPanel.tsx — Floating search panel for node creation.
// Two modes: normal (Space key / right-click) and filtered (port drag-to-search).

import { useState, useRef, useEffect } from 'react';
import {
  getNodesByCategory,
  type ManifestNodeDef,
  type PinType,
  hasCompatibleInputPin,
  hasCompatibleOutputPin,
} from './nodeManifest';

export interface SearchPanelConfig {
  /** Screen position to anchor the panel. */
  x: number;
  y: number;
  /** When set, filters to nodes compatible with the dragged port's pinType. */
  filterPinType?: PinType;
  /** When true, the dragged port was an output (source) — filter for compatible inputs. */
  isSourcePort?: boolean;
  /** Called when user selects a node type, or null to cancel. */
  onSelect: (nodeType: string | null) => void;
}

interface NodeSearchPanelProps {
  config: SearchPanelConfig;
}

export default function NodeSearchPanel({ config }: NodeSearchPanelProps) {
  const [query, setQuery] = useState('');
  const inputRef = useRef<HTMLInputElement>(null);

  useEffect(() => {
    inputRef.current?.focus();
  }, []);

  const allNodes = getNodesByCategory();

  // Filter by search text
  const filtered = new Map<string, ManifestNodeDef[]>();
  for (const [category, nodes] of allNodes) {
    const matched = nodes.filter((n) => {
      // Text filter
      if (query && !n.displayName.toLowerCase().includes(query.toLowerCase()) && !n.type.toLowerCase().includes(query.toLowerCase())) {
        return false;
      }
      // Port drag filter
      if (config.filterPinType) {
        if (config.isSourcePort) {
          // Source port is output → need nodes with compatible input pins
          return hasCompatibleInputPin(n.type, config.filterPinType);
        } else {
          // Target port is input → need nodes with compatible output pins
          return hasCompatibleOutputPin(n.type, config.filterPinType);
        }
      }
      return true;
    });
    if (matched.length > 0) {
      filtered.set(category, matched);
    }
  }

  const handleKeyDown = (e: React.KeyboardEvent) => {
    if (e.key === 'Escape') {
      config.onSelect(null);
    }
  };

  const handleSelect = (nodeType: string) => {
    config.onSelect(nodeType);
  };

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
        placeholder="Search nodes..."
        value={query}
        onChange={(e) => setQuery(e.target.value)}
        onKeyDown={handleKeyDown}
      />
      <div className="pcg-search-panel__list">
        {filtered.size === 0 && (
          <div className="pcg-search-panel__empty">No matching nodes</div>
        )}
        {Array.from(filtered.entries()).map(([category, nodes]) => (
          <div key={category} className="pcg-search-panel__group">
            <div className="pcg-search-panel__group-title">{category}</div>
            {nodes.map((node) => (
              <button
                key={node.type}
                type="button"
                className="pcg-search-panel__item"
                onClick={() => handleSelect(node.type)}
              >
                {node.displayName}
              </button>
            ))}
          </div>
        ))}
      </div>
    </div>
  );
}
