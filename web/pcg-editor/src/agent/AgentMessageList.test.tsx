import { cleanup, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

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
  afterEach(cleanup);

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

  it('only offers Retry for errors marked safe to replay', () => {
    const props = { pendingCalls: [], decisions: {}, onDecision: vi.fn(), onContinue: vi.fn(), onRetry: vi.fn() };
    const failed: AgentMessageRecord = {
      ...baseMessage,
      status: 'error',
      parts: [{
        id: 'error-1', type: 'error', ordinal: 0, status: 'error',
        error: { code: 'provider_error', message: 'Invalid tool result image.', retryable: false },
      }],
    };
    const { rerender } = render(<AgentMessageList messages={[failed]} {...props} />);
    expect(screen.queryByRole('button', { name: 'Retry' })).not.toBeInTheDocument();

    rerender(<AgentMessageList messages={[{
      ...failed,
      parts: [{ ...failed.parts[0], error: { ...failed.parts[0].error!, retryable: true } }],
    }]} {...props} />);
    expect(screen.getByRole('button', { name: 'Retry' })).toBeInTheDocument();
  });
});
