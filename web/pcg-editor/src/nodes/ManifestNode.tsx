// ManifestNode.tsx — Generic manifest-driven node component.
// All node types use this single component; ports and colors are read from node-manifest.json.
// Unity-style: compact pill with tiny port dots on top/bottom edges, title label to the right.
// Fields are NOT rendered on the node — editing happens in the Inspector panel.

import { useState } from 'react';
import { Handle, Position, type NodeProps } from '@xyflow/react';
import { getNodeTypeDefs, getPinTypeColor } from '../nodeManifest';

export default function ManifestNode({ type, selected, data }: NodeProps) {
  const [showGroupPopup, setShowGroupPopup] = useState(false);

  const def = getNodeTypeDefs(type ?? '');
  if (!def) {
    return (
      <div className="pcg-node pcg-node--unknown">
        <div className="pcg-node__title">Unknown: {type}</div>
      </div>
    );
  }

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
    <div className={`pcg-node${selected ? ' pcg-node--selected' : ''}`}>
      {/* Pill + port dots stack (ports stay centered on the pill) */}
      <div className="pcg-node__stack">
        {def.inputs.length > 0 && (
          <div className="pcg-node__ports pcg-node__ports--top">
            {def.inputs.map((pin) => (
              <Handle
                key={pin.id}
                type="target"
                position={Position.Top}
                id={pin.id}
                className="pcg-node__handle"
                style={{ background: getPinTypeColor(pin.pinType) }}
                title={pin.label}
              />
            ))}
          </div>
        )}

        <div className="pcg-node__pill" />

        {def.outputs.length > 0 && (
          <div className="pcg-node__ports pcg-node__ports--bottom">
            {def.outputs.map((pin) => (
              <Handle
                key={pin.id}
                type="source"
                position={Position.Bottom}
                id={pin.id}
                className="pcg-node__handle"
                style={{ background: getPinTypeColor(pin.pinType) }}
                title={pin.label}
              />
            ))}
          </div>
        )}
      </div>

      {/* Title to the right of the pill, Unity-style */}
      <div className="pcg-node__title">{def.displayName}</div>

      {/* Group output badge — compact chip with hover popup */}
      {producedGroups.length > 0 && (
        <div
          className="pcg-node__group-badge"
          onMouseEnter={() => setShowGroupPopup(true)}
          onMouseLeave={() => setShowGroupPopup(false)}
        >
          <span className="pcg-node__group-badge-count">
            🔗{activeGroups.length}
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
    </div>
  );
}
