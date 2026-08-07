// agentCommands.ts — AgentAction types and dispatch into the graph.
// Every dispatch batch is wrapped in a single commit() so one undo step
// reverts the whole agent turn.

export type AgentAction =
  | { type: 'addNode'; nodeType: string; position?: { x: number; y: number } }
  | { type: 'connectNodes'; source: string; target: string; sourceHandle?: string; targetHandle?: string }
  | { type: 'setNodeParam'; nodeId: string; key: string; value: unknown };

export interface AgentActionResult {
  action: AgentAction;
  ok: boolean;
  detail: string;
}

export interface AgentGraphOps {
  /** Returns the new node id, or throws an Error with the reason. */
  addNode(nodeType: string, position?: { x: number; y: number }): string;
  connectNodes(source: string, target: string, sourceHandle?: string, targetHandle?: string): void;
  setNodeParam(nodeId: string, key: string, value: unknown): void;
}

function describe(action: AgentAction): string {
  switch (action.type) {
    case 'addNode':
      return `addNode ${action.nodeType}`;
    case 'connectNodes':
      return `connectNodes ${action.source} → ${action.target}`;
    case 'setNodeParam':
      return `setNodeParam ${action.nodeId}.${action.key}`;
  }
}

export function dispatchAgentActions(
  actions: AgentAction[],
  ops: AgentGraphOps,
  commit: () => void,
): AgentActionResult[] {
  if (actions.length === 0) return [];
  commit();
  return actions.map((action) => {
    try {
      switch (action.type) {
        case 'addNode': {
          const id = ops.addNode(action.nodeType, action.position);
          return { action, ok: true, detail: `${describe(action)} → ${id}` };
        }
        case 'connectNodes':
          ops.connectNodes(action.source, action.target, action.sourceHandle, action.targetHandle);
          return { action, ok: true, detail: describe(action) };
        case 'setNodeParam':
          ops.setNodeParam(action.nodeId, action.key, action.value);
          return { action, ok: true, detail: describe(action) };
      }
    } catch (err) {
      return { action, ok: false, detail: `${describe(action)} failed: ${(err as Error).message}` };
    }
  });
}
