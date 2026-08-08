// SubgraphNode.tsx — Inline subgraph instance node.
// Pins and title derive from the referenced document-level subgraph definition
// (data.subgraphId), mirroring Unity's PcgSubgraphNodeView. Edges whose handles
// no longer exist in the definition render as ghost pins so drift is visible
// instead of silently broken.

import { useState, useContext, useRef, useEffect } from 'react';
import { Handle, Position, NodeToolbar, useEdges, type NodeProps } from '@xyflow/react';
import { getPinTypeColor } from '../nodeManifest';
import { NodeActionsContext, useIsPreviewTarget } from '../nodeActions';
import {
  findSubgraph,
  getSubgraphId,
  getSubgraphNodeTitle,
  useCurrentSubgraph,
  useSubgraphs,
} from '../subgraphs';

export default function SubgraphNode({ id, selected, data }: NodeProps) {
  const [showContents, setShowContents] = useState(false);
  const [hovered, setHovered] = useState(false);
  const hideTimer = useRef<ReturnType<typeof setTimeout> | null>(null);
  const { onInfo, onPreview } = useContext(NodeActionsContext);
  const previewing = useIsPreviewTarget(id);
  const subgraphs = useSubgraphs();
  const edges = useEdges();

  const showToolbar = () => {
    if (hideTimer.current) clearTimeout(hideTimer.current);
    setHovered(true);
  };
  const scheduleHideToolbar = () => {
    if (hideTimer.current) clearTimeout(hideTimer.current);
    hideTimer.current = setTimeout(() => setHovered(false), 250);
  };
  useEffect(() => () => {
    if (hideTimer.current) clearTimeout(hideTimer.current);
  }, []);

  const subgraphId = getSubgraphId(data);
  const subgraph = findSubgraph(subgraphs, subgraphId);
  const title = getSubgraphNodeTitle(data, subgraph);
  const missing = !subgraph;

  const inputPins = subgraph?.inputs ?? [];
  const outputPins = subgraph?.outputs ?? [];

  // Ghost pins: connected handles the definition no longer declares.
  const ghostInputs: string[] = [];
  const ghostOutputs: string[] = [];
  const knownInputs = new Set(inputPins.map((p) => p.id));
  const knownOutputs = new Set(outputPins.map((p) => p.id));
  for (const edge of edges) {
    if (edge.target === id) {
      const handle = edge.targetHandle ?? 'in';
      if (!knownInputs.has(handle) && !ghostInputs.includes(handle)) ghostInputs.push(handle);
    }
    if (edge.source === id) {
      const handle = edge.sourceHandle ?? 'out';
      if (!knownOutputs.has(handle) && !ghostOutputs.includes(handle)) ghostOutputs.push(handle);
    }
  }

  return (
    <div
      className="pcg-node-wrapper"
      onMouseEnter={showToolbar}
      onMouseLeave={scheduleHideToolbar}
    >
      <NodeToolbar
        isVisible={hovered}
        position={Position.Top}
        offset={6}
        className="pcg-node-toolbar"
        onMouseEnter={showToolbar}
        onMouseLeave={scheduleHideToolbar}
      >
        <button
          type="button"
          className="nodrag pcg-node-toolbar__btn"
          title="Node info"
          onClick={(e) => {
            e.stopPropagation();
            onInfo(id);
          }}
        >
          ℹ
        </button>
        <button
          type="button"
          className={`nodrag pcg-node-toolbar__btn${previewing ? ' pcg-node-toolbar__btn--previewing' : ''}`}
          title={
            outputPins.length === 0
              ? 'No output to preview'
              : previewing
                ? 'Clear node preview (back to full-graph preview)'
                : 'Preview this node'
          }
          disabled={outputPins.length === 0}
          onClick={(e) => {
            e.stopPropagation();
            onPreview(id);
          }}
        >
          ▶
        </button>
      </NodeToolbar>

      <div
        className={`pcg-node pcg-node--subgraph${selected ? ' pcg-node--selected' : ''}${previewing ? ' pcg-node--previewing' : ''}${missing ? ' pcg-node--missing' : ''}`}
      >
        <div className="pcg-node__stack">
          {(inputPins.length > 0 || ghostInputs.length > 0) && (
            <div className="pcg-node__ports pcg-node__ports--top">
              {inputPins.map((pin) => (
                <Handle
                  key={pin.id}
                  type="target"
                  position={Position.Top}
                  id={pin.id}
                  className="pcg-node__handle"
                  style={{ background: getPinTypeColor('Any') }}
                  title={`${pin.name} (Any)`}
                />
              ))}
              {ghostInputs.map((handle) => (
                <Handle
                  key={`ghost-${handle}`}
                  type="target"
                  position={Position.Top}
                  id={handle}
                  className="pcg-node__handle pcg-node__handle--ghost"
                  title={`${handle} (missing from subgraph definition)`}
                />
              ))}
            </div>
          )}

          <div className="pcg-node__pill" />

          {(outputPins.length > 0 || ghostOutputs.length > 0) && (
            <div className="pcg-node__ports pcg-node__ports--bottom">
              {outputPins.map((pin) => (
                <Handle
                  key={pin.id}
                  type="source"
                  position={Position.Bottom}
                  id={pin.id}
                  className="pcg-node__handle"
                  style={{ background: getPinTypeColor(pin.pinType) }}
                  title={`${pin.name} (${pin.pinType || 'Any'})`}
                />
              ))}
              {ghostOutputs.map((handle) => (
                <Handle
                  key={`ghost-${handle}`}
                  type="source"
                  position={Position.Bottom}
                  id={handle}
                  className="pcg-node__handle pcg-node__handle--ghost"
                  title={`${handle} (missing from subgraph definition)`}
                />
              ))}
            </div>
          )}
        </div>

        <div className="pcg-node__title">
          {title}
          <span className="pcg-node__type-subtitle">subgraph</span>
        </div>

        {missing && (
          <div className="pcg-node__missing-badge" title={`Subgraph definition "${subgraphId || '(unset)'}" is not in this document`}>
            ⚠
          </div>
        )}

        {subgraph && subgraph.nodes.length > 0 && (
          <div
            className="pcg-node__group-badge"
            onMouseEnter={() => setShowContents(true)}
            onMouseLeave={() => setShowContents(false)}
          >
            <span className="pcg-node__group-badge-count">
              ⧉{subgraph.nodes.length}
            </span>
            {showContents && (
              <div className="pcg-node__group-badge-popup">
                <div className="pcg-node__group-badge-popup-title">Subgraph Contents</div>
                {subgraph.nodes.map((n) => (
                  <div key={n.id} className="pcg-node__group-badge-popup-row">
                    <span className="pcg-node__group-badge-popup-name">{n.id}</span>
                    <span className="pcg-node__group-badge-popup-domain">{n.type}</span>
                  </div>
                ))}
                {ghostInputs.length + ghostOutputs.length > 0 && (
                  <>
                    <div className="pcg-node__group-badge-popup-sep" />
                    <div className="pcg-node__group-badge-popup-title">Ghost Pins (stale edges)</div>
                    {[...ghostInputs, ...ghostOutputs].map((handle) => (
                      <div key={handle} className="pcg-node__group-badge-popup-row pcg-node__group-badge-popup-row--inactive">
                        <span className="pcg-node__group-badge-popup-name">{handle}</span>
                        <span className="pcg-node__group-badge-popup-domain">missing</span>
                      </div>
                    ))}
                  </>
                )}
              </div>
            )}
          </div>
        )}
      </div>
    </div>
  );
}

