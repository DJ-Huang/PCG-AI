import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

import AgentPanel from './AgentPanel';
import * as client from './agentClient';

vi.mock('./agentClient', async (loadOriginal) => {
  const original = await loadOriginal<typeof import('./agentClient')>();
  return {
    ...original,
    getProviders: vi.fn(),
    getAgentSettings: vi.fn(),
    setAgentSettings: vi.fn(),
    startTurn: vi.fn(),
    decideTurn: vi.fn(),
    cancelTurn: vi.fn(),
    connectApiKey: vi.fn(),
    disconnectProvider: vi.fn(),
    validateProvider: vi.fn(),
    startOAuth: vi.fn(),
    getOAuthStatus: vi.fn(),
    listAgentSessions: vi.fn(),
    getAgentSession: vi.fn(),
    renameAgentSession: vi.fn(),
    deleteAgentSession: vi.fn(),
  };
});

const connectedProvider: client.ProviderDescriptor = {
  id: 'openai', name: 'OpenAI', baseUrl: '',
  authMethods: [{ type: 'api', label: 'API Key', available: true }],
  connection: { status: 'connected', authType: 'api', accountLabel: '', error: null },
  models: [{
    id: 'gpt-test', name: 'GPT Test',
    capabilities: { toolCall: true, textInput: true, imageInput: true, contextTokens: 0, outputTokens: 0 },
  }],
};

describe('AgentPanel', () => {
  beforeEach(() => {
    vi.mocked(client.startTurn).mockReset();
    vi.mocked(client.getProviders).mockResolvedValue([connectedProvider]);
    vi.mocked(client.getAgentSettings).mockResolvedValue({ providerId: 'openai', modelId: 'gpt-test', reasoningEffort: 'high' });
    vi.mocked(client.setAgentSettings).mockResolvedValue();
    vi.mocked(client.cancelTurn).mockResolvedValue();
    vi.mocked(client.listAgentSessions).mockResolvedValue({ sessions: [], nextCursor: 0 });
    localStorage.clear();
  });

  afterEach(cleanup);

  it('defaults an approval request to approve', async () => {
    vi.mocked(client.startTurn).mockImplementation(async (_input, onEvent) => {
      onEvent({ type: 'turn.created', data: { turnId: 'turn-1' } });
      onEvent({
        type: 'approval.required',
        data: {
          turnId: 'turn-1',
          calls: [{
            turnId: 'turn-1', toolCallId: 'write-1', name: 'pcg_save_graph',
            arguments: { ifGraphHash: 'hash-1' }, requiresApproval: true,
          }],
        },
      });
    });
    vi.mocked(client.decideTurn).mockImplementation(async (_turn, _decisions, onEvent) => {
      onEvent({ type: 'turn.completed', data: { turnId: 'turn-1' } });
    });
    render(<AgentPanel onApplyActions={() => []} />);
    await screen.findByTitle('OpenAI · GPT Test');

    fireEvent.change(screen.getByPlaceholderText('Plan, build, @ nodes, attach refs…'), { target: { value: 'save it' } });
    fireEvent.click(screen.getByTitle('Send (Enter)'));
    expect(await screen.findByText('Approve this graph write?')).toBeInTheDocument();
    expect(screen.getByText('Approve')).toHaveClass('is-selected');
    fireEvent.click(screen.getByText('Continue'));

    await waitFor(() => expect(client.decideTurn).toHaveBeenCalledWith(
      'turn-1', [{ toolCallId: 'write-1', decision: 'approve' }], expect.any(Function), expect.any(AbortSignal),
    ));
  });

  it('synchronizes the editor context before starting a turn', async () => {
    let releaseSync: (() => void) | undefined;
    const syncEditorContext = vi.fn(() => new Promise<void>((resolve) => {
      releaseSync = resolve;
    }));
    vi.mocked(client.startTurn).mockResolvedValue();
    render(<AgentPanel onApplyActions={() => []} syncEditorContext={syncEditorContext} editorSessionId="editor-page-wood" />);
    await screen.findByTitle('OpenAI · GPT Test');

    fireEvent.change(screen.getByPlaceholderText('Plan, build, @ nodes, attach refs…'), { target: { value: 'inspect selection' } });
    fireEvent.click(screen.getByTitle('Send (Enter)'));

    await waitFor(() => expect(syncEditorContext).toHaveBeenCalledOnce());
    expect(client.startTurn).not.toHaveBeenCalled();

    releaseSync?.();
    await waitFor(() => expect(client.startTurn).toHaveBeenCalledOnce());
    expect(vi.mocked(client.startTurn).mock.calls[0][0]).toMatchObject({ editorSessionId: 'editor-page-wood' });
  });

  it('shows a retryable error when the Provider stops before a final answer', async () => {
    vi.mocked(client.startTurn).mockImplementation(async (_input, onEvent) => {
      onEvent({ type: 'turn.created', data: { turnId: 'turn-cut', messageId: 'assistant-cut' } });
      onEvent({ type: 'reasoning.started', data: {
        turnId: 'turn-cut', messageId: 'assistant-cut', partId: 'reasoning-cut', ordinal: 0,
      } });
      onEvent({ type: 'reasoning.delta', data: {
        turnId: 'turn-cut', messageId: 'assistant-cut', partId: 'reasoning-cut', text: 'Let me also',
      } });
      onEvent({ type: 'reasoning.completed', data: {
        turnId: 'turn-cut', messageId: 'assistant-cut', partId: 'reasoning-cut', text: 'Let me also',
      } });
      onEvent({ type: 'turn.error', data: { turnId: 'turn-cut', messageId: 'assistant-cut', error: {
        code: 'provider_output_truncated',
        message: 'The Provider reached its output limit before producing a final answer.',
        retryable: true,
      } } });
    });
    render(<AgentPanel onApplyActions={() => []} />);
    await screen.findByTitle('OpenAI · GPT Test');

    fireEvent.change(screen.getByPlaceholderText('Plan, build, @ nodes, attach refs…'), { target: { value: 'inspect it' } });
    fireEvent.click(screen.getByTitle('Send (Enter)'));

    expect(await screen.findByText('The Provider reached its output limit before producing a final answer.')).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Retry' })).toBeInTheDocument();
  });

  it('collapses left and restores the Agent panel', async () => {
    render(<AgentPanel onApplyActions={() => []} />);
    await screen.findByTitle('OpenAI · GPT Test');

    fireEvent.click(screen.getByRole('button', { name: 'Collapse Agent panel' }));
    expect(screen.getByRole('button', { name: 'Expand Agent panel' })).toBeInTheDocument();
    expect(screen.queryByPlaceholderText('Plan, build, @ nodes, attach refs…')).not.toBeInTheDocument();

    fireEvent.click(screen.getByRole('button', { name: 'Expand Agent panel' }));
    expect(await screen.findByRole('button', { name: 'Collapse Agent panel' })).toBeInTheDocument();
    expect(screen.getByPlaceholderText('Plan, build, @ nodes, attach refs…')).toBeInTheDocument();
  });
});
