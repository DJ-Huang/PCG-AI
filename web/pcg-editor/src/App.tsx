import { useCallback, useState } from 'react';
import {
  ReactFlow,
  Background,
  Controls,
  MiniMap,
  addEdge,
  useNodesState,
  useEdgesState,
  type Connection,
  type Node,
  type Edge,
  type NodeType,
} from '@xyflow/react';
import '@xyflow/react/dist/style.css';

import ParseConfigNode from './nodes/ParseConfigNode';
import SpawnPointsNode from './nodes/SpawnPointsNode';
import PlaceInSceneNode from './nodes/PlaceInSceneNode';
import { defaultData, type NodeType as GraphNodeType } from './graphSchema';
import { exportGraph, downloadGraph } from './exportGraph';
import './App.css';

const nodeTypes: Record<string, typeof ParseConfigNode> = {
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

export default function App() {
  const [nodes, setNodes, onNodesChange] = useNodesState(initialNodes);
  const [edges, setEdges, onEdgesChange] = useEdgesState(initialEdges);

  const onConnect = useCallback(
    (conn: Connection) => setEdges((eds) => addEdge(conn, eds)),
    [setEdges],
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
  };

  return (
    <div className="pcg-app">
      <div className="pcg-toolbar">
        <button onClick={() => addNode('ParseConfig')}>+ ParseConfig</button>
        <button onClick={() => addNode('SpawnPoints')}>+ SpawnPoints</button>
        <button onClick={() => addNode('PlaceInScene')}>+ PlaceInScene</button>
        <span className="pcg-toolbar__spacer" />
        <button className="pcg-toolbar__export" onClick={handleExport}>Export JSON</button>
      </div>
      <ReactFlow
        nodes={nodes}
        edges={edges}
        onNodesChange={onNodesChange}
        onEdgesChange={onEdgesChange}
        onConnect={onConnect}
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
