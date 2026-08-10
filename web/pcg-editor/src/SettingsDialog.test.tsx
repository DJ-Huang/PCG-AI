import { fireEvent, render, screen, waitFor } from '@testing-library/react';
import { beforeEach, describe, expect, it, vi } from 'vitest';

import SettingsDialog from './SettingsDialog';
import * as client from './agent/agentClient';

vi.mock('./agent/agentClient', async (loadOriginal) => {
  const original = await loadOriginal<typeof import('./agent/agentClient')>();
  return {
    ...original,
    getProviders: vi.fn(),
    connectApiKey: vi.fn(),
    disconnectProvider: vi.fn(),
    validateProvider: vi.fn(),
    startOAuth: vi.fn(),
    getOAuthStatus: vi.fn(),
  };
});

const kimiProvider: client.ProviderDescriptor = {
  id: 'kimi-coding',
  name: 'Kimi for Coding',
  baseUrl: '',
  authMethods: [{ type: 'api', label: 'API Key', available: true }],
  connection: { status: 'unavailable', authType: '', accountLabel: '', error: null },
  models: [],
};

describe('SettingsDialog', () => {
  beforeEach(() => {
    vi.mocked(client.getProviders).mockResolvedValue([kimiProvider]);
    vi.mocked(client.connectApiKey).mockResolvedValue();
  });

  it('connects the fixed Kimi Coding Provider without asking for a Base URL', async () => {
    const changed = vi.fn();
    render(<SettingsDialog open onClose={() => undefined} onProvidersChanged={changed} />);

    expect((await screen.findAllByText('Kimi for Coding')).length).toBeGreaterThan(0);
    expect(screen.queryByText('Base URL')).not.toBeInTheDocument();
    fireEvent.change(screen.getByLabelText('Kimi Coding API Key'), { target: { value: 'kimi-secret' } });
    fireEvent.click(screen.getByText('Validate & Connect'));

    await waitFor(() => expect(client.connectApiKey).toHaveBeenCalledWith('kimi-coding', 'kimi-secret', undefined));
    await waitFor(() => expect(changed).toHaveBeenCalled());
  });
});
