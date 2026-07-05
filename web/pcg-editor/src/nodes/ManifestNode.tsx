// ManifestNode.tsx — Generic manifest-driven node component.
// All node types use this single component; ports and colors are read from node-manifest.json.
// Layout: Header (top) → Input ports (left) → Output ports (right) → Properties summary.

import { Handle, Position, type NodeProps } from '@xyflow/react';
import { getNodeTypeDefs, getCategoryColor } from '../nodeManifest';

export default function ManifestNode({ type, selected }: NodeProps) {
  const def = getNodeTypeDefs(type ?? '');
  if (!def) {
    return (
      <div className="pcg-node pcg-node--unknown">
        <div className="pcg-node__header">Unknown: {type}</div>
      </div>
    );
  }

  const color = getCategoryColor(def.category);

  return (
    <div
      className={`pcg-node${selected ? ' pcg-node--selected' : ''}`}
      style={{ borderColor: color }}
    >
      {/* Header (top) */}
      <div className="pcg-node__header" style={{ background: color }}>
        {def.displayName}
      </div>

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
