import { useMemo } from 'react';

import type { AgentSessionDescriptor } from './agentClient';

interface AgentHistoryProps {
  sessions: AgentSessionDescriptor[];
  activeSessionId: string;
  query: string;
  loading: boolean;
  onQueryChange: (query: string) => void;
  onOpen: (sessionId: string) => void;
  onRename: (session: AgentSessionDescriptor) => void;
  onDelete: (session: AgentSessionDescriptor) => void;
  onClose: () => void;
  onLoadMore?: () => void;
  hasMore?: boolean;
}

function dateGroup(timestamp: number): string {
  const now = new Date();
  const date = new Date(timestamp);
  const start = new Date(now.getFullYear(), now.getMonth(), now.getDate()).getTime();
  const day = 24 * 60 * 60 * 1000;
  if (timestamp >= start) return 'Today';
  if (timestamp >= start - day) return 'Yesterday';
  if (timestamp >= start - 7 * day) return 'Previous 7 Days';
  return date.getFullYear() === now.getFullYear() ? 'Older' : String(date.getFullYear());
}

export default function AgentHistory(props: AgentHistoryProps) {
  const grouped = useMemo(() => {
    const result = new Map<string, AgentSessionDescriptor[]>();
    for (const session of props.sessions) {
      const group = dateGroup(session.updatedAt);
      result.set(group, [...(result.get(group) ?? []), session]);
    }
    return [...result.entries()];
  }, [props.sessions]);

  return (
    <div className="pcg-agent-history">
      <header><strong>Chat history</strong><button type="button" onClick={props.onClose} aria-label="Close chat history">×</button></header>
      <input
        type="search"
        value={props.query}
        placeholder="Search chats"
        aria-label="Search chat history"
        onChange={(event) => props.onQueryChange(event.target.value)}
      />
      <div className="pcg-agent-history__list">
        {props.loading && <div className="pcg-agent-history__empty">Loading…</div>}
        {!props.loading && grouped.length === 0 && <div className="pcg-agent-history__empty">No chats found</div>}
        {grouped.map(([group, sessions]) => (
          <section key={group}>
            <h3>{group}</h3>
            {sessions.map((session) => (
              <div key={session.id} className={`pcg-agent-history__item ${session.id === props.activeSessionId ? 'is-active' : ''}`}>
                <button type="button" className="pcg-agent-history__open" onClick={() => props.onOpen(session.id)}>
                  <strong>{session.title}</strong>
                  <span>{session.graphName || session.modelId || 'PICG Agent'} · {new Date(session.updatedAt).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })}</span>
                </button>
                <div className="pcg-agent-history__actions">
                  <button type="button" title="Rename chat" onClick={() => props.onRename(session)}>✎</button>
                  <button type="button" title="Delete chat" onClick={() => props.onDelete(session)}>×</button>
                </div>
              </div>
            ))}
          </section>
        ))}
        {props.hasMore && <button type="button" className="pcg-agent-history__more" onClick={props.onLoadMore}>Load older chats</button>}
      </div>
    </div>
  );
}
