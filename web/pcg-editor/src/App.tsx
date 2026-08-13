// App.tsx — PCG Graph Editor (Web) with three-panel layout.
// Features: manifest-driven nodes, Inspector, Blackboard, port drag-to-search,
// node search, Copy Raw Data, undo/redo, keyboard shortcuts.

import { useCallback, useRef, useState, useEffect, useMemo, type ChangeEvent } from 'react';
import {
  ReactFlow,
  Background,
  BackgroundVariant,
  Controls,
  MiniMap,
  ReactFlowProvider,
  addEdge,
  applyNodeChanges,
  applyEdgeChanges,
  useNodesState,
  useEdgesState,
  useReactFlow,
  useViewport,
  type Connection,
  type Node,
  type Edge,
  type NodeChange,
  type EdgeChange,
  type NodeMouseHandler,
} from '@xyflow/react';
import '@xyflow/react/dist/style.css';

import ManifestNode from './nodes/ManifestNode';
import SubgraphNode, { SubgraphInterfaceNode } from './nodes/SubgraphNode';
import { defaultData } from './graphSchema';
import type { GraphParameter, GraphSubgraph, GraphNode, GraphEdge } from './graphSchema';
import { exportGraph, downloadGraph, exportToSchema, saveGraphToFile, revealInFinder } from './exportGraph';
import { importGraphFromFile, parseGraphJson, syncNodeCounterFromNodes } from './importGraph';
import { clearEditorSession, loadEditorSession, saveEditorSession } from './editorSession';
import { isValidConnection } from './connectionValidation';
import {
  SubgraphsContext,
  CurrentSubgraphContext,
  findSubgraph,
  getSubgraphId,
  isSubgraphInterfaceNode,
  resolveInputPinType,
  resolveOutputPinType,
} from './subgraphs';
import {
  getNodeTypeDefs,
  getAllNodeTypes,
  getNodeManifest,
  validateNodePropertyValue,
  type ManifestProperty,
  type PinType,
} from './nodeManifest';
import { useUndoRedo } from './useUndoRedo';
import Blackboard from './Blackboard';
import AgentPanel from './agent/AgentPanel';
import { dispatchAgentActions, type AgentAction, type AgentGraphOps } from './agent/agentCommands';
import Inspector from './Inspector';
import NodeInfoPanel from './NodeInfoPanel';
import NodeSearchPanel, { type SearchPanelConfig } from './NodeSearchPanel';
import PreviewViewport, { type PreviewViewportHandle, type SplineEditContext } from './PreviewViewport';
import SettingsDialog from './SettingsDialog';
import { useEditorBridge } from './editorBridge';
import {
  applyGraphOperations,
  parseAndValidateGraph,
  type GraphCommandResult,
  type QueuedGraphCommand,
} from './graphCommands';
import { cookGraphPreview, cancelCook, checkCookServer, buildPreviewCookGraph, buildSubgraphCookGraph, prepareGraphForPreviewCook, newPreviewJobId, type PreviewData } from './previewCook';
import {
  applyPreviewParameterOverrides,
  resolvePreviewParameterValues,
  savePreviewParameterDefaults,
  type PreviewParameterValue,
  type PreviewParameterValues,
} from './previewParameters';
import { NodeActionsContext, getPreviewTargetId, setPreviewTargetId, usePreviewTargetId } from './nodeActions';
import {
  getEffectiveControlPoints,
  isSplineAuthoringNode,
  serializeControlPoints,
} from './splineControlPoints';
import './App.css';

// Map every manifest node type to the generic ManifestNode component.
// Using { default: ManifestNode } alone causes React Flow to pass type='default'
// instead of the real type, so nodes render as "Unknown".
const nodeTypes = {
  ...Object.fromEntries(getAllNodeTypes().map((def) => [def.type, ManifestNode])),
  // Subgraph instance pins derive from the referenced definition (see
  // SubgraphNode); nested contents are preserved by import/export.
  Subgraph: SubgraphNode,
  SubgraphInput: SubgraphInterfaceNode,
  SubgraphOutput: SubgraphInterfaceNode,
};

// React Flow default minZoom is 0.5 — too high for dense PCG graphs.
const GRAPH_MIN_ZOOM = 0.05;
const GRAPH_MAX_ZOOM = 2;
const GRAPH_FIT_VIEW_OPTIONS = {
  minZoom: GRAPH_MIN_ZOOM,
  maxZoom: GRAPH_MAX_ZOOM,
  padding: 0.15,
};

const initialNodes: Node[] = [
  {
    id: 'n1',
    type: 'SpawnPoints',
    position: { x: 50, y: 150 },
    data: { ...defaultData('SpawnPoints') },
  },
  {
    id: 'n2',
    type: 'PlaceInScene',
    position: { x: 400, y: 150 },
    data: { ...defaultData('PlaceInScene') },
  },
];

const initialEdges: Edge[] = [
  { id: 'e1', source: 'n1', target: 'n2', sourceHandle: 'out', targetHandle: 'in' },
];

let nodeCounter = 100;

function allocateNodeId(existingNodes: readonly Node[]): string {
  // Module state can reset during Vite HMR while React preserves the live graph.
  // Always resync against the active scope before allocating to avoid duplicate keys.
  nodeCounter = Math.max(nodeCounter, syncNodeCounterFromNodes([...existingNodes]));
  const existingIds = new Set(existingNodes.map((node) => node.id));
  let candidate: string;
  do {
    candidate = `n${++nodeCounter}`;
  } while (existingIds.has(candidate));
  return candidate;
}

function restoreEditorSession() {
  const session = loadEditorSession();
  if (!session) return null;
  const result = parseGraphJson(JSON.stringify(session.graph));
  if (!result.ok) {
    clearEditorSession();
    return null;
  }
  nodeCounter = session.nodeCounter;
  return {
    nodes: result.nodes,
    edges: result.edges,
    parameters: result.parameters,
    subgraphs: result.subgraphs,
    filename: session.filename,
  };
}

