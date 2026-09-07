import { useCallback, useEffect, useMemo, useState } from 'react';
import {
  applyEdgeChanges,
  applyNodeChanges,
  Background,
  BackgroundVariant,
  Controls,
  MiniMap,
  ReactFlow,
  ReactFlowProvider,
  useReactFlow,
  type Connection,
  type EdgeChange,
  type Node,
  type NodeChange,
  type NodeMouseHandler,
} from '@xyflow/react';

import {
  applyCameraGraphEdgeChanges,
  applyCameraGraphNodeChanges,
  connectCameraGraph,
  preserveFlowNodeLayout,
  shotCamerasToFlow,
} from './cameraGraph';
import CameraNode from './nodes/CameraNode';
import { SHOT_OUTPUT_ID, selectShotNode, type ShotDocument } from './shot';

const cameraNodeTypes = {
  Camera: CameraNode,
  MotionCurve: CameraNode,
  ShotOutput: CameraNode,
};

const CAMERA_FIT_VIEW_OPTIONS = {
  minZoom: 0.05,
  maxZoom: 1.25,
  padding: 0.2,
};

function shouldPersistNodeChanges(changes: NodeChange[]): boolean {
  return changes.some((change) => (
    change.type === 'position'
    || change.type === 'remove'
    || change.type === 'add'
    || change.type === 'replace'
  ));
}

interface CameraWorkspaceProps {
  shot: ShotDocument;
  onShotChange: (shot: ShotDocument) => void;
  onRequestCreateCamera?: (request: {
    x: number;
    y: number;
    flowPosition: { x: number; y: number };
  }) => void;
}

function CameraWorkspaceCanvas({ shot, onShotChange, onRequestCreateCamera }: CameraWorkspaceProps) {
  const derived = useMemo(() => shotCamerasToFlow(shot), [shot]);
  const [nodes, setNodes] = useState(derived.nodes);
  const [edges, setEdges] = useState(derived.edges);
  const { fitView, screenToFlowPosition } = useReactFlow();
  const measuredCount = nodes.filter((node) => node.measured).length;

  useEffect(() => {
    setNodes((prev) => preserveFlowNodeLayout(derived.nodes, prev));
    setEdges(derived.edges);
  }, [derived]);

  useEffect(() => {
    if (measuredCount === 0) return undefined;
    const frame = window.requestAnimationFrame(() => {
      fitView(CAMERA_FIT_VIEW_OPTIONS);
    });
    return () => window.cancelAnimationFrame(frame);
  }, [fitView, nodes.length, measuredCount]);

  const onNodesChange = useCallback((changes: NodeChange[]) => {
    setNodes((current) => applyNodeChanges(changes, current));
    if (shouldPersistNodeChanges(changes)) {
      onShotChange(applyCameraGraphNodeChanges(shot, changes));
    }
  }, [onShotChange, shot]);

  const onEdgesChange = useCallback((changes: EdgeChange[]) => {
    setEdges((current) => applyEdgeChanges(changes, current));
    onShotChange(applyCameraGraphEdgeChanges(shot, changes));
  }, [onShotChange, shot]);

  const onConnect = useCallback((connection: Connection) => {
    onShotChange(connectCameraGraph(shot, connection));
  }, [onShotChange, shot]);

  const onNodeClick: NodeMouseHandler = useCallback((_event, node: Node) => {
    if (node.id === SHOT_OUTPUT_ID) return;
    onShotChange(selectShotNode(shot, node.id));
  }, [onShotChange, shot]);

  const onPaneContextMenu = useCallback((event: { preventDefault: () => void; clientX: number; clientY: number }) => {
    event.preventDefault();
    if (!onRequestCreateCamera) return;
    onRequestCreateCamera({
      x: event.clientX,
      y: event.clientY,
      flowPosition: screenToFlowPosition({ x: event.clientX, y: event.clientY }),
    });
  }, [onRequestCreateCamera, screenToFlowPosition]);

  return (
    <ReactFlow
      nodes={nodes}
      edges={edges}
      onNodesChange={onNodesChange}
      onEdgesChange={onEdgesChange}
      onConnect={onConnect}
      onNodeClick={onNodeClick}
      onPaneContextMenu={onPaneContextMenu}
      nodeTypes={cameraNodeTypes}
      minZoom={0.05}
      maxZoom={2}
      defaultViewport={{ x: 40, y: 40, zoom: 1 }}
      fitView
      fitViewOptions={CAMERA_FIT_VIEW_OPTIONS}
      deleteKeyCode={['Delete', 'Backspace']}
    >
      <Background variant={BackgroundVariant.Lines} gap={24} color="#2b2b2b" />
      <Controls />
      <MiniMap pannable zoomable />
    </ReactFlow>
  );
}

export default function CameraWorkspace(props: CameraWorkspaceProps) {
  return (
    <div className="pcg-camera-workspace">
      <ReactFlowProvider>
        <CameraWorkspaceCanvas {...props} />
      </ReactFlowProvider>
    </div>
  );
}
