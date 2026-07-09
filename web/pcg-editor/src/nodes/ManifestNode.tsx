// ManifestNode.tsx — Generic manifest-driven node component.
// All node types use this single component; ports and colors are read from node-manifest.json.
// Layout: Header (top) → Input ports (left) → Output ports (right) → Properties summary.
// Nodes that produce groups show a "Groups" badge.

import { Handle, Position, type NodeProps } from '@xyflow/react';
import { getNodeTypeDefs, getCategoryColor } from '../nodeManifest';

export default function ManifestNode({ type, selected, data }: NodeProps) {
  const def = getNodeTypeDefs(type ?? '');
  if (!def) {
    return (
      <div className="pcg-node pcg-node--unknown">
        <div className="pcg-node__header">Unknown: {type}</div>
      </div>
    );
  }

  const color = getCategoryColor(def.category);

  // Collect group names this node produces (for badge display)
  const producedGroups: { name: string; domain: string }[] = [];
  if (def.outputGroups) {
    for (const og of def.outputGroups) {
      if (og.dynamic) {
        // Dynamic: read actual group name from node data
        const groupName = (data as Record<string, unknown>)?.[og.name];
        if (typeof groupName === 'string' && groupName.trim()) {
          producedGroups.push({ name: groupName, domain: og.domain });
        }
      } else {
        // Check condition property
        if (og.condition) {
          const condValue = (data as Record<string, unknown>)?.[og.condition];
          if (!condValue) continue;
        }
        producedGroups.push({ name: og.name, domain: og.domain });
      }
    }
  }

  return (
    <div
      className={`pcg-node${selected ? ' pcg-node--selected' : ''}`}
      style={{ borderColor: color }}
    >
      {/* Header (top) */}
      <div className="pcg-node__header" style={{ background: color }}>
        {def.displayName}
      </div>

      {/* Group output badge */}
      {producedGroups.length > 0 && (
        <div className="pcg-node__group-badge">
          {producedGroups.map((g) => (
            <span key={g.name} className="pcg-node__group-tag">
              {g.name}
              <span className="pcg-node__group-tag-domain">{g.domain}</span>
            </span>
          ))}
        </div>
      )}

      {/* Input ports (left side) */}
      {def.inputs.map((pin) => (
        <div key={pin.id} className="pcg-node__port-row">
          <Handle
            type="target"
            position={Position.Left}
            id={pin.id}
            style={{ background: color }}
          />
          <span className="pcg-node__port-label">{pin.label}</span>
        </div>
      ))}

      {/* Output ports (right side) */}
      {def.outputs.map((pin) => (
        <div key={pin.id} className="pcg-node__port-row pcg-node__port-row--output">
          <span className="pcg-node__port-label">{pin.label}</span>
          <Handle
            type="source"
            position={Position.Right}
            id={pin.id}
            style={{ background: color }}
          />
        </div>
      ))}

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
    </div>
  );
}
