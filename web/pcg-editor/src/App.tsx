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
  useNodesState,
  useEdgesState,
  useReactFlow,
  type Connection,
  type Node,
  type Edge,
  type NodeMouseHandler,
} from '@xyflow/react';
import '@xyflow/react/dist/style.css';

import ManifestNode from './nodes/ManifestNode';
import { defaultData } from './graphSchema';
import type { GraphParameter, GraphSubgraph } from './graphSchema';
import { exportGraph, downloadGraph, exportToSchema, saveGraphToFile, revealInFinder } from './exportGraph';
import { importGraphFromFile, parseGraphJson, syncNodeCounterFromNodes } from './importGraph';
import { clearEditorSession, loadEditorSession, saveEditorSession } from './editorSession';
import { isValidConnection } from './connectionValidation';
import {
  getNodeTypeDefs,
  getOutputPinType,
  getInputPinType,
  getAllNodeTypes,
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
import PreviewViewport, { type SplineEditContext } from './PreviewViewport';
import { cookGraphPreview, cancelCook, checkCookServer, buildPreviewCookGraph, type PreviewData } from './previewCook';
import { NodeActionsContext } from './nodeActions';
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
  // Dynamic subgraph pins are preserved by import/export. Full nested graph
  // authoring is intentionally outside this editor's current scope.
  Subgraph: ManifestNode,
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
  const restored = restoredRef.current;

  const [nodes, setNodes, onNodesChange] = useNodesState(restored?.nodes ?? initialNodes);
  const [edges, setEdges, onEdgesChange] = useEdgesState(restored?.edges ?? initialEdges);
  const [parameters, setParameters] = useState<GraphParameter[]>(restored?.parameters ?? []);
  const [subgraphs, setSubgraphs] = useState<GraphSubgraph[]>(restored?.subgraphs ?? []);
  const [selectedNode, setSelectedNode] = useState<Node | null>(null);
  const [showBlackboard, setShowBlackboard] = useState(false);
  const [showAgent, setShowAgent] = useState(true);
  const [showInspector, setShowInspector] = useState(false);
  const [showPreview, setShowPreview] = useState(true);
  const [previewData, setPreviewData] = useState<PreviewData | null>(null);
  const [previewLoading, setPreviewLoading] = useState(false);
  const [previewError, setPreviewError] = useState<string | null>(null);
  const previewAbortRef = useRef<AbortController | null>(null);
  const [status, setStatus] = useState(
    restored
      ? `Restored last session${restored.filename ? `: ${restored.filename}` : ''} (${restored.nodes.length} nodes)`
      : '',
  );
  const [searchConfig, setSearchConfig] = useState<SearchPanelConfig | null>(null);
  const [contextMenu, setContextMenu] = useState<{ x: number; y: number; nodeId: string } | null>(null);
  const [currentFilename, setCurrentFilename] = useState<string>(restored?.filename ?? '');
  const [infoNode, setInfoNode] = useState<{ nodeId: string; x: number; y: number } | null>(null);
  const [previewTargetNodeId, setPreviewTargetNodeId] = useState<string | null>(null);

  const fileInputRef = useRef<HTMLInputElement>(null);
  const connectingNodeId = useRef<string | null>(null);
  const connectingHandleId = useRef<string | null>(null);
  const connectingHandleType = useRef<'source' | 'target' | null>(null);
  const { screenToFlowPosition, flowToScreenPosition, fitView } = useReactFlow();

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

  // ── Connection ──────────────────────────────────────

  const validateConnection = useCallback(
    (conn: Connection | Edge) => isValidConnection(conn, nodes, edges),
    [nodes, edges],
  );

  const onConnect = useCallback(
    (conn: Connection) => {
      if (!validateConnection(conn)) return;
      commit();
      setEdges((eds) => addEdge(conn, eds));
    },
    [setEdges, validateConnection, commit],
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
      const node = nodes.find((n) => n.id === nodeId);
      if (!node?.type) return;

      let pinType: PinType | undefined;
      if (handleType === 'source') {
        pinType = getOutputPinType(node.type, handleId);
      } else {
        pinType = getInputPinType(node.type, handleId);
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
          const newId = `n${++nodeCounter}`;
          const newNode: Node = {
            id: newId,
            type: nodeType,
            position: flowPos,
            data: { ...defaultData(nodeType) },
          };
          commit();
          setNodes((nds) => [...nds, newNode]);

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
                setEdges((eds) => addEdge(conn, eds));
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
                setEdges((eds) => addEdge(conn, eds));
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
    [nodes, setNodes, setEdges, screenToFlowPosition, commit, validateConnection],
  );

  // ── Node creation ───────────────────────────────────

  const createNodeAt = useCallback(
    (nodeType: string, x: number, y: number) => {
      const flowPos = screenToFlowPosition({ x, y });
      const newId = `n${++nodeCounter}`;
      const newNode: Node = {
        id: newId,
        type: nodeType,
        position: flowPos,
        data: { ...defaultData(nodeType) },
      };
      commit();
      setNodes((nds) => [...nds, newNode]);
    },
    [setNodes, screenToFlowPosition, commit],
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

  const handleNodeInfo = useCallback(
    (nodeId: string) => {
      const node = nodes.find((n) => n.id === nodeId);
      if (!node) return;
      setInfoNode((prev) => {
        if (prev?.nodeId === nodeId) return null;
        const anchor = flowToScreenPosition({ x: node.position.x + 200, y: node.position.y });
        return { nodeId, x: anchor.x, y: anchor.y };
      });
    },
    [nodes, flowToScreenPosition],
  );

  const onPaneClick = useCallback(() => {
    setSelectedNode(null);
    setContextMenu(null);
    setInfoNode(null);
  }, []);

  // ── Node data update (from Inspector) ──────────────

  const updateNodeData = useCallback(
    (nodeId: string, patch: Record<string, unknown>) => {
      commit();
      setNodes((nds) =>
        nds.map((n) =>
          n.id === nodeId ? { ...n, data: { ...n.data, ...patch } } : n,
        ),
      );
      // Update selectedNode ref
      setSelectedNode((prev) =>
        prev?.id === nodeId ? { ...prev, data: { ...prev.data, ...patch } } : prev,
      );
    },
    [setNodes, commit],
  );

  // ── Agent graph ops (actions dispatched from the agent panel) ──

  const agentOps = useMemo<AgentGraphOps>(
    () => ({
      addNode: (nodeType, position) => {
        if (!getNodeTypeDefs(nodeType)) {
          throw new Error(`unknown node type "${nodeType}"`);
        }
        const newId = `n${++nodeCounter}`;
        const pos = position ?? screenToFlowPosition({
          x: window.innerWidth / 2,
          y: window.innerHeight / 2,
        });
        setNodes((nds) => [
          ...nds,
          { id: newId, type: nodeType, position: pos, data: { ...defaultData(nodeType) } },
        ]);
        return newId;
      },
      connectNodes: (source, target, sourceHandle, targetHandle) => {
        const sourceNode = nodes.find((n) => n.id === source);
        const targetNode = nodes.find((n) => n.id === target);
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
        setEdges((eds) => addEdge(conn, eds));
      },
      setNodeParam: (nodeId, key, value) => {
        if (!nodes.some((n) => n.id === nodeId)) {
          throw new Error(`node "${nodeId}" not found`);
        }
        setNodes((nds) =>
          nds.map((n) => (n.id === nodeId ? { ...n, data: { ...n.data, [key]: value } } : n)),
        );
      },
    }),
    [nodes, setNodes, setEdges, screenToFlowPosition, validateConnection],
  );

  const applyAgentActions = useCallback(
    (actions: AgentAction[]) => dispatchAgentActions(actions, agentOps, commit),
    [agentOps, commit],
  );

  // ── Promote to parameter ───────────────────────────

  const promoteParameter = useCallback(
    (nodeId: string, nodeType: string, propertyKey: string, prop: ManifestProperty) => {
      const def = getNodeTypeDefs(nodeType);
      if (!def) return;

      const currentValue = (nodes.find((n) => n.id === nodeId)?.data as Record<string, unknown>)?.[propertyKey] ?? prop.default;
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
      setParameters((prev) => [...prev, newParam]);
    },
    [nodes, commit],
  );

  // ── Bind/unbind parameter ──────────────────────────

  const bindParameter = useCallback(
    (nodeId: string, propertyKey: string, paramId: string | null) => {
      commit();
      setParameters((prev) =>
        prev.map((p) => {
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
    [commit],
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
        fitView({ duration: 200 });
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
      }
    };
    window.addEventListener('keydown', onKeyDown);
    return () => window.removeEventListener('keydown', onKeyDown);
  }, [openSearchAt, fitView, undo, redo]);

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
    const node = nodes.find((n) => n.id === nodeId);
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
  }, [nodes]);

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
    setNodes(result.nodes);
    setEdges(result.edges);
    setParameters(result.parameters);
    setSubgraphs(result.subgraphs);
    setSelectedNode(null);
    setCurrentFilename(file.name);
    setStatus(`Imported ${result.filename ?? 'graph'} (${result.nodes.length} nodes, ${result.edges.length} edges, ${result.parameters.length} params, ${result.subgraphs.length} subgraphs)`);
  };

  // ── New / Save As / Show in Project ────────────────

  const handleNewGraph = () => {
    commit();
    setNodes([]);
    setEdges([]);
    setParameters([]);
    setSubgraphs([]);
    setSelectedNode(null);
    setInfoNode(null);
    setPreviewTargetNodeId(null);
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
    if (nodes.length === 0) return;
    const target = targetOverride === undefined ? previewTargetNodeId : targetOverride;
    previewAbortRef.current?.abort();
    void cancelCook();
    const abort = new AbortController();
    previewAbortRef.current = abort;

    setPreviewLoading(true);
    setPreviewError(null);
    let graph = exportGraph(nodes, edges, parameters, subgraphs);
    if (target) {
      const targetNode = nodes.find((n) => n.id === target);
      const outputPin = targetNode ? getNodeTypeDefs(targetNode.type ?? '')?.outputs[0] : undefined;
      const previewGraph =
        targetNode && outputPin ? buildPreviewCookGraph(graph, target, outputPin.id) : null;
      if (previewGraph) {
        graph = previewGraph;
      } else {
        // Preview target was deleted — fall back to the full graph.
        setPreviewTargetNodeId(null);
      }
    }
    const result = await cookGraphPreview(graph, 42, abort.signal);
    if (previewAbortRef.current !== abort) return; // superseded by a newer cook
    setPreviewLoading(false);
    if (result.ok && result.data) {
      setPreviewData(result.data);
    } else if (result.error !== 'aborted') {
      setPreviewError(result.error ?? 'Cook failed');
    }
  }, [nodes, edges, parameters, subgraphs, previewTargetNodeId]);

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
      void cancelCook();
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
      // The target change retriggers the debounced effect below; skip its next
      // run since the direct cook here already covers it (avoids a duplicate
      // request whose cache-hit stats would mask the real exec numbers).
      skipDebounceRef.current = true;
      setPreviewTargetNodeId(nodeId);
      if (!showPreview) {
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

  const previewTargetLabel = (() => {
    if (!previewTargetNodeId) return null;
    const node = nodes.find((n) => n.id === previewTargetNodeId);
    if (!node) return null;
    return getNodeTypeDefs(node.type ?? '')?.displayName ?? node.type ?? previewTargetNodeId;
  })();

  const splineEditNode = useMemo(() => {
    const candidateIds = [previewTargetNodeId, selectedNode?.id].filter(Boolean) as string[];
    for (const id of candidateIds) {
      const node = nodes.find((n) => n.id === id);
      if (node?.type && isSplineAuthoringNode(node.type)) return node;
    }
    return null;
  }, [nodes, previewTargetNodeId, selectedNode?.id]);

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

      {/* Main: three-panel layout */}
      <div className="pcg-main">
        {showAgent && <AgentPanel onApplyActions={applyAgentActions} />}
        {showBlackboard && (
          <Blackboard
            parameters={parameters}
            nodes={nodes}
            onParametersChange={(params) => {
              commit();
              setParameters(params);
            }}
          />
        )}
        <div className="pcg-graph-container">
          <ReactFlow
            nodes={nodes}
            edges={edges}
            onNodesChange={onNodesChange}
            onEdgesChange={onEdgesChange}
            onConnect={onConnect}
            onConnectStart={onConnectStart}
            onConnectEnd={onConnectEnd}
            onNodeClick={onNodeClick}
            onPaneClick={onPaneClick}
            onNodeDragStart={onNodeDragStart}
            onNodeDragStop={onNodeDragStop}
            onNodeContextMenu={onNodeContextMenu}
            onPaneContextMenu={onPaneContextMenu}
            isValidConnection={validateConnection}
            nodeTypes={nodeTypes}
            fitView
            deleteKeyCode={['Delete', 'Backspace']}
          >
            <Background variant={BackgroundVariant.Lines} gap={24} color="#2b2b2b" />
            <Controls />
            <MiniMap pannable zoomable />
          </ReactFlow>

          {/* Status bar */}
          <div className="pcg-status-bar">
            <span>{nodes.length} nodes · {edges.length} edges · {parameters.length} params · {subgraphs.length} subgraphs</span>
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
            parameters={parameters}
            nodes={nodes}
            edges={edges}
            onUpdateNodeData={updateNodeData}
            onPromoteParameter={promoteParameter}
            onBindParameter={bindParameter}
          />
        )}
        {showPreview && (
          <PreviewViewport
            data={previewData}
            loading={previewLoading}
            error={previewError}
            onRefresh={() => void requestPreviewCook()}
            onClose={() => void togglePreview()}
            targetLabel={previewTargetLabel}
            splineEdit={splineEditForViewport}
            onResetTarget={
              previewTargetNodeId
                ? () => {
                    skipDebounceRef.current = true;
                    setPreviewTargetNodeId(null);
                    void requestPreviewCook(null);
                  }
                : undefined
            }
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
      {infoNode &&
        (() => {
          const node = nodes.find((n) => n.id === infoNode.nodeId);
          return node ? (
            <NodeInfoPanel
              node={node}
              nodes={nodes}
              edges={edges}
              x={infoNode.x}
              y={infoNode.y}
              onClose={() => setInfoNode(null)}
            />
          ) : null;
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
