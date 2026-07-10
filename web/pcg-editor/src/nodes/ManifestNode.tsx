// ManifestNode.tsx — Generic manifest-driven node component.
// All node types use this single component; ports and colors are read from node-manifest.json.
// Layout: Input ports (top, horizontal) → Header → Body → Output ports (bottom, horizontal).
// Houdini-style: each port is an independent colored dot on the node's top/bottom edge.
// Nodes that produce groups show a compact "🔗 N groups" badge with hover popup.

import { useState } from 'react';
import { Handle, Position, type NodeProps } from '@xyflow/react';
import { getNodeTypeDefs, getCategoryColor, getPinTypeColor } from '../nodeManifest';

export default function ManifestNode({ type, selected, data }: NodeProps) {
  const [showGroupPopup, setShowGroupPopup] = useState(false);

  const def = getNodeTypeDefs(type ?? '');
  if (!def) {
    return (
      <div className="pcg-node pcg-node--unknown">
        <div className="pcg-node__header">Unknown: {type}</div>
      </div>
    );
  }

  const color = getCategoryColor(def.category);
  const nodeData = data as Record<string, unknown>;

  // Collect group names this node produces (for badge display)
  const producedGroups: { name: string; domain: string; condition?: string; dynamic?: boolean }[] = [];
  if (def.outputGroups) {
    for (const og of def.outputGroups) {
      if (og.dynamic) {
        const groupName = nodeData?.[og.name];
        if (typeof groupName === 'string' && groupName.trim()) {
          producedGroups.push({ name: groupName, domain: og.domain, dynamic: true });
        }
      } else {
        let active = true;
        if (og.condition) {
          active = !!nodeData?.[og.condition];
        }
        producedGroups.push({ name: og.name, domain: og.domain, condition: og.condition && !active ? og.condition : undefined });
      }
    }
  }

  const activeGroups = producedGroups.filter((g) => !g.condition);
  const inactiveGroups = producedGroups.filter((g) => g.condition);

  return (
    <div
      className={`pcg-node${selected ? ' pcg-node--selected' : ''}`}
      style={{ borderColor: color }}
    >
      {/* Input ports — horizontal row at top edge */}
      {def.inputs.length > 0 && (
        <div className="pcg-node__ports-top">
          {def.inputs.map((pin) => (
            <div key={pin.id} className="pcg-node__port-item">
              <Handle
                type="target"
                position={Position.Top}
                id={pin.id}
                className="pcg-node__handle"
                style={{ background: getPinTypeColor(pin.pinType) }}
              />
              <span className="pcg-node__port-label">{pin.label}</span>
            </div>
          ))}
        </div>
      )}

      {/* Header */}
      <div className="pcg-node__header" style={{ background: color }}>
        {def.displayName}
      </div>

      {/* Group output badge — compact */}
      {producedGroups.length > 0 && (
        <div
          className="pcg-node__group-badge"
          onMouseEnter={() => setShowGroupPopup(true)}
          onMouseLeave={() => setShowGroupPopup(false)}
        >
          <span className="pcg-node__group-badge-count">
            🔗 {activeGroups.length} group{activeGroups.length !== 1 ? 's' : ''}
          </span>
          {showGroupPopup && (
            <div className="pcg-node__group-badge-popup">
              <div className="pcg-node__group-badge-popup-title">Output Groups</div>
              {activeGroups.map((g) => (
                <div key={g.name} className="pcg-node__group-badge-popup-row">
                  <span className="pcg-node__group-badge-popup-name">{g.name}</span>
                  <span className="pcg-node__group-badge-popup-domain">{g.domain}</span>
                </div>
              ))}
              {inactiveGroups.length > 0 && (
                <>
                  <div className="pcg-node__group-badge-popup-sep" />
                  <div className="pcg-node__group-badge-popup-title">Conditional (inactive)</div>
                  {inactiveGroups.map((g) => (
                    <div key={g.name} className="pcg-node__group-badge-popup-row pcg-node__group-badge-popup-row--inactive">
                      <span className="pcg-node__group-badge-popup-name">{g.name}</span>
                      <span className="pcg-node__group-badge-popup-domain">if {g.condition}</span>
                    </div>
                  ))}
                </>
              )}
            </div>
          )}
        </div>
      )}

      {/* Properties summary (read-only, editing in Inspector) */}
      {Object.keys(def.properties).length > 0 && (
        <div className="pcg-node__props-summary">
          {Object.keys(def.properties).map((key) => (
            <div key={key} className="pcg-node__prop-line">
              {key}
            </div>
          ))}
        </div>
      )}

      {/* Output ports — horizontal row at bottom edge */}
      {def.outputs.length > 0 && (
        <div className="pcg-node__ports-bottom">
          {def.outputs.map((pin) => (
            <div key={pin.id} className="pcg-node__port-item">
              <span className="pcg-node__port-label">{pin.label}</span>
              <Handle
                type="source"
                position={Position.Bottom}
                id={pin.id}
                className="pcg-node__handle"
                style={{ background: getPinTypeColor(pin.pinType) }}
              />
            </div>
          ))}
        </div>
      )}
    </div>
  );
}
