import { useEffect, useRef, useState } from 'react';
import ReactMarkdown from 'react-markdown';
import remarkGfm from 'remark-gfm';

import type { AgentMessageRecord, AgentPart, ToolCallEvent } from './agentClient';

interface AgentMessageListProps {
  messages: AgentMessageRecord[];
  pendingCalls: ToolCallEvent[];
  decisions: Record<string, 'approve' | 'reject'>;
  onDecision: (callId: string, decision: 'approve' | 'reject') => void;
  onContinue: () => void;
  onRetry?: (turnId: string) => void;
  showReasoning?: boolean;
}

function copyText(text: string): void {
  void navigator.clipboard?.writeText(text);
}

function ThinkingPart({ part }: { part: AgentPart }) {
  const running = part.status === 'streaming';
  const [open, setOpen] = useState(running);
  useEffect(() => setOpen(running), [running]);
  return (
    <details className="pcg-agent-thinking" open={open} onToggle={(event) => setOpen(event.currentTarget.open)}>
      <summary><span className={running ? 'is-pulsing' : ''}>Thinking</span><small>{running ? ' reasoning…' : ' show reasoning'}</small></summary>
      <div className="pcg-agent-thinking__body"><ReactMarkdown remarkPlugins={[remarkGfm]}>{part.text ?? ''}</ReactMarkdown></div>
    </details>
  );
}

function resultValue(result: unknown): unknown {
  if (!result || typeof result !== 'object') return result;
  const value = result as { structuredContent?: unknown; content?: unknown };
  return value.structuredContent ?? value.content ?? result;
}

function toolSummary(part: AgentPart): string {
  const value = resultValue(part.result);
  if (!value || typeof value !== 'object') return part.status === 'running' ? 'Running…' : 'Completed';
  const data = value as Record<string, unknown>;
  if (typeof data.error === 'string') return data.error;
  if (Array.isArray(data.nodes)) return `${data.nodes.length} node(s)`;
  if (typeof data.count === 'number') return `${data.count} item(s)`;
  if (typeof data.graphHash === 'string') return `Graph ${data.graphHash.slice(0, 8)}`;
  if (data.ok === true) return 'Completed';
  return part.status === 'failed' ? 'Failed' : 'Result available';
}

function ToolPart({
  part, call, decision, onDecision, onContinue,
}: {
  part: AgentPart;
  call?: ToolCallEvent;
  decision?: 'approve' | 'reject';
  onDecision: (callId: string, decision: 'approve' | 'reject') => void;
  onContinue: () => void;
}) {
  const active = part.status === 'running' || part.status === 'approval_required' || part.status === 'failed';
  const [open, setOpen] = useState(active);
  useEffect(() => setOpen(active), [active]);
  const output = part.result === undefined ? '' : JSON.stringify(resultValue(part.result), null, 2);
  const input = JSON.stringify(part.arguments ?? {}, null, 2);
  return (
    <details className={`pcg-agent-tool is-${part.status ?? 'running'}`} open={open} onToggle={(event) => setOpen(event.currentTarget.open)}>
      <summary>
        <span className="pcg-agent-tool__state">{part.status === 'failed' ? '!' : part.status === 'completed' ? '✓' : part.status === 'approval_required' ? '◆' : '…'}</span>
        <span className="pcg-agent-tool__name">{part.name}</span>
        <small>{toolSummary(part)}{part.cached ? ' · cached' : ''}{part.durationMs ? ` · ${part.durationMs}ms` : ''}</small>
      </summary>
      <div className="pcg-agent-tool__body">
        <div className="pcg-agent-tool__section"><header>Input <button type="button" onClick={() => copyText(input)}>Copy</button></header><pre>{input}</pre></div>
        {output && <div className="pcg-agent-tool__section"><header>Result <button type="button" onClick={() => copyText(output)}>Copy</button></header><pre>{output}</pre></div>}
        {call && (
          <div className="pcg-agent-tool__approval">
            <strong>Approve this graph write?</strong>
            <div>
              <button type="button" className={decision === 'approve' ? 'is-selected' : ''} onClick={() => onDecision(call.toolCallId, 'approve')}>Approve</button>
              <button type="button" className={decision !== 'approve' ? 'is-selected is-reject' : ''} onClick={() => onDecision(call.toolCallId, 'reject')}>Reject</button>
              <button type="button" className="is-continue" onClick={onContinue}>Continue</button>
            </div>
          </div>
        )}
      </div>
    </details>
  );
}

