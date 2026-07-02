import { Handle, Position, type NodeProps } from '@xyflow/react';
import type { PlaceInSceneData } from '../graphSchema';
import { useNodeData } from './useNodeData';

export default function PlaceInSceneNode({ data }: NodeProps) {
  const d = data as unknown as PlaceInSceneData;
  const { patch } = useNodeData<PlaceInSceneData>();

  return (
    <div className="pcg-node pcg-node--place">
      <Handle type="target" position={Position.Left} id="in" />
      <div className="pcg-node__header">PlaceInScene</div>
      <div className="pcg-node__body">
        <label>
          Prefab
          <input
            type="text"
            value={d.prefab}
            onChange={(e) => patch({ prefab: e.target.value })}
          />
        </label>
        <label>
          Scale
          <input
            type="number"
            step="0.1"
            value={d.scale}
            onChange={(e) => patch({ scale: Number(e.target.value) })}
          />
        </label>
      </div>
    </div>
  );
}
