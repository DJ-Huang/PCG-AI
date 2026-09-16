import { act, cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

import SettingsDialog from './SettingsDialog';
import * as thirdParty from './thirdPartyClient';

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
const tripoConfigured: thirdParty.TripoStatus = {
  ...tripoUnconfigured,
  configured: true,
  source: 'store',
  keyHint: '…cret',
};
const onClose = () => undefined;

describe('SettingsDialog', () => {
  afterEach(() => cleanup());

  beforeEach(() => {
    vi.resetAllMocks();
    vi.mocked(thirdParty.getTripoStatus).mockResolvedValue(tripoUnconfigured);
    vi.mocked(thirdParty.saveTripoKey).mockResolvedValue(tripoConfigured);
    vi.mocked(thirdParty.clearTripoKey).mockResolvedValue(tripoUnconfigured);
  });

  it('opens directly to 3D generation without Agent or LLM settings', async () => {
    render(<SettingsDialog open onClose={onClose} />);
    expect(await screen.findByText('Not configured')).toBeInTheDocument();
    expect(screen.getByRole('button', { name: '3D Generation' })).toHaveAttribute('aria-current', 'page');
    expect(screen.getByText('Tripo (Image to 3D)')).toBeInTheDocument();
    expect(screen.queryByText('General')).not.toBeInTheDocument();
    expect(screen.queryByText('AI Providers')).not.toBeInTheDocument();
    expect(screen.queryByText(/Agent bridge token|Agent timeline|Kimi|OAuth|reasoning/i)).not.toBeInTheDocument();
  });

  it('saves a trimmed Tripo API key and clears the password field', async () => {
    render(<SettingsDialog open onClose={onClose} />);
    await screen.findByText('Not configured');
    fireEvent.change(screen.getByLabelText('Tripo API Key'), { target: { value: '  tsk_secret  ' } });
    fireEvent.click(screen.getByRole('button', { name: 'Save key' }));
    await waitFor(() => expect(thirdParty.saveTripoKey).toHaveBeenCalledWith('tsk_secret'));
    expect(await screen.findByText('Configured')).toBeInTheDocument();
    expect(screen.getByLabelText('Tripo API Key')).toHaveValue('');
  });

  it('disables empty saves and clears a stored key', async () => {
    vi.mocked(thirdParty.getTripoStatus).mockResolvedValue(tripoConfigured);
    render(<SettingsDialog open onClose={onClose} />);
    await screen.findByText('Configured');
    expect(screen.getByRole('button', { name: 'Save key' })).toBeDisabled();
    fireEvent.click(screen.getByRole('button', { name: 'Clear key' }));
    expect(await screen.findByText('Not configured')).toBeInTheDocument();
    expect(thirdParty.clearTripoKey).toHaveBeenCalledOnce();
    expect(screen.queryByRole('button', { name: 'Clear key' })).not.toBeInTheDocument();
  });

  it('locks key editing when the key comes from the environment', async () => {
    vi.mocked(thirdParty.getTripoStatus).mockResolvedValue({ ...tripoConfigured, source: 'env' });
    render(<SettingsDialog open onClose={onClose} />);
    expect(await screen.findByText('Configured (env)')).toBeInTheDocument();
    expect(screen.queryByLabelText('Tripo API Key')).not.toBeInTheDocument();
    expect(screen.queryByRole('button', { name: 'Save key' })).not.toBeInTheDocument();
    expect(screen.queryByRole('button', { name: 'Clear key' })).not.toBeInTheDocument();
  });

  it('reports a status request failure instead of silently hiding it', async () => {
    vi.mocked(thirdParty.getTripoStatus).mockRejectedValue(new Error('Local server unavailable'));
    render(<SettingsDialog open onClose={onClose} />);
    expect(await screen.findByRole('status')).toHaveTextContent('Local server unavailable');
    expect(screen.getByText('Unavailable')).toBeInTheDocument();
  });

  it('reports save errors and leaves the key editable for retry', async () => {
    vi.mocked(thirdParty.saveTripoKey).mockRejectedValue(new Error('Could not save credential'));
    render(<SettingsDialog open onClose={onClose} />);
    await screen.findByText('Not configured');
    fireEvent.change(screen.getByLabelText('Tripo API Key'), { target: { value: 'tsk_secret' } });
    fireEvent.click(screen.getByRole('button', { name: 'Save key' }));
    expect(await screen.findByRole('status')).toHaveTextContent('Could not save credential');
    expect(screen.getByRole('button', { name: 'Save key' })).not.toBeDisabled();
  });

  it('clears unsaved secrets when closed and reopened', async () => {
    const view = render(<SettingsDialog open onClose={onClose} />);
    await screen.findByText('Not configured');
    fireEvent.change(screen.getByLabelText('Tripo API Key'), { target: { value: 'unsaved-secret' } });
    view.rerender(<SettingsDialog open={false} onClose={onClose} />);
    expect(screen.queryByRole('dialog')).not.toBeInTheDocument();
    view.rerender(<SettingsDialog open onClose={onClose} />);
    await screen.findByText('Not configured');
    expect(screen.getByLabelText('Tripo API Key')).toHaveValue('');
  });

  it('ignores an obsolete status response after closing and reopening', async () => {
    let resolveOld!: (value: thirdParty.TripoStatus) => void;
    vi.mocked(thirdParty.getTripoStatus).mockImplementationOnce(() => new Promise((resolve) => { resolveOld = resolve; }));
    const view = render(<SettingsDialog open onClose={onClose} />);
    view.rerender(<SettingsDialog open={false} onClose={onClose} />);
    view.rerender(<SettingsDialog open onClose={onClose} />);
    await screen.findByText('Not configured');
    await act(async () => { resolveOld(tripoConfigured); });
    expect(screen.getByText('Not configured')).toBeInTheDocument();
    expect(screen.queryByText('Configured')).not.toBeInTheDocument();
  });

  it('closes with Escape', async () => {
    const close = vi.fn();
    render(<SettingsDialog open onClose={close} />);
    await screen.findByText('Not configured');
    fireEvent.keyDown(window, { key: 'Escape' });
    expect(close).toHaveBeenCalledOnce();
  });
});
