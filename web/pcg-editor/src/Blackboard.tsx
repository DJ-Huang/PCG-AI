// Blackboard.tsx — Left panel for graph parameter definitions.
// Parameters can be bound to node properties via the Inspector.

import type { GraphParameter, ParameterType } from './graphSchema';
import type { Node } from '@xyflow/react';

interface BlackboardProps {
  parameters: GraphParameter[];
  nodes: Node[];
  onParametersChange: (params: GraphParameter[]) => void;
}

const PARAM_TYPES: ParameterType[] = ['integer', 'number', 'boolean', 'string', 'vector3'];

let paramCounter = 0;

function nextParamId(): string {
  return `p-${Date.now()}-${paramCounter++}`;
}

export default function Blackboard({ parameters, nodes, onParametersChange }: BlackboardProps) {
  const addParameter = () => {
    const newParam: GraphParameter = {
      id: nextParamId(),
      name: `Param${parameters.length + 1}`,
      type: 'number',
      default: 0,
      exposed: true,
      targetNode: '',
      targetProperty: '',
      hasRange: false,
      min: 0,
      max: 1,
    };
    onParametersChange([...parameters, newParam]);
  };

  const updateParam = (id: string, patch: Partial<GraphParameter>) => {
    onParametersChange(
      parameters.map((p) => (p.id === id ? { ...p, ...patch } : p)),
    );
  };

  const removeParam = (id: string) => {
    onParametersChange(parameters.filter((p) => p.id !== id));
  };

  const getBindingDisplay = (param: GraphParameter): string => {
    if (!param.targetNode || !param.targetProperty) return '';
    const node = nodes.find((n) => n.id === param.targetNode);
    const nodeName = node?.type ?? param.targetNode;
    return `${nodeName}.${param.targetProperty}`;
  };

  return (
    <div className="pcg-blackboard">
      <div className="pcg-blackboard__header">
        <span>Parameters</span>
        <button type="button" className="pcg-blackboard__add" onClick={addParameter}>
          + Add
        </button>
      </div>
      <div className="pcg-blackboard__list">
        {parameters.length === 0 && (
          <div className="pcg-blackboard__empty">No parameters. Click "+ Add" to create one.</div>
        )}
        {parameters.map((param) => (
          <div key={param.id} className="pcg-blackboard__param">
            <div className="pcg-blackboard__param-row">
              <input
                type="text"
                className="pcg-blackboard__param-name"
                value={param.name}
                onChange={(e) => updateParam(param.id, { name: e.target.value })}
                placeholder="Name"
              />
              <button
                type="button"
                className="pcg-blackboard__param-remove"
                onClick={() => removeParam(param.id)}
                title="Remove parameter"
              >
                −
              </button>
            </div>
            <div className="pcg-blackboard__param-row">
              <select
                value={param.type}
                onChange={(e) => {
                  const newType = e.target.value as ParameterType;
                  const newDefault =
                    newType === 'boolean' ? false
                    : newType === 'string' ? ''
                    : newType === 'vector3' ? ([0, 0, 0] as [number, number, number])
                    : 0;
                  updateParam(param.id, { type: newType, default: newDefault });
                }}
              >
                {PARAM_TYPES.map((t) => (
                  <option key={t} value={t}>{t}</option>
                ))}
              </select>
              <label className="pcg-blackboard__exposed">
                <input
                  type="checkbox"
                  checked={param.exposed}
                  onChange={(e) => updateParam(param.id, { exposed: e.target.checked })}
                />
                Exposed
              </label>
            </div>
            <div className="pcg-blackboard__param-row">
              <span className="pcg-blackboard__param-label">Default:</span>
              {param.type === 'boolean' ? (
                <input
                  type="checkbox"
                  checked={Boolean(param.default)}
                  onChange={(e) => updateParam(param.id, { default: e.target.checked })}
                />
              ) : param.type === 'string' ? (
                <input
                  type="text"
                  value={String(param.default)}
                  onChange={(e) => updateParam(param.id, { default: e.target.value })}
                />
              ) : param.type === 'vector3' ? (
                <div className="pcg-inspector__vector3">
                  {([0, 1, 2] as const).map((axis) => {
                    const components = Array.isArray(param.default) && param.default.length >= 3
                      ? param.default as [number, number, number]
                      : ([0, 0, 0] as [number, number, number]);
                    return (
                      <input
                        key={axis}
                        type="number"
                        step="0.1"
                        value={components[axis]}
                        onChange={(e) => {
                          const next: [number, number, number] = [...components];
                          next[axis] = Number(e.target.value) || 0;
                          updateParam(param.id, { default: next });
                        }}
                      />
                    );
                  })}
                </div>
              ) : (
                <input
                  type="number"
                  value={Number(param.default)}
                  step="0.1"
                  onChange={(e) => updateParam(param.id, { default: Number(e.target.value) })}
                />
              )}
            </div>
            {(param.type === 'integer' || param.type === 'number') && (
              <>
                <div className="pcg-blackboard__param-row">
                  <label className="pcg-blackboard__range-toggle">
                    <input
                      type="checkbox"
                      checked={param.hasRange}
                      onChange={(e) => updateParam(param.id, { hasRange: e.target.checked })}
                    />
                    Range
                  </label>
                </div>
                {param.hasRange && (
                  <div className="pcg-blackboard__param-row">
                    <span className="pcg-blackboard__param-label">Min:</span>
                    <input
                      type="number"
                      step="0.1"
                      value={param.min}
                      onChange={(e) => updateParam(param.id, { min: Number(e.target.value) })}
                    />
                    <span className="pcg-blackboard__param-label">Max:</span>
                    <input
                      type="number"
                      step="0.1"
                      value={param.max}
                      onChange={(e) => updateParam(param.id, { max: Number(e.target.value) })}
                    />
                  </div>
                )}
              </>
            )}
            {getBindingDisplay(param) && (
              <div className="pcg-blackboard__binding">
                → {getBindingDisplay(param)}
              </div>
            )}
          </div>
        ))}
      </div>
    </div>
  );
}
