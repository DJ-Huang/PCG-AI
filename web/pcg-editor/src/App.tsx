import { useCallback, useState } from 'react';
import {
  ReactFlow,
  Background,
  Controls,
  MiniMap,
  ReactFlowProvider,
  addEdge,
  useNodesState,
  useEdgesState,
  type Connection,
  type Node,
  type Edge,
} from '@xyflow/react';
import '@xyflow/react/dist/style.css';

import ParseConfigNode from './nodes/ParseConfigNode';
import SpawnPointsNode from './nodes/SpawnPointsNode';
import PlaceInSceneNode from './nodes/PlaceInSceneNode';
import { defaultData, type NodeType as GraphNodeType } from './graphSchema';
import { exportGraph, downloadGraph, exportToSchema } from './exportGraph';
import { isValidConnection } from './connectionValidation';
import './App.css';

const nodeTypes = {
  ParseConfig: ParseConfigNode,
  SpawnPoints: SpawnPointsNode,
  PlaceInScene: PlaceInSceneNode,
};

const initialNodes: Node[] = [
  {
    id: 'n1',
    type: 'ParseConfig',
    position: { x: 50, y: 150 },
    data: { ...defaultData.ParseConfig },
  },
  {
    id: 'n2',
    type: 'SpawnPoints',
    position: { x: 400, y: 150 },
    data: { ...defaultData.SpawnPoints },
  },
  {
    id: 'n3',
    type: 'PlaceInScene',
    position: { x: 750, y: 150 },
    data: { ...defaultData.PlaceInScene },
  },
];

const initialEdges: Edge[] = [
  { id: 'e1', source: 'n1', target: 'n2', sourceHandle: 'out', targetHandle: 'in' },
  { id: 'e2', source: 'n2', target: 'n3', sourceHandle: 'out', targetHandle: 'in' },
];

let nodeCounter = 100;

function PcgEditor() {
  const [nodes, setNodes, onNodesChange] = useNodesState(initialNodes);
  const [edges, setEdges, onEdgesChange] = useEdgesState(initialEdges);
  const [status, setStatus] = useState('');

  const validateConnection = useCallback(
    (conn: Connection | Edge) => isValidConnection(conn, nodes, edges),
    [nodes, edges],
  );

  const onConnect = useCallback(
    (conn: Connection) => {
      if (!validateConnection(conn)) return;
      setEdges((eds) => addEdge(conn, eds));
    },
    [setEdges, validateConnection],
  );

  const addNode = (type: GraphNodeType) => {
    const id = `n${++nodeCounter}`;
    const newNode: Node = {
      id,
      type,
      position: { x: 300 + Math.random() * 200, y: 100 + Math.random() * 200 },
      data: { ...defaultData[type] },
    };
    setNodes((nds) => [...nds, newNode]);
  };

  const handleExport = () => {
    const graph = exportGraph(nodes, edges);
    downloadGraph(graph);
    setStatus('Downloaded graph.pcg.json');
  };

  const handleSendToUnity = async () => {
    const graph = exportGraph(nodes, edges);
    const result = await exportToSchema(graph);
    if (result.ok) {
      setStatus('Saved to schema/editor-export.pcg.json — use PCG → Reload Watched Graph in Unity');
    } else {
      setStatus(`Send failed: ${result.error ?? 'unknown error'} (is npm run dev running?)`);
    }
  };

  return (
    <div className="pcg-app">
      <div className="pcg-toolbar">
        <button type="button" onClick={() => addNode('ParseConfig')}>+ ParseConfig</button>
        <button type="button" onClick={() => addNode('SpawnPoints')}>+ SpawnPoints</button>
        <button type="button" onClick={() => addNode('PlaceInScene')}>+ PlaceInScene</button>
        <span className="pcg-toolbar__spacer" />
        <button type="button" className="pcg-toolbar__export" onClick={handleExport}>Export JSON</button>
        <button type="button" className="pcg-toolbar__unity" onClick={handleSendToUnity}>Send to Unity</button>
        {status && <span className="pcg-toolbar__status">{status}</span>}
      </div>
      <ReactFlow
        nodes={nodes}
        edges={edges}
        onNodesChange={onNodesChange}
        onEdgesChange={onEdgesChange}
        onConnect={onConnect}
        isValidConnection={validateConnection}
        nodeTypes={nodeTypes}
        fitView
      >
        <Background />
        <Controls />
        <MiniMap />
      </ReactFlow>
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
