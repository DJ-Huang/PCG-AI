// subgraphs.ts — Subgraph definition resolution for the Web editor.
// A `Subgraph` instance node references a document-level subgraph definition
// via data.subgraphId; title and pins derive from that definition, mirroring
// Unity's PcgSubgraphNodeView (instance inputs accept Any; outputs keep their
// declared pinType).

import { createContext, useContext } from 'react';
import type { Node } from '@xyflow/react';
import type { GraphSubgraph } from './graphSchema';
import {
  getInputPinType,
  getOutputPinType,
  type PinType,
} from './nodeManifest';

export const SubgraphsContext = createContext<GraphSubgraph[]>([]);

/** The subgraph definition currently open for editing (null = root graph). */
export const CurrentSubgraphContext = createContext<GraphSubgraph | null>(null);

export function useSubgraphs(): GraphSubgraph[] {
  return useContext(SubgraphsContext);
}

export function useCurrentSubgraph(): GraphSubgraph | null {
  return useContext(CurrentSubgraphContext);
}

export function getSubgraphId(data: unknown): string {
  if (!data || typeof data !== 'object' || Array.isArray(data)) return '';
  const value = (data as Record<string, unknown>).subgraphId;
  return typeof value === 'string' ? value : '';
}

export function findSubgraph(
  subgraphs: GraphSubgraph[],
  id: string,
): GraphSubgraph | undefined {
  return id ? subgraphs.find((s) => s.id === id) : undefined;
}

/** Structural interface nodes that live inside a subgraph definition. */
export function isSubgraphInterfaceNode(type: string | undefined): boolean {
  return type === 'SubgraphInput' || type === 'SubgraphOutput';
}

/** Instance display title: user rename (__nodeTitle) → definition name → id. */
export function getSubgraphNodeTitle(
  data: unknown,
  subgraph: GraphSubgraph | undefined,
): string {
  const custom =
    data && typeof data === 'object' && !Array.isArray(data)
      ? (data as Record<string, unknown>).__nodeTitle
      : undefined;
  if (typeof custom === 'string' && custom.trim()) return custom.trim();
  if (subgraph?.name) return subgraph.name;
  return getSubgraphId(data) || 'Subgraph';
}

/** Instance input pins accept any spatial payload (Unity PcgSubgraphInputUtility). */
export function getSubgraphInputPinType(): PinType {
  return 'Any';
}

/** Output pin type from the definition; unknown/empty handles fall back to Any (Unity parity). */
export function getSubgraphOutputPinType(
  subgraph: GraphSubgraph | undefined,
  handle: string,
): PinType {
  const port = subgraph?.outputs.find((p) => p.id === handle);
  const pinType = port?.pinType;
  return pinType ? (pinType as PinType) : 'Any';
}

/** Node-type-aware output pin lookup used by connection validation and previews. */
export function resolveOutputPinType(
  node: Node,
  handle: string,
  subgraphs: GraphSubgraph[],
  currentSubgraph: GraphSubgraph | null = null,
): PinType | undefined {
  if (node.type === 'Subgraph') {
    return getSubgraphOutputPinType(findSubgraph(subgraphs, getSubgraphId(node.data)), handle);
  }
  // Inside a subgraph, SubgraphInput's outputs are the definition's input pins
  // (Unity PcgSubgraphNodeView kind=Input).
  if (node.type === 'SubgraphInput' && currentSubgraph) {
    const pinType = currentSubgraph.inputs.find((p) => p.id === handle)?.pinType;
    return pinType ? (pinType as PinType) : 'Any';
  }
  return getOutputPinType(node.type ?? '', handle);
}

/** Node-type-aware input pin lookup used by connection validation. */
export function resolveInputPinType(
  node: Node,
  handle: string,
  _subgraphs: GraphSubgraph[] = [],
  currentSubgraph: GraphSubgraph | null = null,
): PinType | undefined {
  if (node.type === 'Subgraph') {
    return getSubgraphInputPinType();
  }
  // Inside a subgraph, SubgraphOutput's inputs are the definition's output pins.
  if (node.type === 'SubgraphOutput' && currentSubgraph) {
    const pinType = currentSubgraph.outputs.find((p) => p.id === handle)?.pinType;
    return pinType ? (pinType as PinType) : 'Any';
  }
  return getInputPinType(node.type ?? '', handle);
}
