import { Handle, Position, type NodeProps } from '@xyflow/react';
import type { ParseConfigData } from '../graphSchema';

export default function ParseConfigNode({ data }: NodeProps) {
  const d = data as unknown as ParseConfigData;
  return (
    <div className="pcg-node pcg-node--config">
      <div className="pcg-node__header">ParseConfig</div>
      <div className="pcg-node__body">
        <label>
          Seed
          <input type="number" defaultValue={d.seed} />
        </label>
        <label>
          Density
          <input type="number" step="0.1" min="0" max="1" defaultValue={d.density} />
        </label>
      </div>
      <Handle type="source" position={Position.Right} id="out" />
    </div>
  );
}
