// AgentPanel.tsx — Left-side agent panel: message list + composer. Sends chat
// to the pcg-server mock endpoint (local echo fallback), then dispatches the
// returned graph actions through the editor (one undo step per turn).

import { useCallback, useRef, useState } from 'react';

import AgentComposer, { type AgentAttachment } from './AgentComposer';
import AgentMessageList, { type AgentMessage } from './AgentMessageList';
import { localEchoReply, sendAgentChat } from './agentClient';
import type { AgentAction, AgentActionResult } from './agentCommands';

interface AgentPanelProps {
  onApplyActions: (actions: AgentAction[]) => AgentActionResult[];
}

const MIN_WIDTH = 240;
const MAX_WIDTH = 640;

let messageCounter = 0;
const nextMessageId = () => `m${++messageCounter}`;

export default function AgentPanel({ onApplyActions }: AgentPanelProps) {
  const [messages, setMessages] = useState<AgentMessage[]>([]);
  const [sending, setSending] = useState(false);
  const [width, setWidth] = useState(320);
  const abortRef = useRef<AbortController | null>(null);

  const onResizeStart = useCallback(
    (e: React.MouseEvent) => {
      e.preventDefault();
      const startX = e.clientX;
      const startWidth = width;
      const onMove = (ev: MouseEvent) => {
        setWidth(Math.min(MAX_WIDTH, Math.max(MIN_WIDTH, startWidth + ev.clientX - startX)));
      };
      const onUp = () => {
        window.removeEventListener('mousemove', onMove);
        window.removeEventListener('mouseup', onUp);
        document.body.classList.remove('pcg-agent--resizing');
      };
      document.body.classList.add('pcg-agent--resizing');
      window.addEventListener('mousemove', onMove);
      window.addEventListener('mouseup', onUp);
    },
    [width],
  );

  const push = useCallback((msg: AgentMessage) => {
    setMessages((prev) => [...prev, msg]);
  }, []);

  const handleSend = useCallback(
    async (text: string, attachments: AgentAttachment[]) => {
      push({
        id: nextMessageId(),
        role: 'user',
        text: text || '(attachments only)',
        attachments: attachments.map((a) => ({ name: a.file.name })),
      });
      setSending(true);
      const abort = new AbortController();
      abortRef.current = abort;

      const request = {
        message: text,
        attachments: attachments.map((a) => ({
          name: a.file.name,
          size: a.file.size,
          type: a.file.type,
        })),
      };
      let response = await sendAgentChat(request, abort.signal);
      if (!response.ok && response.error !== 'aborted') {
        response = localEchoReply(request, response.error ?? 'unknown error');
      }
      if (abortRef.current !== abort) return;
      abortRef.current = null;
      setSending(false);

      if (response.error === 'aborted') {
        push({ id: nextMessageId(), role: 'assistant', text: '(stopped)' });
        return;
      }
      if (response.reply) {
        push({ id: nextMessageId(), role: 'assistant', text: response.reply });
      }
      if (response.actions && response.actions.length > 0) {
        const results = onApplyActions(response.actions);
        for (const r of results) {
          push({
            id: nextMessageId(),
            role: 'action',
            text: `${r.ok ? '✓' : '✗'} ${r.detail}`,
          });
        }
      }
    },
    [push, onApplyActions],
  );

  const handleStop = useCallback(() => {
    abortRef.current?.abort();
    abortRef.current = null;
    setSending(false);
    push({ id: nextMessageId(), role: 'assistant', text: '(stopped)' });
  }, [push]);

  return (
    <div className="pcg-agent" style={{ width }}>
      <div className="pcg-agent__header">
        <span className="pcg-agent__title">Agent</span>
        <span className="pcg-agent__badge">mock</span>
      </div>
      <AgentMessageList messages={messages} />
      <AgentComposer sending={sending} onSend={handleSend} onStop={handleStop} />
      <div
        className="pcg-agent__resize-handle"
        onMouseDown={onResizeStart}
        title="Drag to resize"
      />
    </div>
  );
}
