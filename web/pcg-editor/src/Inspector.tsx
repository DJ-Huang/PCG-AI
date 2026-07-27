// Inspector.tsx — Houdini-style right panel showing selected node properties.
// Supports promote-to-parameter (+) and bind/unbind via dropdown.
// Group properties (groupSelect/groupMultiSelect) resolve available groups from upstream nodes.

import { useCallback, useId, useMemo } from 'react';
import type { Node, Edge } from '@xyflow/react';
import {
  getNodeTypeDefs,
  getCategoryColor,
  type ManifestProperty,
  type PropertyType,
  type GroupDomain,
} from './nodeManifest';
import type { GraphParameter, ParameterType, NodeData } from './graphSchema';
import { resolveUpstreamGroups, filterGroupsByDomain, type AvailableGroup } from './groupResolver';

interface InspectorProps {
  selectedNode: Node | null;
  parameters: GraphParameter[];
  nodes: Node[];
  edges: Edge[];
  onUpdateNodeData: (nodeId: string, patch: Record<string, unknown>) => void;
  onPromoteParameter: (nodeId: string, nodeType: string, propertyKey: string, prop: ManifestProperty) => void;
  onBindParameter: (nodeId: string, propertyKey: string, paramId: string | null) => void;
}

/** Checks if a parameter type is compatible with a manifest property type. */
function typesCompatible(paramType: ParameterType, propType: PropertyType): boolean {
  if (paramType === 'number') return propType === 'number' || propType === 'integer';
  if (paramType === 'integer') return propType === 'integer';
  if (paramType === 'boolean') return propType === 'boolean';
  if (paramType === 'string') return propType === 'string' || propType === 'enum' || propType === 'groupSelect' || propType === 'groupMultiSelect';
  if (paramType === 'vector3') return propType === 'vector3';
  return false;
}

