import { Handle, Position, type NodeProps } from '@xyflow/react';
import type { ParseConfigData } from '../graphSchema';
import { useNodeData } from './useNodeData';

export default function ParseConfigNode({ data }: NodeProps) {
  const d = data as unknown as ParseConfigData;
  const { patch } = useNodeData<ParseConfigData>();

  return (
    <div className="pcg-node pcg-node--config">
      <div className="pcg-node__header">ParseConfig</div>
      <div className="pcg-node__body">
        <label>
          Seed
          <input
            type="number"
            value={d.seed}
            onChange={(e) => patch({ seed: Number(e.target.value) })}
          />
        </label>
        <label>
          Density
          <input
            type="number"
            step="0.1"
            min="0"
            max="1"
            value={d.density}
            onChange={(e) => patch({ density: Number(e.target.value) })}
          />
        </label>
      </div>
      <Handle type="source" position={Position.Right} id="out" />
    </div>
  );
}
