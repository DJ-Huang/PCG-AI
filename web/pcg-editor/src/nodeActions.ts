// nodeActions.ts — Hover-toolbar actions dispatched from node components up to App.
// React Flow renders node components itself, so callbacks arrive via context
// instead of props.

import { createContext } from 'react';

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
