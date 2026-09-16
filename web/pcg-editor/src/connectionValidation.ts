// Connection validation — manifest pinType-based, with "Any" wildcard support.
// Subgraph instance pins resolve against the referenced subgraph definition.
// Replaces the old hardcoded SpawnPoints→PlaceInScene rule.

import type { Connection, Edge, Node } from '@xyflow/react';
import { getNodeTypeDefs, pinTypesCompatible } from './nodeManifest';
import type { GraphSubgraph } from './graphSchema';
import { resolveInputPinType, resolveOutputPinType } from './subgraphs';

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
  subgraphs: GraphSubgraph[] = [],
  currentSubgraph: GraphSubgraph | null = null,
): boolean {
  const { source, target, sourceHandle, targetHandle } = connection;
  if (!source || !target) return false;
  if (source === target) return false;

  const sourceNode = nodes.find((n) => n.id === source);
  const targetNode = nodes.find((n) => n.id === target);
  if (!sourceNode?.type || !targetNode?.type) return false;

  // PinType compatibility (subgraph-aware; manifest "Any" wildcard applies)
  const sHandle = sourceHandle ?? 'out';
  const tHandle = targetHandle ?? 'in';
  const sourcePinType = resolveOutputPinType(sourceNode, sHandle, subgraphs, currentSubgraph);
  const targetPinType = resolveInputPinType(targetNode, tHandle, subgraphs, currentSubgraph);
  if (!pinTypesCompatible(sourcePinType, targetPinType)) return false;

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
