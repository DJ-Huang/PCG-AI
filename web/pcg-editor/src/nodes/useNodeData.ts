import { useReactFlow, useNodeId } from '@xyflow/react';

export function useNodeData() {
  const id = useNodeId();
  const { updateNodeData } = useReactFlow();

  const patch = (partial: Record<string, unknown>) => {
    if (id) updateNodeData(id, partial);
  };

  return { id, patch };
}
