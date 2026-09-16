import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
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
  addShotCamera,
  addShotMotionCurve,
  connectShotCameras,
  applyCameraGraphNodeChanges,
  connectCameraGraph,
  preserveFlowNodeLayout,
  shotCamerasToFlow,
} from './cameraGraph';
import CameraNode from './nodes/CameraNode';
import { downloadShot, parseShotFile } from './shotFile';
import { layoutShotGraph, selectShotNode, type ShotDocument } from './shot';

const cameraNodeTypes = {
  Camera: CameraNode,
  MotionCurve: CameraNode,
  CameraTransform: CameraNode,
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
      onKeyDown={(event) => {
        if (event.key.toLowerCase() === 'f' && !(event.target as HTMLElement).closest('input,select,textarea')) {
          event.preventDefault(); event.stopPropagation(); void fitView(CAMERA_FIT_VIEW_OPTIONS);
        }
      }}
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
      {nodes.length > 8 && <MiniMap pannable zoomable style={{ width: 120, height: 80 }} />}
    </ReactFlow>
  );
}

export default function CameraWorkspace(props: CameraWorkspaceProps) {
  const input = useRef<HTMLInputElement>(null);
  const [fileError, setFileError] = useState('');
  const [exportedShot, setExportedShot] = useState<{ path: string; url: string } | null>(null);
  return (
    <div className="pcg-camera-workspace">
      <div className="picg-camera-workspace-tools">
        <button onClick={() => props.onShotChange(addShotCamera(props.shot))}>+ Camera</button>
        <button onClick={() => props.onShotChange(layoutShotGraph(props.shot))}>Arrange</button>
        <button onClick={() => {
          let next = addShotMotionCurve(props.shot);
          next = connectShotCameras(next, next.selectedNodeId, next.activeCameraId);
          const id = next.selectedNodeId;
          props.onShotChange({ ...next, motionCurves: next.motionCurves.map((curve) => curve.id !== id ? curve : { ...curve,
            position: { x: next.cameras.find((camera) => camera.id === next.activeCameraId)!.position.x, y: 0 },
            cameraKeyframes: [
              { id: `${id}_start`, timeSeconds: 0, interpolation: 'linear', value: { pathProgress: 0 } },
              { id: `${id}_end`, timeSeconds: next.durationSeconds, interpolation: 'ease-in-out', value: { pathProgress: 1 } },
            ] }) });
        }}>+ Motion Curve → Camera</button>
        <button onClick={async () => {
          try { setExportedShot(await downloadShot(props.shot)); setFileError(''); }
          catch (error) { setFileError(error instanceof Error ? error.message : String(error)); }
        }}>Export shot</button>
        <button onClick={() => input.current?.click()}>Import shot</button>
        <input ref={input} hidden type="file" accept=".picgshot,.json" onChange={async (event) => {
          const file = event.target.files?.[0]; event.target.value = '';
          if (!file) return;
          try { props.onShotChange(parseShotFile(await file.text())); setFileError(''); }
          catch (error) { setFileError(error instanceof Error ? error.message : String(error)); }
        }} />
        {fileError && <span role="alert">{fileError}</span>}
        {exportedShot && <a className="picg-export-link" href={exportedShot.url} download title={exportedShot.path}>Saved shot</a>}
      </div>
      <ReactFlowProvider>
        <CameraWorkspaceCanvas {...props} />
      </ReactFlowProvider>
    </div>
  );
}
