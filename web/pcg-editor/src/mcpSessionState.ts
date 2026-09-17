/** Per-page MCP consent. Never persist it or infer it from the document name. */
export interface McpSessionSnapshot {
  sessionId: string;
  graphPath: string;
  pageUrl: string;
  enabled: boolean;
  revision: number;
  online: boolean;
  serverEnabled: boolean;
  serverRevision: number;
  lastMcpAccessAt: number;
}

export interface McpSessionStatus {
  aiControlEnabled?: boolean;
  aiControlRevision?: number;
  lastMcpAccessAt?: number;
}

const emptySnapshot = (): McpSessionSnapshot => ({
  sessionId: '', graphPath: '', pageUrl: '', enabled: false, revision: 0,
  online: false, serverEnabled: false, serverRevision: -1, lastMcpAccessAt: 0,
});

export function sessionLabel(sessionId: string): string {
  return `PICG ${sessionId.slice(0, 8)}`;
}

export function targetInstruction(session: McpSessionSnapshot): string {
  return `Use only editorSessionId="${session.sessionId}" (${sessionLabel(session.sessionId)}). `
    + `The visible document is ${JSON.stringify(session.graphPath || 'Untitled (unsaved)')} `
    + `at ${session.pageUrl}. Confirm this target before authoring and pass this full ID on every live MCP call. `
    + 'Do not choose another session if this one is unavailable. This selects a window, not permission to overwrite unrelated content.';
}

export function createMcpSessionControl() {
  let snapshot = emptySnapshot();
  const listeners = new Set<() => void>();
  const update = (next: McpSessionSnapshot) => {
    if (Object.keys(next).every((key) => next[key as keyof McpSessionSnapshot] === snapshot[key as keyof McpSessionSnapshot])) return;
    snapshot = next;
    listeners.forEach((listener) => listener());
  };
  return {
    getSnapshot: () => snapshot,
    subscribe: (listener: () => void) => {
      listeners.add(listener);
      return () => { listeners.delete(listener); };
    },
    attach: (sessionId: string, graphPath: string, pageUrl: string) => {
      update({ ...(snapshot.sessionId === sessionId ? snapshot : emptySnapshot()), sessionId, graphPath, pageUrl });
    },
    setEnabled: (sessionId: string, enabled: boolean) => {
      if (!sessionId || snapshot.sessionId !== sessionId || snapshot.enabled === enabled) return;
      update({ ...snapshot, enabled, revision: snapshot.revision + 1 });
    },
    report: (sessionId: string, status: McpSessionStatus) => {
      if (snapshot.sessionId !== sessionId) return;
      const revision = status.aiControlRevision ?? -1;
      // An older request can finish after the user revoked consent. It cannot
      // re-enable local control or advertise an old generation as ready.
      if (revision < snapshot.serverRevision) return;
      update({ ...snapshot, online: true, serverEnabled: status.aiControlEnabled === true,
        serverRevision: revision, lastMcpAccessAt: status.lastMcpAccessAt ?? 0 });
    },
    disconnect: (sessionId: string) => {
      if (snapshot.sessionId === sessionId) update({ ...snapshot, online: false, serverEnabled: false });
    },
    allows: (sessionId: string, revision: number | undefined) =>
      snapshot.sessionId === sessionId && snapshot.enabled && snapshot.revision === revision,
  };
}

export const mcpSessionControl = createMcpSessionControl();
