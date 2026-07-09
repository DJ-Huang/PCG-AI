// NodeInfoPanel.tsx — Houdini-style read-only Node Info floating panel.
// Shows: node type/category, output groups, upstream groups, properties with current values.
// Triggered by hovering a node (300ms delay). Not an editor — Inspector handles editing.

import { useMemo, type CSSProperties } from 'react';
import type { Node, Edge } from '@xyflow/react';
import { getNodeTypeDefs, getCategoryColor } from './nodeManifest';
import { resolveUpstreamGroups, type AvailableGroup } from './groupResolver';
import type { NodeData } from './graphSchema';

interface NodeInfoPanelProps {
  node: Node;
  nodes: Node[];
  edges: Edge[];
  x: number;
  y: number;
}

export default function NodeInfoPanel({ node, nodes, edges, x, y }: NodeInfoPanelProps) {
  const def = getNodeTypeDefs(node.type ?? '');
  const data = node.data as NodeData;

  const upstreamGroups = useMemo(
    () => resolveUpstreamGroups(node.id, nodes, edges),
    [node.id, nodes, edges],
  );

  if (!def) return null;

  const color = getCategoryColor(def.category);

  // Collect output groups (same logic as ManifestNode badge)
  const outputGroups: { name: string; domain: string; condition?: string; active: boolean }[] = [];
  if (def.outputGroups) {
    for (const og of def.outputGroups) {
      if (og.dynamic) {
        const groupName = data[og.name as keyof NodeData] as unknown;
        if (typeof groupName === 'string' && groupName.trim()) {
          outputGroups.push({ name: groupName, domain: og.domain, active: true });
        }
      } else {
        const active = !og.condition || !!data[og.condition as keyof NodeData];
        outputGroups.push({
          name: og.name,
          domain: og.domain,
          condition: og.condition && !active ? og.condition : undefined,
          active,
        });
      }
    }
  }

  const panelStyle: CSSProperties = {
    left: x,
    top: y,
  };

  return (
    <div className="pcg-node-info-panel" style={panelStyle}>
      {/* Header */}
      <div className="pcg-node-info-panel__header" style={{ background: color }}>
        {def.displayName}
      </div>
      <div className="pcg-node-info-panel__type">
        {node.type} · {def.category}
      </div>

      {/* Output Groups */}
      {outputGroups.length > 0 && (
        <div className="pcg-node-info-panel__section">
          <div className="pcg-node-info-panel__section-title">Output Groups</div>
          {outputGroups.map((g) => (
            <div
              key={g.name}
              className={`pcg-node-info-panel__group-row${g.active ? '' : ' pcg-node-info-panel__group-row--inactive'}`}
            >
              <span className="pcg-node-info-panel__group-name">{g.name}</span>
              <span className="pcg-node-info-panel__group-domain">{g.domain}</span>
              {g.condition && (
                <span className="pcg-node-info-panel__group-cond">if {g.condition}</span>
              )}
            </div>
          ))}
        </div>
      )}

      {/* Upstream Groups */}
      {upstreamGroups.length > 0 && (
        <div className="pcg-node-info-panel__section">
          <div className="pcg-node-info-panel__section-title">
            Upstream Groups ({upstreamGroups.length})
          </div>
          {upstreamGroups.map((g: AvailableGroup) => (
            <div key={`${g.source}-${g.name}`} className="pcg-node-info-panel__group-row">
              <span className="pcg-node-info-panel__group-name">{g.name}</span>
              <span className="pcg-node-info-panel__group-domain">{g.domain}</span>
              <span className="pcg-node-info-panel__group-source">{g.sourceType}</span>
            </div>
          ))}
        </div>
      )}

      {/* Properties */}
      {Object.keys(def.properties).length > 0 && (
        <div className="pcg-node-info-panel__section">
          <div className="pcg-node-info-panel__section-title">Properties</div>
          {Object.entries(def.properties).map(([key, prop]) => {
            const value = data[key as keyof NodeData] ?? prop.default;
            return (
              <div key={key} className="pcg-node-info-panel__prop-row">
                <span className="pcg-node-info-panel__prop-key">{key}</span>
                <span className="pcg-node-info-panel__prop-value">
                  {formatValue(value, prop.type)}
                </span>
              </div>
            );
          })}
        </div>
      )}

      {/* Ports */}
      <div className="pcg-node-info-panel__section">
        <div className="pcg-node-info-panel__section-title">Pins</div>
        {def.inputs.map((pin) => (
          <div key={pin.id} className="pcg-node-info-panel__pin-row">
            <span className="pcg-node-info-panel__pin-dir">→</span>
            <span className="pcg-node-info-panel__pin-label">{pin.label}</span>
            <span className="pcg-node-info-panel__pin-type">{pin.pinType}</span>
          </div>
        ))}
        {def.outputs.map((pin) => (
          <div key={pin.id} className="pcg-node-info-panel__pin-row">
            <span className="pcg-node-info-panel__pin-dir">←</span>
            <span className="pcg-node-info-panel__pin-label">{pin.label}</span>
            <span className="pcg-node-info-panel__pin-type">{pin.pinType}</span>
          </div>
        ))}
      </div>
    </div>
  );
}

function formatValue(value: unknown, type: string): string {
  if (value === undefined || value === null || value === '') return '—';
  if (type === 'boolean') return value ? 'true' : 'false';
  if (type === 'enum') return String(value);
  if (typeof value === 'number') {
    return Number.isInteger(value) ? String(value) : value.toFixed(3);
  }
  return String(value);
}
