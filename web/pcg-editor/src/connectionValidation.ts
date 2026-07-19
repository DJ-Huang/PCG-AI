// Connection validation — manifest pinType-based, with "Any" wildcard support.
// Replaces the old hardcoded SpawnPoints→PlaceInScene rule.

import type { Connection, Edge, Node } from '@xyflow/react';
import { canConnect, getNodeTypeDefs } from './nodeManifest';

type ConnectLike = Connection | Edge;

/**
 * Validates a connection using manifest pinType matching:
 * - no self-loops
 * - source pinType must be compatible with target pinType (incl. "Any")
 * - no duplicate edges to the same target handle
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

  // Manifest-based pinType compatibility check
  const sHandle = sourceHandle ?? 'out';
  const tHandle = targetHandle ?? 'in';
  if (!canConnect(sourceNode.type, sHandle, targetNode.type, tHandle)) return false;

  // Prevent duplicate edges. Non-variadic input pins accept only one source.
  const targetPin = getNodeTypeDefs(targetNode.type)?.inputs.find((pin) => pin.id === tHandle);
  const duplicate = edges.some(
    (e) =>
      e.target === target &&
      (e.targetHandle ?? 'in') === tHandle &&
      (!targetPin?.variadic ||
        (e.source === source && (e.sourceHandle ?? 'out') === sHandle)),
  );
  if (duplicate) return false;

  return true;
}
