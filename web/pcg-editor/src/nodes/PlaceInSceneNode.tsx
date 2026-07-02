import { Handle, Position, type NodeProps } from '@xyflow/react';
import type { PlaceInSceneData } from '../graphSchema';

export default function PlaceInSceneNode({ data }: NodeProps) {
  const d = data as unknown as PlaceInSceneData;
  return (
    <div className="pcg-node pcg-node--place">
      <Handle type="target" position={Position.Left} id="in" />
      <div className="pcg-node__header">PlaceInScene</div>
      <div className="pcg-node__body">
        <label>
          Prefab
          <input type="text" defaultValue={d.prefab} />
        </label>
        <label>
          Scale
          <input type="number" step="0.1" defaultValue={d.scale} />
        </label>
      </div>
    </div>
  );
}