function TextPart({ part }: { part: AgentPart }) {
  return (
    <div className={`pcg-agent-markdown ${part.status === 'streaming' ? 'is-streaming' : ''}`}>
      <ReactMarkdown remarkPlugins={[remarkGfm]}>{part.text ?? ''}</ReactMarkdown>
      {part.status !== 'streaming' && part.text && <button type="button" className="pcg-agent-copy" onClick={() => copyText(part.text ?? '')}>Copy</button>}
    </div>
  );
}

export default function AgentMessageList({ messages, pendingCalls, decisions, onDecision, onContinue, onRetry, showReasoning = true }: AgentMessageListProps) {
  const scrollerRef = useRef<HTMLDivElement>(null);
  const endRef = useRef<HTMLDivElement>(null);
  const [follow, setFollow] = useState(true);
  useEffect(() => { if (follow) endRef.current?.scrollIntoView({ block: 'end' }); }, [follow, messages]);

  return (
    <div className="pcg-agent__timeline-wrap">
      <div
        ref={scrollerRef}
        className="pcg-agent__messages"
        onScroll={(event) => {
          const element = event.currentTarget;
          setFollow(element.scrollHeight - element.scrollTop - element.clientHeight < 72);
        }}
      >
        {messages.length === 0 && <div className="pcg-agent__empty"><div className="pcg-agent__welcome-icon" aria-hidden="true">✦</div><strong>Hi! I’m your PCG assistant.</strong><p>Build, refine, and understand your procedural world. Describe what you want to create, or ask about the current graph.</p><span className="pcg-agent__welcome-hint">Start with an idea in the message box below.</span></div>}
        {messages.map((message) => (
          <article key={message.id} className={`pcg-agent-turn pcg-agent-turn--${message.role}`}>
            {message.role === 'user' ? (
              <div className="pcg-agent__message pcg-agent__message--user">
                {message.parts.filter((part) => part.type === 'text').map((part) => <span key={part.id}>{part.text}</span>)}
                {message.parts.filter((part) => part.type === 'attachment').map((part) => <small key={part.id}>📎 {part.name}</small>)}
              </div>
            ) : (
              <div className="pcg-agent-assistant">
                {[...message.parts].sort((a, b) => a.ordinal - b.ordinal).map((part) => {
                  if (part.type === 'reasoning') return showReasoning ? <ThinkingPart key={part.id} part={part} /> : null;
                  if (part.type === 'text') return <TextPart key={part.id} part={part} />;
                  if (part.type === 'tool') {
                    const call = pendingCalls.find((item) => item.toolCallId === part.toolCallId);
                    return <ToolPart key={part.id} part={part} call={call} decision={part.toolCallId ? decisions[part.toolCallId] : undefined} onDecision={onDecision} onContinue={onContinue} />;
                  }
                  if (part.type === 'error') return <div key={part.id} className="pcg-agent-error"><strong>{part.error?.message ?? 'Agent turn failed'}</strong>{part.error?.retryable && message.turnId && onRetry && <button type="button" onClick={() => onRetry(message.turnId!)}>Retry</button>}</div>;
                  return null;
                })}
                {message.status === 'interrupted' && <div className="pcg-agent-interrupted">Turn stopped{message.turnId && onRetry && <button type="button" onClick={() => onRetry(message.turnId!)}>Retry</button>}</div>}
              </div>
            )}
          </article>
        ))}
        <div ref={endRef} />
      </div>
      {!follow && <button type="button" className="pcg-agent__jump-latest" onClick={() => { setFollow(true); endRef.current?.scrollIntoView({ behavior: 'smooth' }); }}>↓ Latest</button>}
    </div>
  );
}
