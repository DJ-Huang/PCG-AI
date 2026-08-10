import { render, screen, waitFor } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';

import AgentMessageList from './AgentMessageList';
import type { AgentMessageRecord } from './agentClient';

const baseMessage: AgentMessageRecord = {
  id: 'assistant-1',
  role: 'assistant',
  turnId: 'turn-1',
  status: 'running',
  createdAt: 1,
  parts: [
    { id: 'reasoning-1', type: 'reasoning', ordinal: 0, status: 'streaming', text: 'Inspecting the graph' },
    {
      id: 'tool-1', type: 'tool', ordinal: 1, status: 'completed', name: 'pcg_get_graph',
      toolCallId: 'call-1', arguments: {}, durationMs: 12,
      result: { structuredContent: { ok: true, graphHash: '1234567890abcdef', nodes: [{ id: 'n1' }] } },
    },
    { id: 'text-1', type: 'text', ordinal: 2, status: 'completed', text: '## Done\nThe graph is valid.' },
  ],
};

describe('AgentMessageList', () => {
  it('renders ordered reasoning, one merged tool card, full output, and Markdown', async () => {
    const props = { pendingCalls: [], decisions: {}, onDecision: vi.fn(), onContinue: vi.fn() };
    const { rerender } = render(<AgentMessageList messages={[baseMessage]} {...props} />);

    const thinking = screen.getByText('Thinking').closest('details');
    expect(thinking).toHaveAttribute('open');
    expect(screen.getAllByText('pcg_get_graph')).toHaveLength(1);
    expect(screen.getByText(/"graphHash": "1234567890abcdef"/)).toBeInTheDocument();
    expect(screen.getByRole('heading', { name: 'Done' })).toBeInTheDocument();

    rerender(<AgentMessageList messages={[{
      ...baseMessage,
      status: 'completed',
      parts: baseMessage.parts.map((part) => part.type === 'reasoning' ? { ...part, status: 'completed' as const } : part),
    }]} {...props} />);
    await waitFor(() => expect(screen.getByText('Thinking').closest('details')).not.toHaveAttribute('open'));
  });
});
