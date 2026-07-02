import { useReactFlow, useNodeId } from '@xyflow/react';
import type { NodeData } from '../graphSchema';

export function useNodeData<T extends NodeData>() {
  const id = useNodeId();
  const { updateNodeData } = useReactFlow();

  const patch = (partial: Partial<T>) => {
    if (id) updateNodeData(id, partial);
  };

  return { id, patch };
}
