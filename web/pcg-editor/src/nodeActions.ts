// nodeActions.ts — Hover-toolbar actions dispatched from node components up to App.
// React Flow renders node components itself, so callbacks arrive via context
// instead of props.

import { createContext, useSyncExternalStore } from 'react';

export interface NodeActions {
  /** Toggle the pinned Node Info panel for a node. */
  onInfo: (nodeId: string) => void;
  /** Cook and preview this node's output in the preview viewport. */
  onPreview: (nodeId: string) => void;
}

export const NodeActionsContext = createContext<NodeActions>({
  onInfo: () => {},
  onPreview: () => {},
});

// Preview-target id lives outside React context: broadcasting it through context
// re-rendered every node on each ▶ click (context propagation ignores memo).
// With useSyncExternalStore each node snapshots a boolean — only the old and new
// target nodes flip and re-render.
let previewTargetId: string | null = null;
const previewTargetListeners = new Set<() => void>();

/** Node currently driving the 3D preview (null = full-graph preview). */
export function getPreviewTargetId(): string | null {
  return previewTargetId;
}

export function setPreviewTargetId(next: string | null): void {
  if (next === previewTargetId) return;
  previewTargetId = next;
  for (const listener of previewTargetListeners) listener();
}

function subscribePreviewTarget(listener: () => void): () => void {
  previewTargetListeners.add(listener);
  return () => {
    previewTargetListeners.delete(listener);
  };
}

/** App-level subscription: re-renders whenever the preview target changes. */
export function usePreviewTargetId(): string | null {
  return useSyncExternalStore(subscribePreviewTarget, getPreviewTargetId);
}

/** Per-node subscription: re-renders only when THIS node's target state flips. */
export function useIsPreviewTarget(nodeId: string): boolean {
  return useSyncExternalStore(subscribePreviewTarget, () => getPreviewTargetId() === nodeId);
}
