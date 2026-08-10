import { fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';

import AgentHistory from './AgentHistory';
import type { AgentSessionDescriptor } from './agentClient';

describe('AgentHistory', () => {
  it('groups sessions and exposes open, search, rename, and delete actions', () => {
    const session: AgentSessionDescriptor = {
      id: 'session-history-1', title: 'Build a bridge', createdAt: Date.now(), updatedAt: Date.now(),
      providerId: 'kimi-coding', modelId: 'kimi-for-coding', graphName: 'bridge.pcg', status: 'completed',
    };
    const onQueryChange = vi.fn();
    const onOpen = vi.fn();
    const onRename = vi.fn();
    const onDelete = vi.fn();
    render(<AgentHistory
      sessions={[session]} activeSessionId="" query="" loading={false}
      onQueryChange={onQueryChange} onOpen={onOpen} onRename={onRename} onDelete={onDelete} onClose={vi.fn()}
    />);

    expect(screen.getByText('Today')).toBeInTheDocument();
    fireEvent.change(screen.getByLabelText('Search chat history'), { target: { value: 'bridge' } });
    fireEvent.click(screen.getByText('Build a bridge'));
    fireEvent.click(screen.getByTitle('Rename chat'));
    fireEvent.click(screen.getByTitle('Delete chat'));
    expect(onQueryChange).toHaveBeenCalledWith('bridge');
    expect(onOpen).toHaveBeenCalledWith(session.id);
    expect(onRename).toHaveBeenCalledWith(session);
    expect(onDelete).toHaveBeenCalledWith(session);
  });
});
