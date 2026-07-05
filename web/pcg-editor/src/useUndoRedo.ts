// useUndoRedo.ts — Basic undo/redo via snapshot stack for nodes, edges, and parameters.
// Cmd/Ctrl+Z → undo, Cmd/Ctrl+Shift+Z → redo.
// Drag operations can be merged into a single undo step via beginDrag/endDrag.

import { useCallback, useRef, useState } from 'react';
import type { Node, Edge } from '@xyflow/react';
import type { GraphParameter } from './graphSchema';

interface Snapshot {
  nodes: Node[];
  edges: Edge[];
  parameters: GraphParameter[];
}

interface UndoRedoState {
  past: Snapshot[];
  future: Snapshot[];
}

const MAX_HISTORY = 100;

export function useUndoRedo(
  nodes: Node[],
  edges: Edge[],
  parameters: GraphParameter[],
  setNodes: (nodes: Node[]) => void,
  setEdges: (edges: Edge[]) => void,
  setParameters: (params: GraphParameter[]) => void,
) {
  const [state, setState] = useState<UndoRedoState>({ past: [], future: [] });
  const dragRef = useRef(false);
  const suppressRef = useRef(false);

  const takeSnapshot = useCallback((): Snapshot => {
    return {
      nodes: nodes.map((n) => ({ ...n, data: { ...n.data } })),
      edges: edges.map((e) => ({ ...e })),
      parameters: parameters.map((p) => ({ ...p })),
    };
  }, [nodes, edges, parameters]);

  const commit = useCallback(
    (_actionName?: string) => {
      if (suppressRef.current) return;
      const snapshot = takeSnapshot();
      setState((prev) => ({
        past: [...prev.past.slice(-(MAX_HISTORY - 1)), snapshot],
        future: [],
      }));
    },
    [takeSnapshot],
  );

  const undo = useCallback(() => {
    setState((prev) => {
      if (prev.past.length === 0) return prev;
      const previous = prev.past[prev.past.length - 1];
      const current = takeSnapshot();

      suppressRef.current = true;
      setNodes(previous.nodes);
      setEdges(previous.edges);
      setParameters(previous.parameters);
      suppressRef.current = false;

      return {
        past: prev.past.slice(0, -1),
        future: [current, ...prev.future],
      };
    });
  }, [takeSnapshot, setNodes, setEdges, setParameters]);

  const redo = useCallback(() => {
    setState((prev) => {
      if (prev.future.length === 0) return prev;
      const next = prev.future[0];
      const current = takeSnapshot();

      suppressRef.current = true;
      setNodes(next.nodes);
      setEdges(next.edges);
      setParameters(next.parameters);
      suppressRef.current = false;

      return {
        past: [...prev.past, current],
        future: prev.future.slice(1),
      };
    });
  }, [takeSnapshot, setNodes, setEdges, setParameters]);

  const beginDrag = useCallback(() => {
    dragRef.current = true;
    commit();
  }, [commit]);

  const endDrag = useCallback(() => {
    dragRef.current = false;
  }, []);

  const canUndo = state.past.length > 0;
  const canRedo = state.future.length > 0;

  return { commit, undo, redo, beginDrag, endDrag, canUndo, canRedo };
}
