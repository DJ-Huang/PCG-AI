import { Handle, Position, type NodeProps } from '@xyflow/react';
import type { SpawnPointsData } from '../graphSchema';
import { useNodeData } from './useNodeData';

export default function SpawnPointsNode({ data }: NodeProps) {
  const d = data as unknown as SpawnPointsData;
  const { patch } = useNodeData<SpawnPointsData>();

  return (
    <div className="pcg-node pcg-node--spawn">
      <Handle type="target" position={Position.Left} id="in" />
      <div className="pcg-node__header">SpawnPoints</div>
      <div className="pcg-node__body">
        <label>
          Count
          <input
            type="number"
            min="0"
            value={d.count}
            onChange={(e) => patch({ count: Number(e.target.value) })}
          />
        </label>
        <label>
          Radius
          <input
            type="number"
            step="0.1"
            min="0"
            value={d.radius}
            onChange={(e) => patch({ radius: Number(e.target.value) })}
          />
        </label>
      </div>
      <Handle type="source" position={Position.Right} id="out" />
    </div>
  );
}
