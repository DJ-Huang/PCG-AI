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
});
