import { Handle, Position, type NodeProps } from '@xyflow/react';

import { CAMERA_PIN_COLOR } from '../cameraGraph';

interface CameraNodeData {
  kind?: 'camera' | 'output' | 'curve' | 'transform';
  name?: string;
  bodyName?: string;
  presetId?: string;
  keyCount?: number;
  active?: boolean;
}

const PILL_COLOR = {
  camera: '#3d5a80',
  output: '#355a4a',
  curve: '#8a6230',
  transform: '#6b5790',
} as const;

export default function CameraNode({ type, selected, data }: NodeProps) {
  const node = data as CameraNodeData;
  const isOutput = type === 'ShotOutput' || node.kind === 'output';
  const isCurve = type === 'MotionCurve' || node.kind === 'curve';
  const kind = isOutput ? 'output' : isCurve ? 'curve' : node.kind === 'transform' ? 'transform' : 'camera';
  const bodyName = node.bodyName || (isCurve ? 'Motion Curve' : 'Camera');
  const stationName = node.name || (isOutput ? 'Shot Output' : bodyName);
  const previewing = node.active === true;

  return (
    <div className="pcg-node-wrapper">
      <div className={`pcg-node${selected ? ' pcg-node--selected' : ''}${previewing ? ' pcg-node--previewing' : ''}`}>
        <div className="pcg-node__stack">
          {!isCurve && (
            <div className="pcg-node__ports pcg-node__ports--top">
              <Handle
                type="target"
                position={Position.Top}
                id="in"
                className="pcg-node__handle"
                style={{ background: CAMERA_PIN_COLOR }}
                title={isOutput ? 'Shot' : 'Path'}
              />
            </div>
          )}
          <div className="pcg-node__pill" style={{ background: PILL_COLOR[kind] }} />
          {!isOutput && (
            <div className="pcg-node__ports pcg-node__ports--bottom">
              <Handle
                type="source"
                position={Position.Bottom}
                id="out"
                className="pcg-node__handle"
                style={{ background: isCurve ? '#e0a35a' : CAMERA_PIN_COLOR }}
                title={isCurve ? 'Path' : 'Camera'}
              />
            </div>
          )}
        </div>
        <div className="pcg-node__label">
          <div className="pcg-node__title">{isOutput ? stationName : bodyName}</div>
          {!isOutput && stationName !== bodyName && (
            <div className="pcg-node__subtitle">{stationName}</div>
          )}
        </div>
        {!isOutput && typeof node.keyCount === 'number' && node.keyCount > 0 && (
          <div className="pcg-node__group-badge">
            <span className="pcg-node__group-badge-count">
              {isCurve ? `${node.keyCount}P` : `${node.keyCount}K`}
            </span>
          </div>
        )}
      </div>
    </div>
  );
}
