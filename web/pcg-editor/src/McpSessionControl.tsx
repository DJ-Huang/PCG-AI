import { useEffect, useRef, useState, useSyncExternalStore } from 'react';
import { mcpSessionControl, sessionLabel, targetInstruction } from './mcpSessionState';
import './McpSessionControl.css';

/** The identity shown here is the same UUID used by the live editor bridge. */
export function McpSessionControl() {
  const session = useSyncExternalStore(mcpSessionControl.subscribe, mcpSessionControl.getSnapshot);
  const panel = useRef<HTMLDetailsElement>(null);
  const [copyStatus, setCopyStatus] = useState('');
  const ready = session.online && session.enabled && session.serverEnabled
    && session.revision === session.serverRevision;
  const label = sessionLabel(session.sessionId);
  const status = !session.online ? 'Server unavailable'
    : ready ? 'AI control allowed'
      : session.enabled ? 'Waiting for approval sync' : 'AI control off';

  useEffect(() => {
    if (!session.sessionId) return;
    const previous = document.title;
    document.title = `[${label}] ${session.graphPath.split(/[\\/]/).pop() || 'Untitled'}`;
    return () => { document.title = previous; };
  }, [session.sessionId, session.graphPath, label]);

  useEffect(() => { setCopyStatus(''); }, [session.sessionId, session.graphPath, session.revision]);

  if (!session.sessionId) return null;
  const copy = async (text: string) => {
    try {
      if (!navigator.clipboard?.writeText) throw new Error('Clipboard unavailable');
      await navigator.clipboard.writeText(text);
      setCopyStatus('Copied');
    } catch {
      setCopyStatus('Clipboard unavailable. Select and copy the text below.');
    }
  };

  return <details className="pcg-mcp-session" ref={panel} onKeyDown={(event) => {
    if (event.key === 'Escape' && panel.current) {
      panel.current.open = false;
      panel.current.querySelector('summary')?.focus();
      event.stopPropagation();
    }
  }}>
    <summary title={`${label} — ${status}`}>
      <span className={`pcg-mcp-session__dot ${ready ? 'is-ready' : ''}`} aria-hidden="true" />
      <span>MCP · {session.sessionId.slice(0, 8)}</span>
      <span className="pcg-mcp-session__state">{!session.online ? 'Offline' : ready ? 'Allowed' : session.enabled ? 'Pending' : 'Off'}</span>
    </summary>
    <section className="pcg-mcp-session__panel" aria-label="MCP session target">
      <strong>{label}</strong>
      <p role="status">{status}</p>
      <label className="pcg-mcp-session__toggle">
        <input type="checkbox" checked={session.enabled}
          onChange={(event) => mcpSessionControl.setEnabled(session.sessionId, event.target.checked)} />
        Allow AI control of this window
      </label>
      <p>Choose this visible window before asking AI to edit. Refreshing the page creates a new, unapproved session.</p>
      <label>Document<input readOnly value={session.graphPath || 'Untitled (unsaved)'} /></label>
      <label>Page<input readOnly value={session.pageUrl} /></label>
      <label>Full session ID<input readOnly value={session.sessionId} onFocus={(event) => event.target.select()} /></label>
      <div className="pcg-mcp-session__actions">
        <button type="button" onClick={() => void copy(session.sessionId)}>Copy ID</button>
        <button type="button" disabled={!ready} onClick={() => void copy(targetInstruction(session))}>Copy AI target</button>
      </div>
      <label>Target instruction<textarea readOnly rows={4} value={targetInstruction(session)}
        onFocus={(event) => event.target.select()} /></label>
      <p aria-live="polite">{copyStatus}</p>
      <p>Last MCP access: {session.lastMcpAccessAt
        ? new Date(session.lastMcpAccessAt).toLocaleTimeString() : 'None in this page session'}. Permission does not mean an AI client is connected.</p>
      <p>Turning control off cancels queued work. An operation already applying may finish; check the graph before retrying.</p>
    </section>
  </details>;
}
