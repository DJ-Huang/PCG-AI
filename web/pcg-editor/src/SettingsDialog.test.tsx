import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

import SettingsDialog from './SettingsDialog';
import * as client from './agent/agentClient';
import * as thirdParty from './thirdPartyClient';

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

vi.mock('./thirdPartyClient', () => ({
  getTripoStatus: vi.fn(),
  saveTripoKey: vi.fn(),
  clearTripoKey: vi.fn(),
}));

const tripoUnconfigured: thirdParty.TripoStatus = {
  configured: false,
  source: 'none',
  baseUrl: 'https://openapi.tripo3d.ai/v3',
  keyHint: '',
  credentialStore: 'protected-file',
};

const kimiProvider: client.ProviderDescriptor = {
  id: 'kimi-coding',
  name: 'Kimi for Coding',
  baseUrl: '',
  authMethods: [{ type: 'api', label: 'API Key', available: true }],
  connection: { status: 'unavailable', authType: '', accountLabel: '', error: null },
  models: [],
};

describe('SettingsDialog', () => {
  afterEach(() => cleanup());

  beforeEach(() => {
    vi.mocked(client.getProviders).mockResolvedValue([kimiProvider]);
    vi.mocked(client.connectApiKey).mockResolvedValue();
    vi.mocked(thirdParty.getTripoStatus).mockResolvedValue(tripoUnconfigured);
    vi.mocked(thirdParty.saveTripoKey).mockResolvedValue({
      ...tripoUnconfigured,
      configured: true,
      source: 'store',
      keyHint: '…cret',
    });
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

  it('saves a Tripo API key from the 3D Generation section', async () => {
    render(<SettingsDialog open onClose={() => undefined} onProvidersChanged={() => undefined} />);

    fireEvent.click(screen.getByText('3D Generation'));
    expect(await screen.findByText('Tripo (Image to 3D)')).toBeInTheDocument();
    expect(screen.getByText('Not configured')).toBeInTheDocument();

    fireEvent.change(screen.getByLabelText('Tripo API Key'), { target: { value: 'tsk_secret' } });
    fireEvent.click(screen.getByText('Save key'));

    await waitFor(() => expect(thirdParty.saveTripoKey).toHaveBeenCalledWith('tsk_secret'));
    expect(await screen.findByText('Configured')).toBeInTheDocument();
  });

  it('locks Tripo key editing when the key comes from the environment', async () => {
    vi.mocked(thirdParty.getTripoStatus).mockResolvedValue({
      ...tripoUnconfigured,
      configured: true,
      source: 'env',
      keyHint: '…9999',
    });
    render(<SettingsDialog open onClose={() => undefined} onProvidersChanged={() => undefined} />);

    fireEvent.click(screen.getByText('3D Generation'));
    expect(await screen.findByText('Configured (env)')).toBeInTheDocument();
    expect(screen.queryByLabelText('Tripo API Key')).not.toBeInTheDocument();
    expect(screen.queryByText('Save key')).not.toBeInTheDocument();
  });
});
