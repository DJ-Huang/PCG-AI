import type { Connection, Node, Edge } from '@xyflow/react';
import type { NodeType } from './graphSchema';

/** Allowed downstream node types for each source type. */
const ALLOWED_TARGETS: Record<NodeType, NodeType[]> = {
  SpawnPoints: ['PlaceInScene'],
  PlaceInScene: [],
};

type ConnectLike = Connection | Edge;

/**
 * MVP connection rules:
 * - no self-loops
 * - pipeline types only (SpawnPoints → PlaceInScene)
 * - handles must be out → in
 * - one incoming edge per target handle
 */
export function isValidConnection(
  connection: ConnectLike,
  nodes: Node[],
  edges: Edge[],
): boolean {
  const { source, target, sourceHandle, targetHandle } = connection;
  if (!source || !target) return false;
  if (source === target) return false;

  const sourceNode = nodes.find((n) => n.id === source);
  const targetNode = nodes.find((n) => n.id === target);
  if (!sourceNode?.type || !targetNode?.type) return false;

  const sourceType = sourceNode.type as NodeType;
  const targetType = targetNode.type as NodeType;
  const allowed = ALLOWED_TARGETS[sourceType];
  if (!allowed?.includes(targetType)) return false;

  if (sourceHandle && sourceHandle !== 'out') return false;
  if (targetHandle && targetHandle !== 'in') return false;

  const duplicateIn = edges.some(
    (e) => e.target === target && (!targetHandle || e.targetHandle === targetHandle),
  );
  if (duplicateIn) return false;

  return true;
}
