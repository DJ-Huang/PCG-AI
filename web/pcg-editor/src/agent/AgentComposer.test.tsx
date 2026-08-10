import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

import AgentComposer from './AgentComposer';

function renderComposer(onSend = vi.fn()) {
  render(
    <AgentComposer
      sending={false}
      agentLabel="Test Agent"
      onSend={onSend}
      onStop={vi.fn()}
    />,
  );
  return { onSend, textarea: screen.getByPlaceholderText('Plan, build, @ nodes, attach refs…') };
}

describe('AgentComposer', () => {
  afterEach(cleanup);

  it('does not send when Enter confirms an IME composition', () => {
    const { onSend, textarea } = renderComposer();
    fireEvent.change(textarea, { target: { value: '中文' } });

    fireEvent.keyDown(textarea, { key: 'Enter', isComposing: true });

    expect(onSend).not.toHaveBeenCalled();
    expect(textarea).toHaveValue('中文');
  });

  it('still sends with Enter outside an IME composition', () => {
    const { onSend, textarea } = renderComposer();
    fireEvent.change(textarea, { target: { value: 'send this' } });

    fireEvent.keyDown(textarea, { key: 'Enter' });

    expect(onSend).toHaveBeenCalledWith('send this', []);
  });

  it('offers supported Kimi reasoning effort levels', () => {
    const onReasoningEffortChange = vi.fn();
    render(
      <AgentComposer
        sending={false}
        agentLabel="Kimi · K3"
        providers={[{
          id: 'kimi-coding', name: 'Kimi for Coding', authMethods: [], baseUrl: '',
          connection: { status: 'connected', authType: 'api', accountLabel: '', error: null },
          models: [{ id: 'k3', name: 'Kimi K3', capabilities: {
            toolCall: true, textInput: true, imageInput: true, reasoning: true,
            reasoningEfforts: ['low', 'high', 'max'], defaultReasoningEffort: 'high',
            contextTokens: 0, outputTokens: 0,
          } }],
        }]}
        providerId="kimi-coding"
        modelId="k3"
        reasoningEffort="high"
        onReasoningEffortChange={onReasoningEffortChange}
        onSend={vi.fn()}
        onStop={vi.fn()}
      />,
    );

    expect(screen.getByTitle('Thinking settings')).toHaveTextContent('Thinking · High');
    fireEvent.click(screen.getByTitle('Thinking settings'));
    expect(screen.getByRole('switch', { name: 'Thinking enabled' })).toHaveAttribute('aria-checked', 'true');
    fireEvent.click(screen.getByRole('button', { name: 'Set thinking effort to max' }));
    expect(onReasoningEffortChange).toHaveBeenCalledWith('max');
  });

  it('keeps thinking visible beside K2.7 even when effort is fixed', () => {
    render(
      <AgentComposer
        sending={false}
        agentLabel="Kimi · K2.7 Coding"
        providers={[{
          id: 'kimi-coding', name: 'Kimi for Coding', authMethods: [], baseUrl: '',
          connection: { status: 'connected', authType: 'api', accountLabel: '', error: null },
          models: [{ id: 'kimi-for-coding', name: 'K2.7 Coding', capabilities: {
            toolCall: true, textInput: true, imageInput: true, reasoning: true,
            contextTokens: 0, outputTokens: 0,
          } }],
        }]}
        providerId="kimi-coding"
        modelId="kimi-for-coding"
        onSend={vi.fn()}
        onStop={vi.fn()}
      />,
    );

    expect(screen.getByTitle('Thinking settings')).toHaveTextContent('Thinking · On');
    fireEvent.click(screen.getByTitle('Thinking settings'));
    expect(screen.getByText('K2.7 Coding always uses its built-in thinking mode.')).toBeInTheDocument();
  });
});
