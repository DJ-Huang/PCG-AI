// groupResolver.ts — Resolve available groups from upstream SpatialMesh connections.
// Walks the graph backwards to collect all named groups produced by upstream nodes,
// similar to how Houdini populates group dropdowns from upstream geometry.

import type { Node, Edge } from '@xyflow/react';
import { getNodeTypeDefs, type GroupDomain } from './nodeManifest';

export interface AvailableGroup {
  name: string;
  domain: GroupDomain;
  source: string;       // node ID
  sourceType: string;   // node type
  label?: string;
}

/**
 * Finds all upstream nodes connected via SpatialMesh pins, walking recursively.
 * Returns nodes in topological order (closest first).
 */
function findUpstreamMeshNodes(
  startNodeId: string,
  nodes: Node[],
  edges: Edge[],
  visited: Set<string> = new Set(),
): Node[] {
  const result: Node[] = [];

  // Find edges that target this node
  const incomingEdges = edges.filter((e) => e.target === startNodeId);

  for (const edge of incomingEdges) {
    const sourceNode = nodes.find((n) => n.id === edge.source);
    if (!sourceNode || visited.has(sourceNode.id)) continue;

    // Check if this edge carries SpatialMesh data
    const sourceDef = getNodeTypeDefs(sourceNode.type ?? '');
    if (!sourceDef) continue;

    const outputPin = sourceDef.outputs.find((p) => p.id === edge.sourceHandle);
    if (!outputPin || outputPin.pinType !== 'SpatialMesh') continue;

    visited.add(sourceNode.id);
    result.push(sourceNode);

    // Recurse upstream
    const further = findUpstreamMeshNodes(sourceNode.id, nodes, edges, visited);
    result.push(...further);
  }

  return result;
}

/**
 * Resolves all available groups from upstream SpatialMesh connections.
 * Collects both static groups (from manifest outputGroups) and dynamic groups
 * (from node data properties marked isGroupOutput).
 */
export function resolveUpstreamGroups(
  nodeId: string,
  nodes: Node[],
  edges: Edge[],
): AvailableGroup[] {
  const upstream = findUpstreamMeshNodes(nodeId, nodes, edges);
  const groups: AvailableGroup[] = [];
  const seen = new Set<string>();

  for (const node of upstream) {
    const def = getNodeTypeDefs(node.type ?? '');
    if (!def) continue;
    const data = node.data as Record<string, unknown>;

    // 1. Static output groups from manifest (e.g. SweepAlongSpline)
    if (def.outputGroups) {
      for (const og of def.outputGroups) {
        // Check condition property (e.g. capStart must be true)
        if (og.condition) {
          const condValue = data[og.condition];
          if (!condValue) continue;
        }

        let groupName = og.name;
        let isDynamic = false;

        // Dynamic: group name comes from a property value
        if (og.dynamic) {
          const propValue = data[og.name];
          if (typeof propValue === 'string' && propValue.trim()) {
            groupName = propValue;
            isDynamic = true;
          } else {
            continue; // No group name set
          }
        }

        // Deduplicate by name+domain (first upstream wins)
        const key = `${groupName}:${og.domain}`;
        if (seen.has(key)) continue;
        seen.add(key);

        groups.push({
          name: groupName,
          domain: og.domain,
          source: node.id,
          sourceType: node.type ?? '',
          label: isDynamic ? undefined : og.label,
        });
      }
    }

    // 2. Dynamic output groups from properties with isGroupOutput flag
    if (!def.outputGroups || def.outputGroups.length === 0) {
      for (const [key, prop] of Object.entries(def.properties)) {
        if (!prop.isGroupOutput) continue;

        const groupName = data[key];
        if (typeof groupName !== 'string' || !groupName.trim()) continue;

        const domain = prop.groupDomain ?? 'edge';
        const dedupKey = `${groupName}:${domain}`;
        if (seen.has(dedupKey)) continue;
        seen.add(dedupKey);

        groups.push({
          name: groupName,
          domain,
          source: node.id,
          sourceType: node.type ?? '',
        });
      }
    }
  }

  return groups;
}

/**
 * Filters available groups by domain.
 */
export function filterGroupsByDomain(
  groups: AvailableGroup[],
  domain?: GroupDomain,
): AvailableGroup[] {
  if (!domain) return groups;
  return groups.filter((g) => g.domain === domain);
}
