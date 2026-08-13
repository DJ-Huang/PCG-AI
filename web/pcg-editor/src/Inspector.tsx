// Inspector.tsx — Houdini-style right panel showing selected node properties.
// Supports promote-to-parameter (+) and bind/unbind via dropdown.
// Group properties (groupSelect/groupMultiSelect) resolve available groups from upstream nodes.

import { useCallback, useId, useMemo, type CSSProperties } from 'react';
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
import { findSubgraph, getSubgraphId, getSubgraphNodeTitle, isSubgraphInterfaceNode, useCurrentSubgraph, useSubgraphs } from './subgraphs';
import { resolvePbrTextureUrl } from './preview/pbrMaterials';

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

  const subgraphs = useSubgraphs();

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

  if (selectedNode.type === 'Subgraph') {
    return (
      <SubgraphInspector
        node={selectedNode}
        subgraphs={subgraphs}
        onUpdateNodeData={onUpdateNodeData}
      />
    );
  }

  if (isSubgraphInterfaceNode(selectedNode.type)) {
    return <InterfaceNodeInspector node={selectedNode} />;
  }

  if (selectedNode.type === 'Material') {
    return <MaterialInspector node={selectedNode} onUpdateNodeData={onUpdateNodeData} />;
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
  const normalizeVisible = (v: unknown): string => {
    if (v == null) return '';
    if (typeof v === 'boolean') return v ? 'true' : 'false';
    if (typeof v === 'string' && (v === 'true' || v === 'false')) return v;
    return String(v);
  };
  const isPropVisible = (prop: ManifestProperty): boolean => {
    const vw = prop.visibleWhen;
    if (!vw?.property) return true;
    const driver = def.properties[vw.property];
    const current = normalizeVisible(data[vw.property] ?? driver?.default);
    if (vw.oneOf && vw.oneOf.length > 0) {
      return vw.oneOf.some((c) => normalizeVisible(c) === current);
    }
    return current === normalizeVisible(vw.equals);
  };

  const companionTargets = new Set<string>();
  for (const prop of Object.values(def.properties)) {
    if (prop.companionField) companionTargets.add(prop.companionField);
  }

  const groupProps: [string, ManifestProperty][] = [];
  const regularProps: [string, ManifestProperty][] = [];
  for (const [key, prop] of Object.entries(def.properties)) {
    if (companionTargets.has(key)) continue;
    if (!isPropVisible(prop)) continue;
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

    const companionKey = prop.type === 'boolean' ? prop.companionField : undefined;
    const companionProp = companionKey ? def.properties[companionKey] : undefined;

    if (companionKey && companionProp) {
      const companionBinding = parameters.find(
        (p) => p.targetNode === selectedNode.id && p.targetProperty === companionKey,
      );
      const companionValue = data[companionKey] ?? companionProp.default;
      const companionGroups = companionProp.groupDomain
        ? filterGroupsByDomain(upstreamGroups, companionProp.groupDomain as GroupDomain)
        : upstreamGroups;
      const toggled = Boolean(value);

      return (
        <div key={key} className="pcg-inspector__prop">
          <div className="pcg-inspector__prop-header">
            <span className="pcg-inspector__prop-label">{prop.displayName ?? key}</span>
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
          <div className="pcg-inspector__prop-value pcg-inspector__toggle-row">
            <PropertyEditor
              prop={prop}
              value={value}
              disabled={isBound}
              binding={binding}
              availableGroups={filteredGroups}
              onChange={(v) => handleValueChange(selectedNode.id, key, v)}
            />
            <div className="pcg-inspector__toggle-companion">
              <PropertyEditor
                prop={companionProp}
                value={companionValue}
                disabled={!toggled || !!companionBinding}
                binding={companionBinding}
                availableGroups={companionGroups}
                onChange={(v) => handleValueChange(selectedNode.id, companionKey, v)}
              />
            </div>
          </div>
        </div>
      );
    }

    return (
      <div key={key} className="pcg-inspector__prop">
        <div className="pcg-inspector__prop-header">
          <span className="pcg-inspector__prop-label">{prop.displayName ?? key}</span>
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

// ── Material Inspector ────────────────────────────────

function MaterialInspector({
  node,
  onUpdateNodeData,
}: {
  node: Node;
  onUpdateNodeData: (nodeId: string, patch: Record<string, unknown>) => void;
}) {
  const data = node.data as NodeData;
  const update = (key: string, value: unknown) => onUpdateNodeData(node.id, { [key]: value });
  const numberValue = (key: string, fallback: number) => {
    const value = data[key];
    return typeof value === 'number' && Number.isFinite(value) ? value : fallback;
  };

  return (
    <div className="pcg-inspector pcg-material-inspector">
      <div className="pcg-inspector__title">Inspector</div>
      <div className="pcg-inspector__node-header" style={{ background: getCategoryColor('Material') }}>
        Material
      </div>
      <div className="pcg-inspector__node-type">Material · Shader driven</div>

      <div className="pcg-inspector__section">
        <div className="pcg-inspector__section-title">Shader</div>
        <MaterialTextField label="Name" value={String(data.materialName ?? 'Material')} onChange={(v) => update('materialName', v)} />
        <label className="pcg-material-inspector__field">
          <span>Web Shader</span>
          <select value={String(data.shaderId ?? 'pcg.standard-pbr')} onChange={(e) => update('shaderId', e.target.value)}>
            <option value="pcg.standard-pbr">Standard PBR</option>
          </select>
        </label>
        {data.unityShaderName ? (
          <div className="pcg-material-inspector__unity-shader">
            Unity: {String(data.unityShaderName)}
            <small>Web 使用 Standard PBR fallback，只映射通用 PBR 参数。</small>
          </div>
        ) : null}
      </div>

      <div className="pcg-inspector__section">
        <div className="pcg-inspector__section-title">Surface</div>
        <MaterialColorField label="Base Color" value={String(data.baseColor ?? '#b8c2cc')} onChange={(v) => update('baseColor', v)} />
        <MaterialSlider label="Metallic" value={numberValue('metallic', 0)} min={0} max={1} onChange={(v) => update('metallic', v)} />
        <MaterialSlider label="Roughness" value={numberValue('roughness', 0.5)} min={0} max={1} onChange={(v) => update('roughness', v)} />
        <MaterialSlider label="Opacity" value={numberValue('opacity', 1)} min={0} max={1} onChange={(v) => update('opacity', v)} />
        <label className="pcg-material-inspector__field">
          <span>Alpha Mode</span>
          <select value={String(data.alphaMode ?? 'opaque')} onChange={(e) => update('alphaMode', e.target.value)}>
            <option value="opaque">Opaque</option>
            <option value="mask">Mask</option>
            <option value="blend">Blend</option>
          </select>
        </label>
        {data.alphaMode === 'mask' && (
          <MaterialSlider label="Alpha Cutoff" value={numberValue('alphaCutoff', 0.5)} min={0} max={1} onChange={(v) => update('alphaCutoff', v)} />
        )}
        <label className="pcg-material-inspector__toggle">
          <input type="checkbox" checked={data.doubleSided === true} onChange={(e) => update('doubleSided', e.target.checked)} />
          <span>Double Sided</span>
        </label>
      </div>

      <div className="pcg-inspector__section">
        <div className="pcg-inspector__section-title">Maps</div>
        <MaterialTextureField label="Base Color" value={String(data.baseColorMap ?? '')} onChange={(v) => update('baseColorMap', v)} />
        <MaterialTextureField label="Metallic" value={String(data.metallicMap ?? '')} onChange={(v) => update('metallicMap', v)} />
        <MaterialTextureField label="Roughness" value={String(data.roughnessMap ?? '')} onChange={(v) => update('roughnessMap', v)} />
        <MaterialTextureField label="Normal" value={String(data.normalMap ?? '')} onChange={(v) => update('normalMap', v)} />
        <MaterialSlider label="Normal Scale" value={numberValue('normalScale', 1)} min={0} max={4} onChange={(v) => update('normalScale', v)} />
        <MaterialTextureField label="AO" value={String(data.aoMap ?? '')} onChange={(v) => update('aoMap', v)} />
        <MaterialSlider label="AO Intensity" value={numberValue('aoIntensity', 1)} min={0} max={4} onChange={(v) => update('aoIntensity', v)} />
      </div>

      <div className="pcg-inspector__section">
        <div className="pcg-inspector__section-title">Emission</div>
        <MaterialColorField label="Color" value={String(data.emissiveColor ?? '#000000')} onChange={(v) => update('emissiveColor', v)} />
        <MaterialTextureField label="Map" value={String(data.emissiveMap ?? '')} onChange={(v) => update('emissiveMap', v)} />
        <MaterialSlider label="Intensity" value={numberValue('emissiveIntensity', 0)} min={0} max={16} onChange={(v) => update('emissiveIntensity', v)} />
      </div>
    </div>
  );
}

function MaterialTextField({ label, value, onChange }: { label: string; value: string; onChange: (value: string) => void }) {
  return (
    <label className="pcg-material-inspector__field">
      <span>{label}</span>
      <input type="text" value={value} onChange={(e) => onChange(e.target.value)} />
    </label>
  );
}

function MaterialColorField({ label, value, onChange }: { label: string; value: string; onChange: (value: string) => void }) {
  const color = /^#[0-9a-f]{6}$/i.test(value) ? value : '#ffffff';
  return (
    <label className="pcg-material-inspector__field pcg-material-inspector__color">
      <span>{label}</span>
      <input type="color" value={color} onChange={(e) => onChange(e.target.value)} />
      <input type="text" value={value} onChange={(e) => onChange(e.target.value)} />
    </label>
  );
}

function MaterialSlider({
  label,
  value,
  min,
  max,
  onChange,
}: {
  label: string;
  value: number;
  min: number;
  max: number;
  onChange: (value: number) => void;
}) {
  const progress = max === min ? 0 : Math.min(1, Math.max(0, (value - min) / (max - min)));
  const sliderStyle = { '--pcg-material-slider-progress': `${progress * 100}%` } as CSSProperties;
  return (
    <label className="pcg-material-inspector__field pcg-material-inspector__slider">
      <span>{label}</span>
      <span className="pcg-material-inspector__slider-control" style={sliderStyle}>
        <span className="pcg-material-inspector__slider-track" aria-hidden>
          <span className="pcg-material-inspector__slider-fill" />
        </span>
        <input type="range" min={min} max={max} step="0.01" value={value} onChange={(e) => onChange(Number(e.target.value))} />
      </span>
      <input type="number" min={min} max={max} step="0.01" value={value} onChange={(e) => onChange(Number(e.target.value))} />
    </label>
  );
}

function MaterialTextureField({ label, value, onChange }: { label: string; value: string; onChange: (value: string) => void }) {
  const inputId = useId();
  const importImage = (file?: File) => {
    if (!file) return;
    const reader = new FileReader();
    reader.addEventListener('load', () => {
      if (typeof reader.result === 'string') onChange(reader.result);
    });
    reader.readAsDataURL(file);
  };
  return (
    <div className="pcg-material-inspector__texture">
      <label htmlFor={inputId}>{label}</label>
      <div>
        {value && <img src={resolvePbrTextureUrl(value)} alt="" />}
        <input type="text" value={value} placeholder="pcg-resource:// / URL / data URI" onChange={(e) => onChange(e.target.value)} />
        <input id={inputId} type="file" accept="image/*" hidden onChange={(e) => importImage(e.target.files?.[0])} />
        <label className="pcg-material-inspector__browse" htmlFor={inputId}>…</label>
        {value && <button type="button" title="Clear texture" onClick={() => onChange('')}>×</button>}
      </div>
    </div>
  );
}

// ── Subgraph Inspector ────────────────────────────────
// Read-mostly view of an inline subgraph instance: editable instance title,
// interface pins, and contents summary. Nested authoring stays in Unity.

/** Structural interface node (SubgraphInput/SubgraphOutput) — no editable props. */
function InterfaceNodeInspector({ node }: { node: Node }) {
  const currentSubgraph = useCurrentSubgraph();
  const isInput = node.type === 'SubgraphInput';
  const pins = currentSubgraph
    ? isInput
      ? currentSubgraph.inputs
      : currentSubgraph.outputs
    : [];
  const color = getCategoryColor('Structural');

  return (
    <div className="pcg-inspector">
      <div className="pcg-inspector__title">Inspector</div>
      <div className="pcg-inspector__node-header" style={{ background: color }}>
        {isInput ? 'Subgraph Input' : 'Subgraph Output'}
      </div>
      <div className="pcg-inspector__node-type">{node.type} · Structural</div>
      <div className="pcg-inspector__section">
        <div className="pcg-inspector__section-title">Interface Pins</div>
        {pins.map((pin) => (
          <div key={pin.id} className="pcg-inspector__pin-row">
            <span className="pcg-inspector__pin-dir">{isInput ? '←' : '→'}</span>
            <span className="pcg-inspector__pin-label">{pin.name}</span>
            <span className="pcg-inspector__pin-type">{pin.pinType || 'Any'}</span>
          </div>
        ))}
        {pins.length === 0 && <div className="pcg-inspector__no-props">No interface pins</div>}
        <div className="pcg-inspector__stat">Structural node — not deletable</div>
      </div>
    </div>
  );
}

function SubgraphInspector({
  node,
  subgraphs,
  onUpdateNodeData,
}: {
  node: Node;
  subgraphs: ReturnType<typeof useSubgraphs>;
  onUpdateNodeData: (nodeId: string, patch: Record<string, unknown>) => void;
}) {
  const data = node.data as NodeData;
  const subgraphId = getSubgraphId(data);
  const subgraph = findSubgraph(subgraphs, subgraphId);
  const title = getSubgraphNodeTitle(data, subgraph);
  const customTitle = typeof data.__nodeTitle === 'string' ? data.__nodeTitle : '';
  const color = getCategoryColor('Structural');

  if (!subgraph) {
    return (
      <div className="pcg-inspector">
        <div className="pcg-inspector__title">Inspector</div>
        <div className="pcg-inspector__node-header" style={{ background: color }}>
          {title}
        </div>
        <div className="pcg-inspector__node-type">Subgraph · Structural</div>
        <div className="pcg-inspector__section">
          <div className="pcg-panel__empty-hint">
            Missing subgraph definition: {subgraphId || '(no subgraphId set)'}
          </div>
        </div>
      </div>
    );
  }

  return (
    <div className="pcg-inspector">
      <div className="pcg-inspector__title">Inspector</div>
      <div className="pcg-inspector__node-header" style={{ background: color }}>
        {subgraph.name || subgraph.id}
      </div>
      <div className="pcg-inspector__node-type">Subgraph · Structural</div>

      <div className="pcg-inspector__section">
        <div className="pcg-inspector__section-title">Instance</div>
        <div className="pcg-inspector__props">
          <div className="pcg-inspector__prop">
            <div className="pcg-inspector__prop-header">
              <span className="pcg-inspector__prop-label">Title</span>
            </div>
            <div className="pcg-inspector__prop-value">
              <input
                type="text"
                value={customTitle}
                placeholder={subgraph.name || subgraph.id}
                onChange={(e) => onUpdateNodeData(node.id, { __nodeTitle: e.target.value })}
              />
            </div>
          </div>
          <div className="pcg-inspector__prop">
            <div className="pcg-inspector__prop-header">
              <span className="pcg-inspector__prop-label">Subgraph ID</span>
            </div>
            <div className="pcg-inspector__prop-value">
              <input type="text" value={subgraph.id} readOnly disabled />
            </div>
          </div>
        </div>
      </div>

      <div className="pcg-inspector__section">
        <div className="pcg-inspector__section-title">Interface</div>
        {subgraph.inputs.map((pin) => (
          <div key={pin.id} className="pcg-inspector__pin-row">
            <span className="pcg-inspector__pin-dir">→</span>
            <span className="pcg-inspector__pin-label">{pin.name}</span>
            <span className="pcg-inspector__pin-type">Any</span>
          </div>
        ))}
        {subgraph.outputs.map((pin) => (
          <div key={pin.id} className="pcg-inspector__pin-row">
            <span className="pcg-inspector__pin-dir">←</span>
            <span className="pcg-inspector__pin-label">{pin.name}</span>
            <span className="pcg-inspector__pin-type">{pin.pinType || 'Any'}</span>
          </div>
        ))}
        {subgraph.inputs.length === 0 && subgraph.outputs.length === 0 && (
          <div className="pcg-inspector__no-props">No interface pins</div>
        )}
      </div>

      <div className="pcg-inspector__section">
        <div className="pcg-inspector__section-title">Contents</div>
        <div className="pcg-inspector__stat">
          {subgraph.nodes.length} nodes · {subgraph.edges.length} edges
          {subgraph.parameters && subgraph.parameters.length > 0
            ? ` · ${subgraph.parameters.length} params`
            : ''}
        </div>
        {subgraph.parameters && subgraph.parameters.length > 0 && (
          <div className="pcg-inspector__props">
            {subgraph.parameters.map((p) => (
              <div key={p.id} className="pcg-inspector__pin-row">
                <span className="pcg-inspector__pin-label">{p.name}</span>
                <span className="pcg-inspector__pin-type">{p.type}</span>
              </div>
            ))}
          </div>
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
            disabled={disabled}
            onChange={(e) => onChange(Number(e.target.value))}
          />
        );
      }
      return (
        <input
          type="number"
          value={Number(value)}
          disabled={disabled}
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
            disabled={disabled}
            onChange={(e) => onChange(Number(e.target.value))}
          />
        );
      }
      return (
        <input
          type="number"
          step="0.1"
          value={Number(value)}
          disabled={disabled}
          onChange={(e) => onChange(Number(e.target.value))}
        />
      );

    case 'boolean':
      return (
        <input
          type="checkbox"
          checked={Boolean(value)}
          disabled={disabled}
          onChange={(e) => onChange(e.target.checked)}
        />
      );

    case 'enum':
      if (prop.uiHint === 'radio') {
        return (
          <div className="pcg-inspector__radio">
            {prop.options?.map((opt) => (
              <button
                key={opt.value}
                type="button"
                disabled={disabled}
                className={`pcg-inspector__radio-btn${String(value) === opt.value ? ' pcg-inspector__radio-btn--active' : ''}`}
                onClick={() => onChange(opt.value)}
              >
                {opt.label}
              </button>
            ))}
          </div>
        );
      }
      return (
        <select
          value={String(value)}
          disabled={disabled}
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
          disabled={disabled}
          onChange={(e) => onChange(e.target.value)}
        />
      );

    case 'texture2d':
      return (
        <input
          type="text"
          value={String(value ?? '')}
          disabled={disabled}
          placeholder="Texture path or URL"
          onChange={(e) => onChange(e.target.value)}
        />
      );

    case 'groupSelect':
      return (
        <GroupSelect
          value={String(value ?? '')}
          groups={availableGroups ?? []}
          disabled={disabled}
          onChange={onChange}
        />
      );

    case 'groupMultiSelect':
      return (
        <GroupMultiSelect
          value={String(value ?? '')}
          groups={availableGroups ?? []}
          disabled={disabled}
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
                disabled={disabled}
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
  disabled,
  onChange,
}: {
  value: string;
  groups: AvailableGroup[];
  disabled?: boolean;
  onChange: (value: string) => void;
}) {
  const listId = useId();

  return (
    <div className="pcg-group-select">
      <input
        type="text"
        list={listId}
        value={value}
        disabled={disabled}
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
  disabled,
  onChange,
}: {
  value: string;
  groups: AvailableGroup[];
  disabled?: boolean;
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
        disabled={disabled}
        onChange={(e) => onChange(e.target.value)}
        placeholder="Comma-separated group names"
        className="pcg-group-multiselect__text"
      />
    </div>
  );
}