/**
 * Structural interface nodes (SubgraphInput/SubgraphOutput). Inside a subgraph
 * they expose the definition's pins (Unity PcgSubgraphNodeView kind=Input/Output):
 * SubgraphInput outputs the definition's input pins; SubgraphOutput takes the
 * definition's output pins as inputs.
 */
export function SubgraphInterfaceNode({ type, selected }: NodeProps) {
  const currentSubgraph = useCurrentSubgraph();
  const isInput = type === 'SubgraphInput';

  // Definition pins: Input → outputs from def.inputs, Output → inputs from def.outputs.
  const pins = currentSubgraph
    ? isInput
      ? currentSubgraph.inputs
      : currentSubgraph.outputs
    : [];
  const fallbackPin = { id: isInput ? 'out' : 'in', name: isInput ? 'out' : 'in', pinType: 'Any' };
  const shownPins = pins.length > 0 ? pins : [fallbackPin];

  return (
    <div className={`pcg-node pcg-node--subgraph pcg-node--interface${selected ? ' pcg-node--selected' : ''}`}>
      <div className="pcg-node__stack">
        {!isInput && (
          <div className="pcg-node__ports pcg-node__ports--top">
            {shownPins.map((pin) => (
              <Handle
                key={pin.id}
                type="target"
                position={Position.Top}
                id={pin.id}
                className="pcg-node__handle"
                style={{ background: getPinTypeColor(pin.pinType || 'Any') }}
                title={`${pin.name} (${pin.pinType || 'Any'})`}
              />
            ))}
          </div>
        )}
        <div className="pcg-node__pill" />
        {isInput && (
          <div className="pcg-node__ports pcg-node__ports--bottom">
            {shownPins.map((pin) => (
              <Handle
                key={pin.id}
                type="source"
                position={Position.Bottom}
                id={pin.id}
                className="pcg-node__handle"
                style={{ background: getPinTypeColor(pin.pinType || 'Any') }}
                title={`${pin.name} (${pin.pinType || 'Any'})`}
              />
            ))}
          </div>
        )}
      </div>
      <div className="pcg-node__title">
        {isInput ? 'Subgraph Input' : 'Subgraph Output'}
        <span className="pcg-node__type-subtitle">interface</span>
      </div>
    </div>
  );
}
