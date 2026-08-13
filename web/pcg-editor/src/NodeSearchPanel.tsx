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
import {
  ensureLibraryIndex,
  getLibraryIndexSync,
  getLibraryItemsByCategory,
  libraryItemHasCompatibleInput,
  libraryItemHasCompatibleOutput,
  libraryItemMatchesQuery,
  type LibraryIndex,
  type LibraryIndexItem,
} from './libraryManifest';

export type NodeSearchSelection =
  | { kind: 'node'; nodeType: string }
  | { kind: 'library'; item: LibraryIndexItem };

export interface SearchPanelConfig {
  /** Screen position to anchor the panel. */
  x: number;
  y: number;
  /** When set, filters to nodes compatible with the dragged port's pinType. */
  filterPinType?: PinType;
  /** When true, the dragged port was an output (source) — filter for compatible inputs. */
  isSourcePort?: boolean;
  /** Called when user selects a node or library item, or null to cancel. */
  onSelect: (selection: NodeSearchSelection | null) => void;
}

interface NodeSearchPanelProps {
  config: SearchPanelConfig;
}

export default function NodeSearchPanel({ config }: NodeSearchPanelProps) {
  const [query, setQuery] = useState('');
  const [expanded, setExpanded] = useState<Set<string>>(new Set());
  const [library, setLibrary] = useState<LibraryIndex | null>(getLibraryIndexSync());
  const inputRef = useRef<HTMLInputElement>(null);

  useEffect(() => {
    inputRef.current?.focus();
  }, []);

  useEffect(() => {
    if (library) return;
    let cancelled = false;
    void ensureLibraryIndex().then((index) => {
      if (!cancelled && index) setLibrary(index);
    });
    return () => {
      cancelled = true;
    };
  }, [library]);

  const toggleCategory = (category: string) => {
    setExpanded((prev) => {
      const next = new Set(prev);
      if (next.has(category)) {
        next.delete(category);
      } else {
        next.add(category);
      }
      return next;
    });
  };

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

  // Builtin library items merge into the same panel under Library · <category>.
  const filteredLibrary = new Map<string, LibraryIndexItem[]>();
  if (library) {
    for (const [category, items] of getLibraryItemsByCategory()) {
      const matched = items.filter((item) => {
        if (query && !libraryItemMatchesQuery(item, query)) return false;
        if (config.filterPinType) {
          if (config.isSourcePort) {
            return libraryItemHasCompatibleInput(item, config.filterPinType);
          }
          return libraryItemHasCompatibleOutput(item, config.filterPinType);
        }
        return true;
      });
      if (matched.length > 0) {
        filteredLibrary.set(category, matched);
      }
    }
  }

  const handleKeyDown = (e: React.KeyboardEvent) => {
    if (e.key === 'Escape') {
      config.onSelect(null);
    }
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
        {filtered.size === 0 && filteredLibrary.size === 0 && (
          <div className="pcg-search-panel__empty">No matching nodes</div>
        )}
        {Array.from(filtered.entries()).map(([category, nodes]) => {
          // Searching or port-drag filtering always expands results.
          const isExpanded = query !== '' || config.filterPinType !== undefined || expanded.has(category);
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
                <span className="pcg-search-panel__count">{nodes.length}</span>
              </button>
              {isExpanded &&
                nodes.map((node) => (
                  <button
                    key={node.type}
                    type="button"
                    className="pcg-search-panel__item"
                    onClick={() => config.onSelect({ kind: 'node', nodeType: node.type })}
                  >
                    {node.displayName}
                  </button>
                ))}
            </div>
          );
        })}
        {Array.from(filteredLibrary.entries()).map(([category, items]) => {
          const group = `Library · ${category}`;
          const isExpanded = query !== '' || config.filterPinType !== undefined || expanded.has(group);
          return (
            <div key={group} className="pcg-search-panel__group">
              <button
                type="button"
                className="pcg-search-panel__group-title"
                onClick={() => toggleCategory(group)}
              >
                <span className={`pcg-search-panel__chevron${isExpanded ? ' pcg-search-panel__chevron--open' : ''}`}>
                  ▸
                </span>
                {group}
                <span className="pcg-search-panel__count">{items.length}</span>
              </button>
              {isExpanded &&
                items.map((item) => (
                  <button
                    key={item.id}
                    type="button"
                    className="pcg-search-panel__item"
                    title={item.description}
                    onClick={() => config.onSelect({ kind: 'library', item })}
                  >
                    {item.displayName}
                    <span className="pcg-search-panel__badge">lib</span>
                  </button>
                ))}
            </div>
          );
        })}
      </div>
    </div>
  );
}
