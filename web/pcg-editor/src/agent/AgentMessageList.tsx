// AgentMessageList.tsx — Chat bubbles for user / assistant messages plus
// action execution records.

import { useEffect, useRef } from 'react';

export interface AgentMessage {
  id: string;
  role: 'user' | 'assistant' | 'action';
  text: string;
  attachments?: { name: string }[];
}

interface AgentMessageListProps {
  messages: AgentMessage[];
}

export default function AgentMessageList({ messages }: AgentMessageListProps) {
  const endRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    endRef.current?.scrollIntoView({ block: 'end' });
  }, [messages.length]);

  return (
    <div className="pcg-agent__messages">
      {messages.length === 0 && (
        <div className="pcg-agent__empty">
          Ask the agent to operate the graph. Mock backend — no LLM connected yet.
        </div>
      )}
      {messages.map((msg) =>
        msg.role === 'action' ? (
          <div key={msg.id} className="pcg-agent__action">
            {msg.text}
          </div>
        ) : (
          <div key={msg.id} className={`pcg-agent__message pcg-agent__message--${msg.role}`}>
            {msg.text}
            {msg.attachments && msg.attachments.length > 0 && (
              <div className="pcg-agent__message-attachments">
                {msg.attachments.map((a, i) => (
                  <span key={`${msg.id}-att-${i}`} className="pcg-agent__message-attachment">
                    📎 {a.name}
                  </span>
                ))}
              </div>
            )}
          </div>
        ),
      )}
      <div ref={endRef} />
    </div>
  );
}
