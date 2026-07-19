// App.tsx — PCG Graph Editor (Web) with three-panel layout.
// Features: manifest-driven nodes, Inspector, Blackboard, port drag-to-search,
// node search, Copy Raw Data, undo/redo, keyboard shortcuts.

import { useCallback, useRef, useState, useEffect, type ChangeEvent } from 'react';
import {
  ReactFlow,
  Background,
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
import { importGraphFromFile, syncNodeCounterFromNodes } from './importGraph';
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
import Inspector from './Inspector';
import NodeInfoPanel from './NodeInfoPanel';
import NodeSearchPanel, { type SearchPanelConfig } from './NodeSearchPanel';
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

function PcgEditor() {
  const [nodes, setNodes, onNodesChange] = useNodesState(initialNodes);
  const [edges, setEdges, onEdgesChange] = useEdgesState(initialEdges);
  const [parameters, setParameters] = useState<GraphParameter[]>([]);
  const [subgraphs, setSubgraphs] = useState<GraphSubgraph[]>([]);
  const [selectedNode, setSelectedNode] = useState<Node | null>(null);
  const [showBlackboard, setShowBlackboard] = useState(true);
  const [showInspector, setShowInspector] = useState(true);
  const [status, setStatus] = useState('');
  const [searchConfig, setSearchConfig] = useState<SearchPanelConfig | null>(null);
  const [contextMenu, setContextMenu] = useState<{ x: number; y: number; nodeId: string } | null>(null);
  const [currentFilename, setCurrentFilename] = useState<string>('');
  const [hoveredNode, setHoveredNode] = useState<{ node: Node; x: number; y: number } | null>(null);
  const hoverTimer = useRef<ReturnType<typeof setTimeout> | null>(null);

  const fileInputRef = useRef<HTMLInputElement>(null);
  const connectingNodeId = useRef<string | null>(null);
  const connectingHandleId = useRef<string | null>(null);
  const connectingHandleType = useRef<'source' | 'target' | null>(null);
  const { screenToFlowPosition, fitView } = useReactFlow();

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

  // ── Node hover → Node Info panel (300ms delay) ─────
  const onNodeMouseEnter: NodeMouseHandler = useCallback((event, node) => {
    if (hoverTimer.current) clearTimeout(hoverTimer.current);
    hoverTimer.current = setTimeout(() => {
      setHoveredNode({ node, x: event.clientX + 16, y: event.clientY + 8 });
    }, 300);
  }, []);

  const onNodeMouseLeave: NodeMouseHandler = useCallback(() => {
    if (hoverTimer.current) clearTimeout(hoverTimer.current);
    setHoveredNode(null);
  }, []);

  const onPaneClick = useCallback(() => {
    setSelectedNode(null);
    setContextMenu(null);
    setHoveredNode(null);
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

  // ── Promote to parameter ───────────────────────────

  const promoteParameter = useCallback(
    (nodeId: string, nodeType: string, propertyKey: string, prop: ManifestProperty) => {
      const def = getNodeTypeDefs(nodeType);
      if (!def) return;

      const currentValue = (nodes.find((n) => n.id === nodeId)?.data as Record<string, unknown>)?.[propertyKey] ?? prop.default;
      const hasRange = prop.minimum !== undefined && prop.maximum !== undefined;

      const paramType: GraphParameter['type'] = (prop.type === 'enum' || prop.type === 'groupSelect' || prop.type === 'groupMultiSelect') ? 'string' : prop.type;
      const newParam: GraphParameter = {
        id: `p-${propertyKey}-${Date.now()}`,
        name: propertyKey.charAt(0).toUpperCase() + propertyKey.slice(1),
        type: paramType,
        default: currentValue as number | boolean | string,
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
        e.preventDefault();
        fitView({ duration: 200 });
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
    setCurrentFilename('');
    nodeCounter = 100;
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

  // ── Render ──────────────────────────────────────────

  return (
    <div className="pcg-app">
      {/* Toolbar — aligned with Unity: left=New/Save/Save As, right=Parameters/Inspector/Show in Project */}
      <div className="pcg-toolbar">
        <button type="button" onClick={() => handleNewGraph()} title="New Graph">New</button>
        <button type="button" onClick={handleSave} title="Save Graph">Save</button>
        <button type="button" onClick={handleSaveAs} title="Save As">Save As...</button>
        <span className="pcg-toolbar__spacer" />
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
        <button type="button" onClick={handleShowInProject} title="Show in Project">Show in Project</button>
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
            onNodeMouseEnter={onNodeMouseEnter}
            onNodeMouseLeave={onNodeMouseLeave}
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
            <Background />
            <Controls />
            <MiniMap />
          </ReactFlow>

          {/* Status bar */}
          <div className="pcg-status-bar">
            <span>{nodes.length} nodes · {edges.length} edges · {parameters.length} params · {subgraphs.length} subgraphs</span>
            <span className="pcg-status-bar__shortcuts">Space: Create · F: Fit</span>
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
      </div>

      {/* Floating: Node Search Panel */}
      {searchConfig && (
        <>
          <div className="pcg-overlay" onClick={() => searchConfig.onSelect(null)} />
          <NodeSearchPanel config={searchConfig} />
        </>
      )}

      {/* Floating: Node Info Panel */}
      {hoveredNode && (
        <NodeInfoPanel
          node={hoveredNode.node}
          nodes={nodes}
          edges={edges}
          x={hoveredNode.x}
          y={hoveredNode.y}
        />
      )}

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
  );
}

export default function App() {
  return (
    <ReactFlowProvider>
      <PcgEditor />
    </ReactFlowProvider>
  );
}
