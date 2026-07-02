import { Handle, Position, type NodeProps } from '@xyflow/react';
import type { SpawnPointsData } from '../graphSchema';

export default function SpawnPointsNode({ data }: NodeProps) {
  const d = data as unknown as SpawnPointsData;
  return (
    <div className="pcg-node pcg-node--spawn">
      <Handle type="target" position={Position.Left} id="in" />
      <div className="pcg-node__header">SpawnPoints</div>
      <div className="pcg-node__body">
        <label>
          Count
          <input type="number" min="0" defaultValue={d.count} />
        </label>
        <label>
          Radius
          <input type="number" step="0.1" min="0" defaultValue={d.radius} />
        </label>
      </div>
      <Handle type="source" position={Position.Right} id="out" />
    </div>
  );
}