export default function Inspector({
  selectedNode,
  parameters,
  nodes,
  edges,
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

  // Resolve available groups from upstream SpatialMesh connections (Houdini-style)
  const upstreamGroups = useMemo(
    () => selectedNode ? resolveUpstreamGroups(selectedNode.id, nodes, edges) : [],
    [selectedNode, nodes, edges],
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

  // Split properties into group-related and regular
  const groupProps: [string, ManifestProperty][] = [];
  const regularProps: [string, ManifestProperty][] = [];
  for (const [key, prop] of Object.entries(def.properties)) {
    if (prop.type === 'groupSelect' || prop.type === 'groupMultiSelect' || prop.isGroupOutput) {
      groupProps.push([key, prop]);
    } else {
      regularProps.push([key, prop]);
    }
  }

  const renderProp = (key: string, prop: ManifestProperty) => {
    const binding = parameters.find(
      (p) => p.targetNode === selectedNode.id && p.targetProperty === key,
    );
    const isBound = !!binding;
    const value = data[key] ?? prop.default;
    const filteredGroups = prop.groupDomain
      ? filterGroupsByDomain(upstreamGroups, prop.groupDomain as GroupDomain)
      : upstreamGroups;

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
            availableGroups={filteredGroups}
            onChange={(v) => handleValueChange(selectedNode.id, key, v)}
          />
        </div>
      </div>
    );
  };

  const hasGroupConsumers = groupProps.some(([, p]) => p.type === 'groupSelect' || p.type === 'groupMultiSelect');

  return (
    <div className="pcg-inspector">
      <div className="pcg-inspector__title">Inspector</div>
      <div className="pcg-inspector__node-header" style={{ background: color }}>
        {def.displayName}
      </div>
      <div className="pcg-inspector__node-type">{selectedNode.type}</div>

      {groupProps.length > 0 && (
        <div className="pcg-inspector__section">
          <div className="pcg-inspector__section-title">Groups</div>
          <div className="pcg-inspector__props">
            {groupProps.map(([key, prop]) => renderProp(key, prop))}
          </div>
          {hasGroupConsumers && (
            <div className={`pcg-inspector__group-hint${upstreamGroups.length === 0 ? ' pcg-inspector__group-hint--empty' : ''}`}>
              {upstreamGroups.length > 0
                ? `${upstreamGroups.length} group${upstreamGroups.length !== 1 ? 's' : ''} available from upstream`
                : 'No groups from upstream — connect a Group Create or Sweep node'}
            </div>
          )}
        </div>
      )}

      <div className="pcg-inspector__section">
        {groupProps.length > 0 && <div className="pcg-inspector__section-title">Parameters</div>}
        <div className="pcg-inspector__props">
          {regularProps.map(([key, prop]) => renderProp(key, prop))}
          {Object.keys(def.properties).length === 0 && (
            <div className="pcg-inspector__no-props">No properties</div>
          )}
        </div>
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
  availableGroups?: AvailableGroup[];
  onChange: (value: unknown) => void;
}

function PropertyEditor({ prop, value, disabled, binding, availableGroups, onChange }: PropertyEditorProps) {
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

    case 'groupSelect':
      return (
        <GroupSelect
          value={String(value ?? '')}
          groups={availableGroups ?? []}
          onChange={onChange}
        />
      );

    case 'groupMultiSelect':
      return (
        <GroupMultiSelect
          value={String(value ?? '')}
          groups={availableGroups ?? []}
          onChange={onChange}
        />
      );

    case 'vector3': {
      const components = parseVector3(value);
      const setAxis = (index: 0 | 1 | 2, next: number) => {
        const updated: [number, number, number] = [...components];
        updated[index] = Number.isFinite(next) ? next : 0;
        onChange(updated);
      };
      return (
        <div className="pcg-inspector__vector3">
          {(['X', 'Y', 'Z'] as const).map((label, index) => (
            <label key={label} className="pcg-inspector__vector3-axis">
              <span>{label}</span>
              <input
                type="number"
                step="0.1"
                value={components[index as 0 | 1 | 2]}
                onChange={(e) => setAxis(index as 0 | 1 | 2, Number(e.target.value))}
              />
            </label>
          ))}
        </div>
      );
    }

    default:
      return <span>{String(value)}</span>;
  }
}

function parseVector3(value: unknown): [number, number, number] {
  if (Array.isArray(value) && value.length >= 3) {
    return [
      Number(value[0]) || 0,
      Number(value[1]) || 0,
      Number(value[2]) || 0,
    ];
  }
  if (value && typeof value === 'object') {
    const obj = value as { x?: unknown; y?: unknown; z?: unknown };
    return [Number(obj.x) || 0, Number(obj.y) || 0, Number(obj.z) || 0];
  }
  if (typeof value === 'string') {
    const match = value.match(/^\(?\s*([-\d.eE]+)\s*,\s*([-\d.eE]+)\s*,\s*([-\d.eE]+)\s*\)?$/);
    if (match) {
      return [Number(match[1]) || 0, Number(match[2]) || 0, Number(match[3]) || 0];
    }
  }
  return [0, 0, 0];
}

// ── Group Select (single) ──────────────────────────────
// Houdini-style: text input with datalist autocomplete from upstream groups.

function GroupSelect({
  value,
  groups,
  onChange,
}: {
  value: string;
  groups: AvailableGroup[];
  onChange: (value: string) => void;
}) {
  const listId = useId();

  return (
    <div className="pcg-group-select">
      <input
        type="text"
        list={listId}
        value={value}
        onChange={(e) => onChange(e.target.value)}
        placeholder="(all or type name)"
        className="pcg-group-select__input"
      />
      <datalist id={listId}>
        {groups.map((g) => (
          <option key={`${g.source}-${g.name}`} value={g.name}>
            {g.label ? `${g.label} — ${g.name}` : g.name} ({g.domain})
          </option>
        ))}
      </datalist>
      {groups.length > 0 && (
        <div className="pcg-group-select__chips">
          {groups.map((g) => (
            <button
              key={`${g.source}-${g.name}`}
              type="button"
              className={`pcg-group-chip${value === g.name ? ' pcg-group-chip--active' : ''}`}
              onClick={() => onChange(g.name)}
              title={`${g.label ?? g.name} (${g.domain}) from ${g.sourceType}`}
            >
              {g.name}
              <span className="pcg-group-chip__domain">{g.domain}</span>
            </button>
          ))}
        </div>
      )}
    </div>
  );
}

// ── Group Multi-Select ─────────────────────────────────
// Shows checkboxes for each available group + text input for custom names.

function GroupMultiSelect({
  value,
  groups,
  onChange,
}: {
  value: string;
  groups: AvailableGroup[];
  onChange: (value: string) => void;
}) {
  const selected = value.split(',').map((s) => s.trim()).filter(Boolean);

  const toggle = (name: string) => {
    if (selected.includes(name)) {
      onChange(selected.filter((s) => s !== name).join(','));
    } else {
      onChange([...selected, name].join(','));
    }
  };

  // Show custom names that aren't in the available groups
  const availableNames = new Set(groups.map((g) => g.name));
  const customSelected = selected.filter((s) => !availableNames.has(s));

  return (
    <div className="pcg-group-multiselect">
      {groups.length > 0 && (
        <div className="pcg-group-multiselect__list">
          {groups.map((g) => (
            <label key={`${g.source}-${g.name}`} className="pcg-group-multiselect__item">
              <input
                type="checkbox"
                checked={selected.includes(g.name)}
                onChange={() => toggle(g.name)}
              />
              <span className="pcg-group-multiselect__name">{g.name}</span>
              <span className="pcg-group-multiselect__domain">{g.domain}</span>
            </label>
          ))}
          {customSelected.map((name) => (
            <label key={`custom-${name}`} className="pcg-group-multiselect__item">
              <input
                type="checkbox"
                checked
                onChange={() => toggle(name)}
              />
              <span className="pcg-group-multiselect__name">{name}</span>
              <span className="pcg-group-multiselect__domain">custom</span>
            </label>
          ))}
        </div>
      )}
      <input
        type="text"
        value={value}
        onChange={(e) => onChange(e.target.value)}
        placeholder="Comma-separated group names"
        className="pcg-group-multiselect__text"
      />
    </div>
  );
}
