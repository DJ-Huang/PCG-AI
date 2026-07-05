// Inspector.tsx — Houdini-style right panel showing selected node properties.
// Supports promote-to-parameter (+) and bind/unbind via dropdown.

import { useCallback } from 'react';
import type { Node } from '@xyflow/react';
import {
  getNodeTypeDefs,
  getCategoryColor,
  type ManifestProperty,
  type PropertyType,
} from './nodeManifest';
import type { GraphParameter, ParameterType, NodeData } from './graphSchema';

interface InspectorProps {
  selectedNode: Node | null;
  parameters: GraphParameter[];
  onUpdateNodeData: (nodeId: string, patch: Record<string, unknown>) => void;
  onPromoteParameter: (nodeId: string, nodeType: string, propertyKey: string, prop: ManifestProperty) => void;
  onBindParameter: (nodeId: string, propertyKey: string, paramId: string | null) => void;
}

/** Checks if a parameter type is compatible with a manifest property type. */
function typesCompatible(paramType: ParameterType, propType: PropertyType): boolean {
  if (paramType === 'number') return propType === 'number' || propType === 'integer';
  if (paramType === 'integer') return propType === 'integer';
  if (paramType === 'boolean') return propType === 'boolean';
  if (paramType === 'string') return propType === 'string' || propType === 'enum';
  return false;
}

export default function Inspector({
  selectedNode,
  parameters,
  onUpdateNodeData,
  onPromoteParameter,
  onBindParameter,
}: InspectorProps) {
  const handleValueChange = useCallback(
    (nodeId: string, key: string, value: unknown) => {
      onUpdateNodeData(nodeId, { [key]: value });
    },
    [onUpdateNodeData],
  );

  if (!selectedNode) {
    return (
      <div className="pcg-inspector pcg-panel--empty">
        <div className="pcg-inspector__title">Inspector</div>
        <div className="pcg-panel__empty-hint">Select a node to inspect</div>
      </div>
    );
  }

  const def = getNodeTypeDefs(selectedNode.type ?? '');
  if (!def) {
    return (
      <div className="pcg-inspector">
        <div className="pcg-inspector__title">Inspector</div>
        <div className="pcg-panel__empty-hint">Unknown node type: {selectedNode.type}</div>
      </div>
    );
  }

  const color = getCategoryColor(def.category);
  const data = selectedNode.data as NodeData;

  return (
    <div className="pcg-inspector">
      <div className="pcg-inspector__title">Inspector</div>
      <div className="pcg-inspector__node-header" style={{ background: color }}>
        {def.displayName}
      </div>
      <div className="pcg-inspector__node-type">{selectedNode.type}</div>

      <div className="pcg-inspector__props">
        {Object.entries(def.properties).map(([key, prop]) => {
          const binding = parameters.find(
            (p) => p.targetNode === selectedNode.id && p.targetProperty === key,
          );
          const isBound = !!binding;
          const value = data[key] ?? prop.default;

          return (
            <div key={key} className="pcg-inspector__prop">
              <div className="pcg-inspector__prop-header">
                <span className="pcg-inspector__prop-label">{key}</span>
                <div className="pcg-inspector__prop-actions">
                  <button
                    type="button"
                    className="pcg-inspector__promote"
                    title="Promote to Parameter"
                    disabled={isBound}
                    onClick={() => onPromoteParameter(selectedNode.id, selectedNode.type!, key, prop)}
                  >
                    +
                  </button>
                  <select
                    className="pcg-inspector__bind-select"
                    value={binding?.id ?? ''}
                    onChange={(e) => onBindParameter(selectedNode.id, key, e.target.value || null)}
                  >
                    <option value="">(none)</option>
                    {parameters
                      .filter((p) => typesCompatible(p.type, prop.type))
                      .map((p) => (
                        <option key={p.id} value={p.id}>
                          {p.name}
                        </option>
                      ))}
                  </select>
                </div>
              </div>
              <div className="pcg-inspector__prop-value">
                <PropertyEditor
                  prop={prop}
                  value={value}
                  disabled={isBound}
                  binding={binding}
                  onChange={(v) => handleValueChange(selectedNode.id, key, v)}
                />
              </div>
            </div>
          );
        })}
        {Object.keys(def.properties).length === 0 && (
          <div className="pcg-inspector__no-props">No properties</div>
        )}
      </div>
    </div>
  );
}

// ── Property Editor ────────────────────────────────────

interface PropertyEditorProps {
  prop: ManifestProperty;
  value: unknown;
  disabled: boolean;
  binding: GraphParameter | undefined;
  onChange: (value: unknown) => void;
}

function PropertyEditor({ prop, value, disabled, binding, onChange }: PropertyEditorProps) {
  if (disabled && binding) {
    // Bound: show blue readonly label
    const displayValue = binding.hasRange ? value : `(default: ${binding.default})`;
    return (
      <div className="pcg-inspector__bound">
        <span className="pcg-inspector__bound-label">→ {binding.name}</span>
        {binding.hasRange && (
          <span className="pcg-inspector__bound-value">{String(displayValue)}</span>
        )}
      </div>
    );
  }

  const hasRange = prop.minimum !== undefined && prop.maximum !== undefined;

  switch (prop.type) {
    case 'integer':
      if (hasRange) {
        return (
          <input
            type="range"
            min={prop.minimum}
            max={prop.maximum}
            value={Number(value)}
            onChange={(e) => onChange(Number(e.target.value))}
          />
        );
      }
      return (
        <input
          type="number"
          value={Number(value)}
          onChange={(e) => onChange(Number(e.target.value))}
        />
      );

    case 'number':
      if (hasRange) {
        return (
          <input
            type="range"
            min={prop.minimum}
            max={prop.maximum}
            step="0.01"
            value={Number(value)}
            onChange={(e) => onChange(Number(e.target.value))}
          />
        );
      }
      return (
        <input
          type="number"
          step="0.1"
          value={Number(value)}
          onChange={(e) => onChange(Number(e.target.value))}
        />
      );

    case 'boolean':
      return (
        <input
          type="checkbox"
          checked={Boolean(value)}
          onChange={(e) => onChange(e.target.checked)}
        />
      );

    case 'enum':
      return (
        <select
          value={String(value)}
          onChange={(e) => onChange(e.target.value)}
        >
          {prop.options?.map((opt) => (
            <option key={opt.value} value={opt.value}>
              {opt.label}
            </option>
          ))}
        </select>
      );

    case 'string':
      return (
        <input
          type="text"
          value={String(value ?? '')}
          onChange={(e) => onChange(e.target.value)}
        />
      );

    default:
      return <span>{String(value)}</span>;
  }
}