function PcgEditor() {
  const restoredRef = useRef(restoreEditorSession());
  const editorSessionIdRef = useRef(crypto.randomUUID());
  const restored = restoredRef.current;

  const [nodes, setNodes, onNodesChange] = useNodesState(restored?.nodes ?? initialNodes);
  const [edges, setEdges, onEdgesChange] = useEdgesState(restored?.edges ?? initialEdges);
  const [parameters, setParameters] = useState<GraphParameter[]>(restored?.parameters ?? []);
  const [subgraphs, setSubgraphs] = useState<GraphSubgraph[]>(restored?.subgraphs ?? []);
  const [selectedNode, setSelectedNode] = useState<Node | null>(null);
  const [showBlackboard, setShowBlackboard] = useState(false);
  const [showAgent, setShowAgent] = useState(true);
  const [showSettings, setShowSettings] = useState(false);
  const [providerRevision, setProviderRevision] = useState(0);
  const [showInspector, setShowInspector] = useState(false);
  const [showPreview, setShowPreview] = useState(true);
  const [previewData, setPreviewData] = useState<PreviewData | null>(null);
  const [previewLoading, setPreviewLoading] = useState(false);
  const [previewError, setPreviewError] = useState<string | null>(null);
  const [previewParameterValuesByScope, setPreviewParameterValuesByScope] = useState<Record<string, PreviewParameterValues>>({});
  const previewAbortRef = useRef<AbortController | null>(null);
  const previewJobIdRef = useRef<string | null>(null);
  const previewViewportRef = useRef<PreviewViewportHandle>(null);
  const [status, setStatus] = useState(
    restored
      ? `Restored last session${restored.filename ? `: ${restored.filename}` : ''} (${restored.nodes.length} nodes)`
      : '',
  );
  const [searchConfig, setSearchConfig] = useState<SearchPanelConfig | null>(null);
  const [contextMenu, setContextMenu] = useState<{ x: number; y: number; nodeId: string } | null>(null);
  const [currentFilename, setCurrentFilename] = useState<string>(restored?.filename ?? '');
  const [infoNodeId, setInfoNodeId] = useState<string | null>(null);
  const previewTargetNodeId = usePreviewTargetId();

  const openSettings = useCallback(() => setShowSettings(true), []);
  const closeSettings = useCallback(() => setShowSettings(false), []);

  const fileInputRef = useRef<HTMLInputElement>(null);
  const connectingNodeId = useRef<string | null>(null);
  const connectingHandleId = useRef<string | null>(null);
  const connectingHandleType = useRef<'source' | 'target' | null>(null);
  const { screenToFlowPosition, flowToScreenPosition, fitView } = useReactFlow();
  // Subscribe to viewport so the pinned info panel re-anchors on pan/zoom.
  useViewport();

  const { commit, undo, redo, beginDrag, endDrag, canUndo, canRedo } = useUndoRedo(
    nodes,
    edges,
    parameters,
    subgraphs,
    setNodes,
    setEdges,
    setParameters,
    setSubgraphs,
  );

  // ── Subgraph navigation (nested editing) ────────────
  // Single document state: root nodes/edges/parameters plus subgraphs (whose
  // defs carry their own interior nodes/edges/parameters). editPath selects
  // which graph the canvas shows; every mutation routes to the owning slice,
  // so undo/redo, export, and session persist always see the whole document.

  const [editPath, setEditPath] = useState<string[]>([]);
  const currentSubgraphId = editPath.length > 0 ? editPath[editPath.length - 1] : null;
  const currentSubgraph = useMemo(
    () => (currentSubgraphId ? findSubgraph(subgraphs, currentSubgraphId) ?? null : null),
    [subgraphs, currentSubgraphId],
  );

  const viewNodes = useMemo(
    () => (currentSubgraph ? currentSubgraph.nodes : nodes) as Node[],
    [currentSubgraph, nodes],
  );
  const viewEdges = useMemo(
    () => (currentSubgraph ? currentSubgraph.edges : edges) as Edge[],
    [currentSubgraph, edges],
  );
  const viewParameters = useMemo(
    () => (currentSubgraph ? (currentSubgraph.parameters ?? []) : parameters),
    [currentSubgraph, parameters],
  );
  const previewParameterScope = currentSubgraphId ? `subgraph:${currentSubgraphId}` : 'root';
  const previewParameterValues = useMemo(
    () => resolvePreviewParameterValues(viewParameters, previewParameterValuesByScope[previewParameterScope]),
    [viewParameters, previewParameterValuesByScope, previewParameterScope],
  );
  const previewParameterNodeIds = useMemo(
    () => new Set(viewNodes.map((node) => node.id)),
    [viewNodes],
  );

  /** Routes a nodes update to the root graph or the open subgraph definition. */
  const setViewNodes = useCallback(
    (updater: (nds: Node[]) => Node[]) => {
      if (!currentSubgraphId) {
        setNodes(updater);
        return;
      }
      setSubgraphs((subs) =>
        subs.map((sg) =>
          sg.id === currentSubgraphId
            ? { ...sg, nodes: updater(sg.nodes as unknown as Node[]) as unknown as GraphNode[] }
            : sg,
        ),
      );
    },
    [currentSubgraphId, setNodes, setSubgraphs],
  );

  const setViewEdges = useCallback(
    (updater: (eds: Edge[]) => Edge[]) => {
      if (!currentSubgraphId) {
        setEdges(updater);
        return;
      }
      setSubgraphs((subs) =>
        subs.map((sg) =>
          sg.id === currentSubgraphId
            ? { ...sg, edges: updater(sg.edges as unknown as Edge[]) as unknown as GraphEdge[] }
            : sg,
        ),
      );
    },
    [currentSubgraphId, setEdges, setSubgraphs],
  );

  const setViewParameters = useCallback(
    (next: GraphParameter[]) => {
      if (!currentSubgraphId) {
        setParameters(next);
        return;
      }
      setSubgraphs((subs) =>
        subs.map((sg) => (sg.id === currentSubgraphId ? { ...sg, parameters: next } : sg)),
      );
    },
    [currentSubgraphId, setParameters, setSubgraphs],
  );

  const updatePreviewParameterValue = useCallback(
    (parameterId: string, value: PreviewParameterValue) => {
      setPreviewParameterValuesByScope((stored) => ({
        ...stored,
        [previewParameterScope]: {
          ...resolvePreviewParameterValues(viewParameters, stored[previewParameterScope]),
          [parameterId]: value,
        },
      }));
    },
    [previewParameterScope, viewParameters],
  );

  const resetPreviewParameters = useCallback(() => {
    setPreviewParameterValuesByScope((stored) => ({
      ...stored,
      [previewParameterScope]: resolvePreviewParameterValues(viewParameters, undefined),
    }));
    setStatus('Preview parameters reset to defaults');
  }, [previewParameterScope, viewParameters]);

  const savePreviewParametersAsDefaults = useCallback(() => {
    const saved = savePreviewParameterDefaults(
      viewNodes as unknown as GraphNode[],
      viewParameters,
      previewParameterValues,
    );
    commit();
    setViewNodes(() => saved.nodes as unknown as Node[]);
    setViewParameters(saved.parameters);
    setSelectedNode((selected) => {
      if (!selected) return selected;
      return (saved.nodes.find((node) => node.id === selected.id) as unknown as Node | undefined) ?? selected;
    });
    setStatus('Preview parameter values saved as defaults');
  }, [viewNodes, viewParameters, previewParameterValues, commit, setViewNodes, setViewParameters]);

  // Deletions flow through onNodesChange/onEdgesChange (not commit()-wrapped
  // callers), so commit here on remove changes. React Flow fires the node
  // remove and its cascade edge removes in the same tick — dedupe by timestamp
  // so one Delete keypress produces one undo step.
  const removeCommitRef = useRef(0);
  const commitForRemove = useCallback(() => {
    const now = performance.now();
    if (now - removeCommitRef.current < 50) return;
    removeCommitRef.current = now;
    commit();
  }, [commit]);

  const onViewNodesChange = useCallback(
    (changes: NodeChange[]) => {
      if (!currentSubgraphId) {
        if (changes.some((c) => c.type === 'remove')) commitForRemove();
        onNodesChange(changes);
        return;
      }
      // Interface nodes (SubgraphInput/Output) are structural — not deletable.
      const filtered = changes.filter((c) => {
        if (c.type !== 'remove') return true;
        const node = currentSubgraph?.nodes.find((n) => n.id === c.id);
        return !isSubgraphInterfaceNode(node?.type);
      });
      if (filtered.length === 0) return;
      if (filtered.some((c) => c.type === 'remove')) commitForRemove();
      setSubgraphs((subs) =>
        subs.map((sg) =>
          sg.id === currentSubgraphId
            ? {
                ...sg,
                nodes: applyNodeChanges(filtered, sg.nodes as unknown as Node[]) as unknown as GraphNode[],
              }
            : sg,
        ),
      );
    },
    [currentSubgraphId, currentSubgraph, onNodesChange, setSubgraphs, commitForRemove],
  );

  const onViewEdgesChange = useCallback(
    (changes: EdgeChange[]) => {
      if (!currentSubgraphId) {
        if (changes.some((c) => c.type === 'remove')) commitForRemove();
        onEdgesChange(changes);
        return;
      }
      // React Flow cascades node deletion to connected edges. When the node is
      // a protected interface node, the cascade must be blocked too — cascade
      // removes arrive unselected, while a user-deleted edge is selected first.
      const filtered = changes.filter((c) => {
        if (c.type !== 'remove') return true;
        const edge = currentSubgraph?.edges.find((e) => e.id === c.id);
        if (!edge) return true;
        const selected = (edge as Edge).selected === true;
        if (selected) return true;
        const sourceNode = currentSubgraph?.nodes.find((n) => n.id === edge.source);
        const targetNode = currentSubgraph?.nodes.find((n) => n.id === edge.target);
        return !isSubgraphInterfaceNode(sourceNode?.type) && !isSubgraphInterfaceNode(targetNode?.type);
      });
      if (filtered.length === 0) return;
      if (filtered.some((c) => c.type === 'remove')) commitForRemove();
      setSubgraphs((subs) =>
        subs.map((sg) =>
          sg.id === currentSubgraphId
            ? {
                ...sg,
                edges: applyEdgeChanges(filtered, sg.edges as unknown as Edge[]) as unknown as GraphEdge[],
              }
            : sg,
        ),
      );
    },
    [currentSubgraphId, currentSubgraph, onEdgesChange, setSubgraphs, commitForRemove],
  );

  // Navigation is view state, not a document change — no undo commit.
  const navigateTo = useCallback(
    (path: string[]) => {
      setEditPath(path);
      setSelectedNode(null);
      setInfoNodeId(null);
      setContextMenu(null);
      setPreviewTargetId(null);
      window.setTimeout(() => void fitView({ ...GRAPH_FIT_VIEW_OPTIONS, duration: 200 }), 50);
    },
    [fitView],
  );

  const enterSubgraph = useCallback(
    (subgraphId: string) => {
      if (!subgraphs.some((s) => s.id === subgraphId)) {
        setStatus(`Cannot enter: subgraph "${subgraphId || '(unset)'}" is missing from this document`);
        return;
      }
      navigateTo([...editPath, subgraphId]);
    },
    [editPath, subgraphs, navigateTo],
  );

  // ── Preview picking: executor ids are flattened from the root
  // ("<instanceId>/<innerId>"), so resolve from the root scope regardless of
  // which subgraph is currently open, then select + center the target node.
  const handlePreviewPickNode = useCallback(
    (flatId: string) => {
      const segments = flatId.split('/').filter(Boolean);
      if (segments.length === 0) return;

      // Subgraph-interior cooks (editor inside a subgraph) emit unprefixed ids
      // scoped to the open subgraph — resolve those against the current view.
      let scopeNodes: Node[] =
        segments.length === 1 && currentSubgraph ? viewNodes : (nodes as Node[]);
      const path: string[] =
        segments.length === 1 && currentSubgraph ? [...editPath] : [];
      for (let i = 0; i < segments.length - 1; i++) {
        const instance = scopeNodes.find((n) => n.id === segments[i]);
        const sgId = instance?.type === 'Subgraph' ? getSubgraphId(instance.data) : null;
        const def = sgId ? findSubgraph(subgraphs, sgId) : null;
        if (!def) {
          setStatus(`Pick: cannot resolve subgraph path in "${flatId}"`);
          return;
        }
        path.push(sgId!);
        scopeNodes = def.nodes as unknown as Node[];
      }
      const targetId = segments[segments.length - 1];
      const target = scopeNodes.find((n) => n.id === targetId);
      if (!target) {
        setStatus(`Pick: node "${targetId}" no longer exists`);
        return;
      }

      const samePath =
        editPath.length === path.length && editPath.every((id, i) => id === path[i]);
      if (!samePath) {
        setEditPath(path);
        setInfoNodeId(null);
        setContextMenu(null);
        setPreviewTargetId(null);
      }

      const markSelected = (nds: Node[]): Node[] =>
        nds.map((n) => ({ ...n, selected: n.id === targetId }));
      if (path.length === 0) {
        setNodes((nds) => markSelected(nds as Node[]));
      } else {
        const scopeId = path[path.length - 1];
        setSubgraphs((subs) =>
          subs.map((sg) =>
            sg.id === scopeId
              ? { ...sg, nodes: markSelected(sg.nodes as unknown as Node[]) as unknown as GraphNode[] }
              : sg,
          ),
        );
      }
      setSelectedNode(target);
      window.setTimeout(
        () => void fitView({ nodes: [{ id: targetId }], duration: 300, padding: 0.4, maxZoom: 1.5 }),
        80,
      );
      setStatus(`Picked ${target.type ?? 'node'} "${targetId}"`);
    },
    [nodes, subgraphs, editPath, currentSubgraph, viewNodes, fitView, setNodes, setSubgraphs],
  );

  const onNodeDoubleClick: NodeMouseHandler = useCallback(
    (_, node) => {
      if (node.type !== 'Subgraph') return;
      const id = getSubgraphId(node.data);
      if (id) enterSubgraph(id);
    },
    [enterSubgraph],
  );

  // Undo can remove the definition being edited — bail back to root.
  useEffect(() => {
    if (editPath.length > 0 && !editPath.every((id) => subgraphs.some((s) => s.id === id))) {
      setEditPath([]);
    }
  }, [editPath, subgraphs]);

  // ── Connection ──────────────────────────────────────

  const validateConnection = useCallback(
    (conn: Connection | Edge) => isValidConnection(conn, viewNodes, viewEdges, subgraphs, currentSubgraph),
    [viewNodes, viewEdges, subgraphs, currentSubgraph],
  );

  const onConnect = useCallback(
    (conn: Connection) => {
      if (!validateConnection(conn)) return;
      commit();
      setViewEdges((eds) => addEdge(conn, eds));
    },
    [setViewEdges, validateConnection, commit],
  );

  const onConnectStart = useCallback((_: unknown, { nodeId, handleId, handleType }: { nodeId: string | null; handleId: string | null; handleType: string | null }) => {
    connectingNodeId.current = nodeId;
    connectingHandleId.current = handleId;
    connectingHandleType.current = handleType as 'source' | 'target' | null;
  }, []);

  const onConnectEnd = useCallback(
    (event: MouseEvent | TouchEvent) => {
      // Only open search if connection didn't land on a valid port
      const target = (event as MouseEvent).target as HTMLElement;
      if (target && target.classList.contains('react-flow__handle')) {
        return; // Landed on a port, let onConnect handle it
      }

      const nodeId = connectingNodeId.current;
      const handleId = connectingHandleId.current;
      const handleType = connectingHandleType.current;

      if (!nodeId || !handleId || !handleType) return;

      // Find the node and its pin type
      const node = viewNodes.find((n) => n.id === nodeId);
      if (!node?.type) return;

      let pinType: PinType | undefined;
      if (handleType === 'source') {
        pinType = resolveOutputPinType(node, handleId, subgraphs, currentSubgraph);
      } else {
        pinType = resolveInputPinType(node, handleId, subgraphs, currentSubgraph);
      }
      if (!pinType) return;

      const clientX = 'clientX' in event ? event.clientX : event.touches?.[0]?.clientX ?? 0;
      const clientY = 'clientY' in event ? event.clientY : event.touches?.[0]?.clientY ?? 0;

      setSearchConfig({
        x: clientX,
        y: clientY,
        filterPinType: pinType,
        isSourcePort: handleType === 'source',
        onSelect: (nodeType) => {
          setSearchConfig(null);
          if (!nodeType) return;

          // Create new node at flow position
          const flowPos = screenToFlowPosition({ x: clientX, y: clientY });
          const newId = allocateNodeId(viewNodes);
          const newNode: Node = {
            id: newId,
            type: nodeType,
            position: flowPos,
            data: { ...defaultData(nodeType) },
          };
          commit();
          setViewNodes((nds) => [...nds, newNode]);

          // Auto-connect
          let conn: Connection;
          if (handleType === 'source') {
            // Source port → new node's first compatible input
            const def = getNodeTypeDefs(nodeType);
            const inputPin = def?.inputs[0];
            if (inputPin) {
              conn = {
                source: nodeId,
                target: newId,
                sourceHandle: handleId,
                targetHandle: inputPin.id,
              };
              if (validateConnection(conn)) {
                setViewEdges((eds) => addEdge(conn, eds));
              }
            }
          } else {
            // Target port ← new node's first compatible output
            const def = getNodeTypeDefs(nodeType);
            const outputPin = def?.outputs[0];
            if (outputPin) {
              conn = {
                source: newId,
                target: nodeId,
                sourceHandle: outputPin.id,
                targetHandle: handleId,
              };
              if (validateConnection(conn)) {
                setViewEdges((eds) => addEdge(conn, eds));
              }
            }
          }
        },
      });

      // Reset
      connectingNodeId.current = null;
      connectingHandleId.current = null;
      connectingHandleType.current = null;
    },
    [viewNodes, subgraphs, currentSubgraph, setViewNodes, setViewEdges, screenToFlowPosition, commit, validateConnection],
  );

  // ── Node creation ───────────────────────────────────

  const createNodeAt = useCallback(
    (nodeType: string, x: number, y: number) => {
      const flowPos = screenToFlowPosition({ x, y });
      const newId = allocateNodeId(viewNodes);
      const newNode: Node = {
        id: newId,
        type: nodeType,
        position: flowPos,
        data: { ...defaultData(nodeType) },
      };
      commit();
      setViewNodes((nds) => [...nds, newNode]);
      setStatus(`Added ${nodeType}`);
    },
    [viewNodes, setViewNodes, screenToFlowPosition, commit],
  );

  const openSearchAt = useCallback((x: number, y: number) => {
    setSearchConfig({
      x,
      y,
      onSelect: (nodeType) => {
        setSearchConfig(null);
        if (nodeType) createNodeAt(nodeType, x, y);
      },
    });
  }, [createNodeAt]);

  // ── Selection ───────────────────────────────────────

  const onNodeClick: NodeMouseHandler = useCallback((_, node) => {
    setSelectedNode(node);
  }, []);

  // ── Node hover toolbar actions (info / per-node preview) ──

  const handleNodeInfo = useCallback((nodeId: string) => {
    setInfoNodeId((prev) => (prev === nodeId ? null : nodeId));
  }, []);

  const onPaneClick = useCallback(() => {
    setSelectedNode(null);
    setContextMenu(null);
    setInfoNodeId(null);
  }, []);

  // ── Node data update (from Inspector) ──────────────

  const updateNodeData = useCallback(
    (nodeId: string, patch: Record<string, unknown>) => {
      commit();
      setViewNodes((nds) =>
        nds.map((n) =>
          n.id === nodeId ? { ...n, data: { ...n.data, ...patch } } : n,
        ),
      );
      // Update selectedNode ref
      setSelectedNode((prev) =>
        prev?.id === nodeId ? { ...prev, data: { ...prev.data, ...patch } } : prev,
      );
    },
    [setViewNodes, commit],
  );

  // ── Agent graph ops (actions dispatched from the agent panel) ──

  const agentOps = useMemo<AgentGraphOps>(
    () => ({
      addNode: (nodeType, position) => {
        if (!getNodeTypeDefs(nodeType)) {
          throw new Error(`unknown node type "${nodeType}"`);
        }
        const newId = allocateNodeId(viewNodes);
        const pos = position ?? screenToFlowPosition({
          x: window.innerWidth / 2,
          y: window.innerHeight / 2,
        });
        setViewNodes((nds) => [
          ...nds,
          { id: newId, type: nodeType, position: pos, data: { ...defaultData(nodeType) } },
        ]);
        return newId;
      },
      connectNodes: (source, target, sourceHandle, targetHandle) => {
        const sourceNode = viewNodes.find((n) => n.id === source);
        const targetNode = viewNodes.find((n) => n.id === target);
        if (!sourceNode) throw new Error(`source node "${source}" not found`);
        if (!targetNode) throw new Error(`target node "${target}" not found`);
        const outPin = getNodeTypeDefs(sourceNode.type ?? '')?.outputs[0];
        const inPin = getNodeTypeDefs(targetNode.type ?? '')?.inputs[0];
        const conn: Connection = {
          source,
          target,
          sourceHandle: sourceHandle ?? outPin?.id ?? null,
          targetHandle: targetHandle ?? inPin?.id ?? null,
        };
        if (!validateConnection(conn)) {
          throw new Error(`invalid connection ${source} → ${target}`);
        }
        setViewEdges((eds) => addEdge(conn, eds));
      },
      setNodeParam: (nodeId, key, value) => {
        if (!viewNodes.some((n) => n.id === nodeId)) {
          throw new Error(`node "${nodeId}" not found`);
        }
        setViewNodes((nds) =>
          nds.map((n) => (n.id === nodeId ? { ...n, data: { ...n.data, [key]: value } } : n)),
        );
        setSelectedNode((prev) =>
          prev?.id === nodeId ? { ...prev, data: { ...prev.data, [key]: value } } : prev,
        );
      },
      patchNode: (nodeId, patch) => {
        const node = viewNodes.find((candidate) => candidate.id === nodeId);
        if (!node) {
          throw new Error(`node "${nodeId}" not found`);
        }
        for (const [key, value] of Object.entries(patch)) {
          const error = validateNodePropertyValue(node.type ?? '', key, value);
          if (error) throw new Error(error);
        }
        setViewNodes((nds) =>
          nds.map((n) => (n.id === nodeId ? { ...n, data: { ...n.data, ...patch } } : n)),
        );
        setSelectedNode((prev) =>
          prev?.id === nodeId ? { ...prev, data: { ...prev.data, ...patch } } : prev,
        );
      },
    }),
    [viewNodes, setViewNodes, setViewEdges, screenToFlowPosition, validateConnection],
  );

  const applyAgentActions = useCallback(
    (actions: AgentAction[]) => dispatchAgentActions(actions, agentOps, commit),
    [agentOps, commit],
  );

  const bridgeGraph = useMemo(
    () => exportGraph(nodes, edges, parameters, subgraphs),
    [nodes, edges, parameters, subgraphs],
  );
  const installBridgeGraph = useCallback(
    (parsed: ReturnType<typeof parseAndValidateGraph> & { ok: true }, nextEditPath: string[]) => {
      commit('PCG MCP graph edit');
      nodeCounter = syncNodeCounterFromNodes(parsed.parsed.nodes);
      setNodes(parsed.parsed.nodes);
      setEdges(parsed.parsed.edges);
      setParameters(parsed.parsed.parameters);
      setSubgraphs(parsed.parsed.subgraphs);
      setEditPath(nextEditPath);
      setSelectedNode(null);
      setInfoNodeId(null);
      setContextMenu(null);
      setPreviewTargetId(null);
      setPreviewParameterValuesByScope({});
    },
    [commit, setNodes, setEdges],
  );
  const applyBridgeCommands = useCallback(
    async (commands: QueuedGraphCommand[]): Promise<GraphCommandResult[]> => {
      const results: GraphCommandResult[] = [];
      for (const command of commands) {
        try {
          if (command.type === 'saveGraph') {
            const targetPath = command.path ?? currentFilename;
            if (!targetPath) throw new Error('save path is required for an unnamed graph');
            const saved = await saveGraphToFile(
              bridgeGraph,
              targetPath,
            );
            if (!saved.ok) throw new Error(saved.error ?? 'unknown save error');
            if (command.path !== undefined) setCurrentFilename(targetPath);
            setStatus(`Saved to ${targetPath} through PCG MCP`);
            results.push({ id: command.id, ok: true, detail: { path: targetPath } });
            continue;
          }

          if (command.type === 'replaceGraph') {
            const validated = parseAndValidateGraph(command.graph);
            if (!validated.ok) throw new Error(validated.error);
            installBridgeGraph(validated, []);
            setStatus(`PCG MCP replaced graph (${validated.parsed.nodes.length} root nodes)`);
            results.push({
              id: command.id,
              ok: true,
              detail: {
                rootNodes: validated.parsed.nodes.length,
                rootEdges: validated.parsed.edges.length,
                subgraphs: validated.parsed.subgraphs.length,
              },
            });
            continue;
          }

          const operations = command.type === 'setNodeParams'
            ? [{ op: 'patch_node' as const, nodeId: command.nodeId, patch: command.patch }]
            : command.operations;
          const applied = applyGraphOperations(bridgeGraph, editPath, operations);
          if (!applied.ok) throw new Error(applied.error);
          installBridgeGraph({ ok: true, parsed: applied.parsed }, editPath);
          setStatus(`PCG MCP applied ${operations.length} graph operation(s)`);
          results.push({ id: command.id, ok: true, detail: applied.detail });
        } catch (error) {
          const message = error instanceof Error ? error.message : String(error);
          setStatus(`PCG MCP command failed: ${message}`);
          results.push({ id: command.id, ok: false, error: message });
        }
      }
      return results;
    },
    [bridgeGraph, currentFilename, editPath, installBridgeGraph],
  );
  const captureBridgePreview = useCallback(
    () => previewViewportRef.current?.captureFrame() ?? null,
    [],
  );
  const syncEditorContext = useEditorBridge({
    sessionId: editorSessionIdRef.current,
    graph: bridgeGraph,
    nodeManifest: getNodeManifest(),
    graphPath: currentFilename,
    editPath,
    selectedNodeId: selectedNode?.id ?? null,
    previewTargetNodeId,
    applyCommands: applyBridgeCommands,
    capturePreview: captureBridgePreview,
  });

  // ── Promote to parameter ───────────────────────────

  const promoteParameter = useCallback(
    (nodeId: string, nodeType: string, propertyKey: string, prop: ManifestProperty) => {
      const def = getNodeTypeDefs(nodeType);
      if (!def) return;

      const currentValue = (viewNodes.find((n) => n.id === nodeId)?.data as Record<string, unknown>)?.[propertyKey] ?? prop.default;
      const hasRange = prop.minimum !== undefined && prop.maximum !== undefined;

      const paramType: GraphParameter['type'] =
        prop.type === 'enum' || prop.type === 'groupSelect' || prop.type === 'groupMultiSelect'
          ? 'string'
          : prop.type === 'vector3'
            ? 'vector3'
            : (prop.type as GraphParameter['type']);
      const newParam: GraphParameter = {
        id: `p-${propertyKey}-${Date.now()}`,
        name: propertyKey.charAt(0).toUpperCase() + propertyKey.slice(1),
        type: paramType,
        default: currentValue as number | boolean | string | [number, number, number],
        exposed: true,
        targetNode: nodeId,
        targetProperty: propertyKey,
        hasRange,
        min: prop.minimum ?? 0,
        max: prop.maximum ?? 1,
      };
      commit();
      setViewParameters([...viewParameters, newParam]);
    },
    [viewNodes, viewParameters, setViewParameters, commit],
  );

  // ── Bind/unbind parameter ──────────────────────────

  const bindParameter = useCallback(
    (nodeId: string, propertyKey: string, paramId: string | null) => {
      commit();
      setViewParameters(
        viewParameters.map((p) => {
          // Clear any existing binding to this node.property
          if (p.targetNode === nodeId && p.targetProperty === propertyKey) {
            return { ...p, targetNode: '', targetProperty: '' };
          }
          return p;
        }).map((p) => {
          // Set new binding on the selected parameter
          if (paramId && p.id === paramId) {
            return { ...p, targetNode: nodeId, targetProperty: propertyKey };
          }
          return p;
        }),
      );
    },
    [viewParameters, setViewParameters, commit],
  );

  // ── Keyboard shortcuts ─────────────────────────────

  useEffect(() => {
    const onKeyDown = (e: KeyboardEvent) => {
      const target = e.target as HTMLElement;
      // Skip if typing in an input/select
      if (target.tagName === 'INPUT' || target.tagName === 'SELECT' || target.tagName === 'TEXTAREA') return;

      if (e.key === ' ' || e.code === 'Space') {
        e.preventDefault();
        openSearchAt(window.innerWidth / 2, window.innerHeight / 2);
      } else if (e.key === 'f' || e.key === 'F') {
        if ((e.target as HTMLElement).closest('.pcg-preview')) return;
        e.preventDefault();
        fitView({ ...GRAPH_FIT_VIEW_OPTIONS, duration: 200 });
      } else if (e.key === 'p' || e.key === 'P') {
        e.preventDefault();
        setShowBlackboard((v) => !v);
      } else if (e.key === 'i' || e.key === 'I') {
        e.preventDefault();
        setShowInspector((v) => !v);
      } else if ((e.metaKey || e.ctrlKey) && e.key === 'z') {
        e.preventDefault();
        if (e.shiftKey) {
          redo();
        } else {
          undo();
        }
      } else if ((e.metaKey || e.ctrlKey) && e.key === ',') {
        e.preventDefault();
        openSettings();
      }
    };
    window.addEventListener('keydown', onKeyDown);
    return () => window.removeEventListener('keydown', onKeyDown);
  }, [openSearchAt, fitView, openSettings, undo, redo]);

  // ── Node drag undo ─────────────────────────────────

  const onNodeDragStart = useCallback(() => {
    beginDrag();
  }, [beginDrag]);

  const onNodeDragStop = useCallback(() => {
    endDrag();
  }, [endDrag]);

  // ── Context menu (right-click on node) ─────────────

  const onNodeContextMenu: NodeMouseHandler = useCallback((event, node) => {
    event.preventDefault();
    setContextMenu({ x: event.clientX, y: event.clientY, nodeId: node.id });
  }, []);

  const copyRawData = useCallback(async (nodeId: string) => {
    const node = viewNodes.find((n) => n.id === nodeId);
    if (!node) return;
    const rawData = {
      id: node.id,
      type: node.type,
      position: { x: node.position.x, y: node.position.y },
      data: node.data,
    };
    try {
      await navigator.clipboard.writeText(JSON.stringify(rawData, null, 2));
      setStatus(`Copied raw data for "${node.id}"`);
    } catch {
      setStatus('Failed to copy to clipboard');
    }
    setContextMenu(null);
  }, [viewNodes]);

  // ── Graph pane right-click ─────────────────────────

  const onPaneContextMenu = useCallback((event: MouseEvent | React.MouseEvent) => {
    event.preventDefault();
    const clientX = 'clientX' in event ? event.clientX : 0;
    const clientY = 'clientY' in event ? event.clientY : 0;
    openSearchAt(clientX, clientY);
  }, [openSearchAt]);

  // ── Import / Export ─────────────────────────────────

  const handleExport = () => {
    const graph = exportGraph(nodes, edges, parameters, subgraphs);
    downloadGraph(graph);
    setStatus('Downloaded graph.pcg');
  };

  const handleSave = async () => {
    if (!currentFilename) {
      handleExport();
      return;
    }
    const graph = exportGraph(nodes, edges, parameters, subgraphs);
    const result = await saveGraphToFile(graph, currentFilename);
    if (result.ok) {
      setStatus(`Saved to ${currentFilename}`);
    } else {
      setStatus(`Save failed: ${result.error ?? 'unknown error'}`);
    }
  };

  const handleSendToUnity = async () => {
    const graph = exportGraph(nodes, edges, parameters, subgraphs);
    const result = await exportToSchema(graph);
    if (result.ok) {
      setStatus('Saved to schema/editor-export.pcg — use PCG → Reload Watched Graph in Unity');
    } else {
      setStatus(`Send failed: ${result.error ?? 'unknown error'} (is npm run dev running?)`);
    }
  };

  const handleImportClick = () => {
    fileInputRef.current?.click();
  };

  const handleImportFile = async (event: ChangeEvent<HTMLInputElement>) => {
    const file = event.target.files?.[0];
    event.target.value = '';
    if (!file) return;

    const result = await importGraphFromFile(file);
    if (!result.ok) {
      setStatus(`Import failed: ${result.error}`);
      return;
    }

    nodeCounter = syncNodeCounterFromNodes(result.nodes);
    commit();
    setEditPath([]);
    setNodes(result.nodes);
    setEdges(result.edges);
    setParameters(result.parameters);
    setSubgraphs(result.subgraphs);
    setSelectedNode(null);
    setPreviewParameterValuesByScope({});
    setCurrentFilename(file.name);
    setStatus(`Imported ${result.filename ?? 'graph'} (${result.nodes.length} nodes, ${result.edges.length} edges, ${result.parameters.length} params, ${result.subgraphs.length} subgraphs)`);
  };

  // ── New / Save As / Show in Project ────────────────

  const handleNewGraph = () => {
    commit();
    setEditPath([]);
    setNodes([]);
    setEdges([]);
    setParameters([]);
    setSubgraphs([]);
    setSelectedNode(null);
    setInfoNodeId(null);
    setPreviewTargetId(null);
    setPreviewParameterValuesByScope({});
    setCurrentFilename('');
    nodeCounter = 100;
    clearEditorSession();
    setStatus('New graph created');
  };

  const handleSaveAs = () => {
    const filename = currentFilename || 'graph.pcg';
    const graph = exportGraph(nodes, edges, parameters, subgraphs);
    downloadGraph(graph, filename);
    setStatus(`Saved as ${filename}`);
  };

  const handleShowInProject = async () => {
    if (!currentFilename) {
      setStatus('No file imported — import a .pcg file first');
      return;
    }
    const result = await revealInFinder(currentFilename);
    if (!result.ok) {
      setStatus(`Show in Project failed: ${result.error ?? 'unknown error'}`);
    }
  };

  // ── Preview cook ────────────────────────────────────

  const requestPreviewCook = useCallback(async (targetOverride?: string | null) => {
    if (viewNodes.length === 0) return;
    // Read the target from the store (not React state) so this callback's identity
    // stays stable across ▶ clicks — a new identity would propagate through
    // NodeActionsContext and re-render every node.
    const target = targetOverride === undefined ? getPreviewTargetId() : targetOverride;
    previewAbortRef.current?.abort();
    const previousJobId = previewJobIdRef.current;
    if (previousJobId) {
      await cancelCook(previousJobId);
    }
    const abort = new AbortController();
    previewAbortRef.current = abort;
    const jobId = newPreviewJobId();
    previewJobIdRef.current = jobId;

    setPreviewLoading(true);
    setPreviewError(null);
    // Full document from root states; inside a subgraph, cook its interior in
    // isolation via a synthetic Output node (interface nodes are stripped).
    let graph = exportGraph(nodes, edges, parameters, subgraphs);
    if (currentSubgraph) {
      graph = buildSubgraphCookGraph(currentSubgraph, graph.subgraphs ?? []);
    }
    graph = applyPreviewParameterOverrides(graph, previewParameterValues);
    if (target) {
      const targetNode = graph.nodes.find((n) => n.id === target);
      // Manifest nodes take their first declared output pin; subgraph instances
      // take the first output pin of the referenced definition.
      const outputPinId =
        targetNode?.type === 'Subgraph'
          ? findSubgraph(subgraphs, getSubgraphId(targetNode.data))?.outputs[0]?.id
          : getNodeTypeDefs(targetNode?.type ?? '')?.outputs[0]?.id;
      const previewGraph =
        targetNode && outputPinId ? buildPreviewCookGraph(graph, target, outputPinId) : null;
      if (previewGraph) {
        graph = previewGraph;
      } else {
        // Preview target was deleted — fall back to the full graph.
        setPreviewTargetId(null);
      }
    } else {
      graph = prepareGraphForPreviewCook(graph);
    }
    const result = await cookGraphPreview(graph, 42, abort.signal, jobId);
    if (previewAbortRef.current !== abort) return; // superseded by a newer cook
    setPreviewLoading(false);
    if (result.ok && result.data) {
      setPreviewData(result.data);
    } else if (result.error !== 'aborted') {
      setPreviewError(result.error ?? 'Cook failed');
    }
  }, [nodes, edges, parameters, subgraphs, currentSubgraph, viewNodes.length, previewParameterValues]);

  const openPreview = useCallback(async () => {
    setShowPreview(true);
    setPreviewError(null);
    const health = await checkCookServer();
    if (!health.ok) {
      setPreviewError('pcg-server unreachable — start it with scripts/run-pcg-server.sh, then press Re-cook.');
    }
    // Initial cook is fired by the debounced effect below (showPreview change).
  }, []);

  const togglePreview = useCallback(async () => {
    if (showPreview) {
      previewAbortRef.current?.abort();
      const jobId = previewJobIdRef.current;
      if (jobId) {
        await cancelCook(jobId);
      }
      setShowPreview(false);
      return;
    }
    await openPreview();
  }, [showPreview, openPreview]);

  // Persist editor session to localStorage (debounced). Skip the first render so
  // a fresh visit with no prior session does not overwrite with the default graph.
  const skipPersistRef = useRef(true);
  useEffect(() => {
    if (skipPersistRef.current) {
      skipPersistRef.current = false;
      return;
    }
    const timer = setTimeout(() => {
      if (nodes.length === 0 && !currentFilename) {
        clearEditorSession();
        return;
      }
      saveEditorSession({
        graph: exportGraph(nodes, edges, parameters, subgraphs),
        filename: currentFilename,
        nodeCounter,
      });
    }, 500);
    return () => clearTimeout(timer);
  }, [nodes, edges, parameters, subgraphs, currentFilename]);

  // Preview panel is always-on: open it once on mount.
  // StrictMode double-invocation is safe — the health-check is idempotent.
  useEffect(() => {
    void openPreview();
  }, [openPreview]);

  const handleNodePreview = useCallback(
    (nodeId: string) => {
      if (nodeId === getPreviewTargetId()) {
        // Clicking ▶ on the current target clears it — back to full-graph preview.
        setPreviewTargetId(null);
        void requestPreviewCook(null);
        return;
      }
      setPreviewTargetId(nodeId);
      if (!showPreview) {
        // Opening the panel flips showPreview, which retriggers the debounced
        // effect below; skip its next run since the direct cook here already
        // covers it. (With the panel already open the effect does not re-run —
        // the target lives outside its deps — so nothing needs skipping.)
        skipDebounceRef.current = true;
        void openPreview();
      }
      void requestPreviewCook(nodeId);
    },
    [showPreview, openPreview, requestPreviewCook],
  );

  const nodeActions = useMemo(
    () => ({ onInfo: handleNodeInfo, onPreview: handleNodePreview }),
    [handleNodeInfo, handleNodePreview],
  );

  const splineEditNode = useMemo(() => {
    const candidateIds = [previewTargetNodeId, selectedNode?.id].filter(Boolean) as string[];
    for (const id of candidateIds) {
      const node = viewNodes.find((n) => n.id === id);
      if (node?.type && isSplineAuthoringNode(node.type)) return node;
    }
    return null;
  }, [viewNodes, previewTargetNodeId, selectedNode?.id]);

  const splineEdit = useMemo((): SplineEditContext | null => {
    if (!splineEditNode) return null;
    const data = splineEditNode.data as Record<string, unknown>;
    const controlPoints = getEffectiveControlPoints(data);
    if (controlPoints.length === 0) return null;
    return {
      nodeId: splineEditNode.id,
      controlPoints,
      closed: data.closed === true,
      onControlPointsChange: () => {},
    };
  }, [splineEditNode]);

  const handleSplineControlPointsChange = useCallback(
    (nodeId: string, points: ReturnType<typeof getEffectiveControlPoints>) => {
      updateNodeData(nodeId, { controlPoints: serializeControlPoints(points) });
    },
    [updateNodeData],
  );

  const splineEditForViewport = useMemo((): SplineEditContext | null => {
    if (!splineEdit) return null;
    return {
      ...splineEdit,
      onControlPointsChange: (points) => handleSplineControlPointsChange(splineEdit.nodeId, points),
    };
  }, [splineEdit, handleSplineControlPointsChange]);

  // Debounced re-cook on graph/selection change while the panel is open.
  // Coalesces edits; never cooks per keystroke.
  const previewDebounceRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const skipDebounceRef = useRef(false);
  useEffect(() => {
    if (!showPreview) return;
    if (skipDebounceRef.current) {
      skipDebounceRef.current = false;
      return;
    }
    if (previewDebounceRef.current) clearTimeout(previewDebounceRef.current);
    previewDebounceRef.current = setTimeout(() => {
      void requestPreviewCook();
    }, 600);
    return () => {
      if (previewDebounceRef.current) clearTimeout(previewDebounceRef.current);
    };
  }, [showPreview, selectedNode, requestPreviewCook]);

  // ── Render ──────────────────────────────────────────

  return (
    <NodeActionsContext.Provider value={nodeActions}>
    <SubgraphsContext.Provider value={subgraphs}>
    <CurrentSubgraphContext.Provider value={currentSubgraph}>
    <div className="pcg-app">
      {/* Toolbar — aligned with Unity: left=New/Save/Save As/Show in Project, right=Parameters/Inspector */}
      <div className="pcg-toolbar">
        <button type="button" onClick={() => handleNewGraph()} title="New Graph">New</button>
        <button type="button" onClick={handleSave} title="Save Graph">Save</button>
        <button type="button" onClick={handleSaveAs} title="Save As">Save As...</button>
        <button type="button" onClick={handleShowInProject} title="Show in Project">Show in Project</button>
        <span className="pcg-toolbar__spacer" />
        <button
          type="button"
          className={showAgent ? 'pcg-toolbar__toggle--active' : ''}
          onClick={() => setShowAgent((v) => !v)}
          title="Toggle Agent panel"
        >
          Agent
        </button>
        <button
          type="button"
          className={showBlackboard ? 'pcg-toolbar__toggle--active' : ''}
          onClick={() => setShowBlackboard((v) => !v)}
          title="Toggle Parameters (P)"
        >
          Parameters
        </button>
        <button
          type="button"
          className={showInspector ? 'pcg-toolbar__toggle--active' : ''}
          onClick={() => setShowInspector((v) => !v)}
          title="Toggle Inspector (I)"
        >
          Inspector
        </button>
        <button
          type="button"
          className={showPreview ? 'pcg-toolbar__toggle--active' : ''}
          onClick={() => void togglePreview()}
          title="Toggle 3D Preview (requires pcg-server)"
        >
          Preview
        </button>
        <span className="pcg-toolbar__separator" />
        <button type="button" onClick={openSettings} title="PCG Settings (⌘,)">Settings</button>
        <input
          ref={fileInputRef}
          type="file"
          accept=".pcg,.json,application/json"
          className="pcg-toolbar__file-input"
          onChange={handleImportFile}
        />
        <button type="button" className="pcg-toolbar__import" onClick={handleImportClick}>Import</button>
        <button type="button" className="pcg-toolbar__export" onClick={handleSendToUnity}>Send to Unity</button>
        {status && <span className="pcg-toolbar__status">{status}</span>}
      </div>

      <SettingsDialog
        open={showSettings}
        onClose={closeSettings}
        onProvidersChanged={() => setProviderRevision((revision) => revision + 1)}
      />

      {/* Main: three-panel layout */}
      <div className="pcg-main">
        {showAgent && (
          <AgentPanel
            onApplyActions={applyAgentActions}
            onOpenSettings={openSettings}
            syncEditorContext={syncEditorContext}
            editorSessionId={editorSessionIdRef.current}
            providerRevision={providerRevision}
          />
        )}
        {showBlackboard && (
          <Blackboard
            parameters={viewParameters}
            nodes={viewNodes}
            onParametersChange={(params) => {
              commit();
              setViewParameters(params);
            }}
          />
        )}
        {showPreview && (
          <PreviewViewport
            ref={previewViewportRef}
            data={previewData}
            loading={previewLoading}
            error={previewError}
            onRefresh={() => void requestPreviewCook()}
            parameters={viewParameters}
            parameterNodeIds={previewParameterNodeIds}
            parameterValues={previewParameterValues}
            onParameterValueChange={updatePreviewParameterValue}
            onResetParameters={resetPreviewParameters}
            onSaveParameterDefaults={savePreviewParametersAsDefaults}
            splineEdit={splineEditForViewport}
            onPickNode={handlePreviewPickNode}
            selectedNodeId={selectedNode?.id ?? null}
          />
        )}
        <div className="pcg-graph-container">
          {/* Breadcrumb — visible while editing inside a subgraph */}
          {editPath.length > 0 && (
            <div className="pcg-breadcrumb">
              <button
                type="button"
                className="pcg-breadcrumb__item"
                onClick={() => navigateTo([])}
              >
                Root
              </button>
              {editPath.map((id, i) => (
                <span key={`${id}-${i}`} className="pcg-breadcrumb__segment">
                  <span className="pcg-breadcrumb__sep">/</span>
                  <button
                    type="button"
                    className={`pcg-breadcrumb__item${i === editPath.length - 1 ? ' pcg-breadcrumb__item--current' : ''}`}
                    onClick={() => navigateTo(editPath.slice(0, i + 1))}
                  >
                    {subgraphs.find((s) => s.id === id)?.name || id}
                  </button>
                </span>
              ))}
              <span className="pcg-breadcrumb__hint">Double-click empty space to go up</span>
            </div>
          )}
          <ReactFlow
            nodes={viewNodes}
            edges={viewEdges}
            onNodesChange={onViewNodesChange}
            onEdgesChange={onViewEdgesChange}
            onConnect={onConnect}
            onConnectStart={onConnectStart}
            onConnectEnd={onConnectEnd}
            onNodeClick={onNodeClick}
            onNodeDoubleClick={onNodeDoubleClick}
            onDoubleClick={(e) => {
              // Double-click on empty canvas goes up one level (Unity-style).
              const target = e.target as HTMLElement;
              if (editPath.length > 0 && target.classList.contains('react-flow__pane')) {
                navigateTo(editPath.slice(0, -1));
              }
            }}
            onPaneClick={onPaneClick}
            onNodeDragStart={onNodeDragStart}
            onNodeDragStop={onNodeDragStop}
            onNodeContextMenu={onNodeContextMenu}
            onPaneContextMenu={onPaneContextMenu}
            isValidConnection={validateConnection}
            nodeTypes={nodeTypes}
            minZoom={GRAPH_MIN_ZOOM}
            maxZoom={GRAPH_MAX_ZOOM}
            fitView
            fitViewOptions={GRAPH_FIT_VIEW_OPTIONS}
            deleteKeyCode={['Delete', 'Backspace']}
          >
            <Background variant={BackgroundVariant.Lines} gap={24} color="#2b2b2b" />
            <Controls />
            <MiniMap pannable zoomable />
          </ReactFlow>

          {/* Status bar */}
          <div className="pcg-status-bar">
            <span>
              {currentSubgraph && <span className="pcg-status-bar__path">{currentSubgraph.name || currentSubgraph.id}: </span>}
              {viewNodes.length} nodes · {viewEdges.length} edges · {viewParameters.length} params · {subgraphs.length} subgraphs
            </span>
            <span className="pcg-status-bar__shortcuts">Space: Create · F: Fit · P: Parameters · I: Inspector</span>
            {(canUndo || canRedo) && (
              <span className="pcg-status-bar__undo">
                <button type="button" onClick={undo} disabled={!canUndo} className="pcg-status-bar__btn">↶ Undo</button>
                <button type="button" onClick={redo} disabled={!canRedo} className="pcg-status-bar__btn">↷ Redo</button>
              </span>
            )}
          </div>
        </div>
        {showInspector && (
          <Inspector
            selectedNode={selectedNode}
            parameters={viewParameters}
            nodes={viewNodes}
            edges={viewEdges}
            onUpdateNodeData={updateNodeData}
            onPromoteParameter={promoteParameter}
            onBindParameter={bindParameter}
          />
        )}
      </div>

      {/* Floating: Node Search Panel */}
      {searchConfig && (
        <>
          <div className="pcg-overlay" onClick={() => searchConfig.onSelect(null)} />
          <NodeSearchPanel config={searchConfig} />
        </>
      )}

      {/* Floating: Node Info Panel (pinned via hover toolbar ℹ) */}
      {infoNodeId &&
        (() => {
          const node = viewNodes.find((n) => n.id === infoNodeId);
          if (!node) return null;
          // Anchor to the node's right edge; recompute every render so the
          // panel sticks to the node when it moves or the viewport changes.
          const width = node.measured?.width ?? 200;
          const anchor = flowToScreenPosition({
            x: node.position.x + width + 12,
            y: node.position.y,
          });
          return (
            <NodeInfoPanel
              node={node}
              nodes={viewNodes}
              edges={viewEdges}
              x={anchor.x}
              y={anchor.y}
              onClose={() => setInfoNodeId(null)}
            />
          );
        })()}

      {/* Floating: Context Menu */}
      {contextMenu && (
        <>
          <div className="pcg-overlay" onClick={() => setContextMenu(null)} />
          <div
            className="pcg-context-menu"
            style={{ left: contextMenu.x, top: contextMenu.y }}
          >
            <button
              type="button"
              className="pcg-context-menu__item"
              onClick={() => copyRawData(contextMenu.nodeId)}
            >
              Copy Raw Data
            </button>
          </div>
        </>
      )}
    </div>
    </CurrentSubgraphContext.Provider>
    </SubgraphsContext.Provider>
    </NodeActionsContext.Provider>
  );
}

export default function App() {
  return (
    <ReactFlowProvider>
      <PcgEditor />
    </ReactFlowProvider>
  );
}
