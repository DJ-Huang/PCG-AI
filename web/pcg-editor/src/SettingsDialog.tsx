import { useCallback, useEffect, useMemo, useRef, useState } from 'react';

import {
  connectApiKey,
  disconnectProvider,
  getAgentToken,
  getOAuthStatus,
  getProviders,
  setAgentToken,
  startOAuth,
  validateProvider,
  type ProviderDescriptor,
} from './agent/agentClient';
import {
  clearTripoKey,
  getTripoStatus,
  saveTripoKey,
  type TripoStatus,
} from './thirdPartyClient';

interface SettingsDialogProps {
  open: boolean;
  onClose: () => void;
  onProvidersChanged: () => void;
}

type SettingsSection = 'general' | 'providers' | 'generation';
const SHOW_REASONING_KEY = 'pcg-agent-show-reasoning';

function errorMessage(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}

export default function SettingsDialog({ open, onClose, onProvidersChanged }: SettingsDialogProps) {
  const [section, setSection] = useState<SettingsSection>('providers');
  const [providers, setProviders] = useState<ProviderDescriptor[]>([]);
  const [providerId, setProviderId] = useState('kimi-coding');
  const [apiKey, setApiKey] = useState('');
  const [baseUrl, setBaseUrl] = useState('');
  const [bridgeToken, setBridgeToken] = useState('');
  const [status, setStatus] = useState('');
  const [busy, setBusy] = useState(false);
  const [showReasoning, setShowReasoning] = useState(() => localStorage.getItem(SHOW_REASONING_KEY) !== 'false');
  const [tripoStatus, setTripoStatus] = useState<TripoStatus | null>(null);
  const [tripoKey, setTripoKey] = useState('');
  const oauthGeneration = useRef(0);

  const selectedProvider = useMemo(
    () => providers.find((provider) => provider.id === providerId),
    [providerId, providers],
  );

  const refreshProviders = useCallback(async () => {
    const next = await getProviders();
    setProviders(next);
    setProviderId((current) => next.some((provider) => provider.id === current)
      ? current
      : (next.find((provider) => provider.id === 'kimi-coding')?.id ?? next[0]?.id ?? ''));
  }, []);

  useEffect(() => {
    if (!open) {
      oauthGeneration.current += 1;
      setBusy(false);
      return;
    }
    setBusy(false);
    setBridgeToken(getAgentToken());
    setStatus('');
    void refreshProviders().catch((error) => setStatus(errorMessage(error)));
    void getTripoStatus()
      .then(setTripoStatus)
      .catch(() => setTripoStatus(null));
    const onKeyDown = (event: KeyboardEvent) => {
      if (event.key === 'Escape') onClose();
    };
    window.addEventListener('keydown', onKeyDown);
    return () => window.removeEventListener('keydown', onKeyDown);
  }, [onClose, open, refreshProviders]);

  useEffect(() => {
    setApiKey('');
    setStatus('');
  }, [providerId]);

  useEffect(() => {
    setBaseUrl(selectedProvider?.baseUrl ?? '');
  }, [selectedProvider?.baseUrl]);

  const markProvidersChanged = useCallback(async () => {
    await refreshProviders();
    onProvidersChanged();
  }, [onProvidersChanged, refreshProviders]);

  const connectKey = useCallback(async () => {
    if (!apiKey.trim() || !selectedProvider) return;
    setBusy(true);
    setStatus('Validating credentials…');
    try {
      await connectApiKey(
        selectedProvider.id,
        apiKey.trim(),
        selectedProvider.id === 'openai-compatible' ? baseUrl.trim() : undefined,
      );
      setApiKey('');
      setStatus('Connected and validated.');
      await markProvidersChanged();
    } catch (error) {
      setStatus(errorMessage(error));
    } finally {
      setBusy(false);
    }
  }, [apiKey, baseUrl, markProvidersChanged, selectedProvider]);

  const connectOAuth = useCallback(async () => {
    if (!selectedProvider) return;
    const generation = ++oauthGeneration.current;
    setBusy(true);
    setStatus('Starting authorization…');
    try {
      const attempt = await startOAuth(selectedProvider.id);
      window.open(attempt.url, '_blank', 'noopener,noreferrer');
      setStatus(attempt.userCode
        ? `Open the authorization page and enter: ${attempt.userCode}`
        : 'Complete authorization in the browser…');
      while (oauthGeneration.current === generation && Date.now() < attempt.expiresAt) {
        await new Promise((resolve) => window.setTimeout(resolve, Math.max(1000, attempt.intervalSeconds * 1000)));
        if (oauthGeneration.current !== generation) return;
        const result = await getOAuthStatus(attempt.attemptId);
        if (result.status === 'pending') continue;
        if (result.status !== 'connected') throw new Error(result.error || `Authorization ${result.status}`);
        setStatus('Connected and validated.');
        await markProvidersChanged();
        return;
      }
      if (oauthGeneration.current === generation) throw new Error('Authorization expired');
    } catch (error) {
      setStatus(errorMessage(error));
    } finally {
      if (oauthGeneration.current === generation) setBusy(false);
    }
  }, [markProvidersChanged, selectedProvider]);

  const revalidate = useCallback(async () => {
    if (!selectedProvider) return;
    setBusy(true);
    setStatus('Revalidating…');
    try {
      await validateProvider(selectedProvider.id);
      setStatus('Connection is valid.');
      await markProvidersChanged();
    } catch (error) {
      setStatus(errorMessage(error));
    } finally {
      setBusy(false);
    }
  }, [markProvidersChanged, selectedProvider]);

  const disconnect = useCallback(async () => {
    if (!selectedProvider) return;
    setBusy(true);
    try {
      await disconnectProvider(selectedProvider.id);
      setStatus('Disconnected.');
      await markProvidersChanged();
    } catch (error) {
      setStatus(errorMessage(error));
    } finally {
      setBusy(false);
    }
  }, [markProvidersChanged, selectedProvider]);

  const saveTripo = useCallback(async () => {
    if (!tripoKey.trim()) return;
    setBusy(true);
    try {
      const next = await saveTripoKey(tripoKey.trim());
      setTripoStatus(next);
      setTripoKey('');
      setStatus('Tripo API key saved by the local server.');
    } catch (error) {
      setStatus(errorMessage(error));
    } finally {
      setBusy(false);
    }
  }, [tripoKey]);

  const clearTripo = useCallback(async () => {
    setBusy(true);
    try {
      const next = await clearTripoKey();
      setTripoStatus(next);
      setStatus(next.source === 'env'
        ? 'Stored key cleared — the PCG_TRIPO_API_KEY environment variable is still active.'
        : 'Tripo API key cleared.');
    } catch (error) {
      setStatus(errorMessage(error));
    } finally {
      setBusy(false);
    }
  }, []);

  if (!open) return null;

  return (
    <div className="pcg-settings-backdrop" role="presentation" onMouseDown={(event) => {
      if (event.target === event.currentTarget) onClose();
    }}>
      <section className="pcg-settings" role="dialog" aria-modal="true" aria-labelledby="pcg-settings-title">
        <header className="pcg-settings__header">
          <div>
            <h2 id="pcg-settings-title">PCG Settings</h2>
            <p>Editor, local runtime, and AI Provider connections.</p>
          </div>
          <button type="button" className="pcg-settings__close" onClick={onClose} aria-label="Close settings">×</button>
        </header>
        <div className="pcg-settings__body">
          <nav className="pcg-settings__nav" aria-label="Settings sections">
            <button type="button" className={section === 'general' ? 'is-active' : ''} onClick={() => setSection('general')}>General</button>
            <button type="button" className={section === 'providers' ? 'is-active' : ''} onClick={() => setSection('providers')}>AI Providers</button>
            <button type="button" className={section === 'generation' ? 'is-active' : ''} onClick={() => setSection('generation')}>3D Generation</button>
          </nav>

          <main className="pcg-settings__content">
            {section === 'general' ? (
              <div className="pcg-settings__section">
                <h3>Local runtime</h3>
                <p className="pcg-settings__hint">The Web editor uses the embedded localhost pcg-server for cooking, Agent turns, and credentials.</p>
                <label className="pcg-settings__field">
                  <span>Agent bridge token</span>
                  <input
                    type="password"
                    value={bridgeToken}
                    autoComplete="off"
                    placeholder="Optional PCG_AGENT_TOKEN"
                    onChange={(event) => setBridgeToken(event.target.value)}
                  />
                </label>
                <button type="button" onClick={() => {
                  setAgentToken(bridgeToken.trim());
                  setStatus('Local bridge token saved in this browser.');
                }}>Save local token</button>
                <div className="pcg-settings__field">
                  <span>Agent timeline</span>
                  <label>
                    <input
                      type="checkbox"
                      checked={showReasoning}
                      onChange={(event) => {
                        const next = event.target.checked;
                        setShowReasoning(next);
                        localStorage.setItem(SHOW_REASONING_KEY, String(next));
                        window.dispatchEvent(new CustomEvent('pcg-agent-settings-changed'));
                      }}
                    /> Show Provider reasoning when available
                  </label>
                </div>
              </div>
            ) : section === 'generation' ? (
              <div className="pcg-settings__section">
                <div className="pcg-settings__provider-title">
                  <div>
                    <h3>Tripo (Image to 3D)</h3>
                    <p>Cloud image-to-3D for Tripo3DGenerator nodes. Cook and preview never call the API — only the node Generate action does.</p>
                  </div>
                  <span className={`pcg-settings__connection is-${tripoStatus?.configured ? 'connected' : 'unavailable'}`}>
                    {tripoStatus
                      ? (tripoStatus.configured
                        ? (tripoStatus.source === 'env' ? 'Configured (env)' : 'Configured')
                        : 'Not configured')
                      : 'Unavailable'}
                  </span>
                </div>
                <p className="pcg-settings__hint">
                  The key is stored by the local pcg-server credential store ({tripoStatus?.credentialStore ?? 'protected-file'}), never in this browser or in .pcg files.
                  {tripoStatus?.configured && tripoStatus.keyHint ? ` Active key: ${tripoStatus.keyHint}` : ''}
                </p>
                {tripoStatus?.source === 'env' ? (
                  <p className="pcg-settings__hint">
                    Provided by the PCG_TRIPO_API_KEY environment variable. Restart pcg-server without it to use a stored key instead.
                  </p>
                ) : (
                  <div className="pcg-settings__credential">
                    <label className="pcg-settings__field">
                      <span>Tripo API Key</span>
                      <input
                        type="password"
                        value={tripoKey}
                        autoComplete="off"
                        spellCheck={false}
                        placeholder="Stored securely by the local server"
                        onChange={(event) => setTripoKey(event.target.value)}
                      />
                    </label>
                    <div className="pcg-settings__connected-actions">
                      <button type="button" disabled={busy || !tripoKey.trim()} onClick={() => void saveTripo()}>Save key</button>
                      {tripoStatus?.configured && (
                        <button type="button" className="is-danger" disabled={busy} onClick={() => void clearTripo()}>Clear key</button>
                      )}
                    </div>
                  </div>
                )}
              </div>
            ) : (
              <div className="pcg-settings__providers">
                <div className="pcg-settings__provider-list" role="list">
                  {providers.map((provider) => (
                    <button
                      key={provider.id}
                      type="button"
                      className={provider.id === providerId ? 'is-active' : ''}
                      onClick={() => setProviderId(provider.id)}
                    >
                      <span>{provider.name}</span>
                      <small className={`is-${provider.connection.status}`}>
                        {provider.connection.status === 'connected' ? 'Connected' : provider.connection.status}
                      </small>
                    </button>
                  ))}
                </div>
                <div className="pcg-settings__provider-detail">
                  {selectedProvider && (
                    <>
                      <div className="pcg-settings__provider-title">
                        <div>
                          <h3>{selectedProvider.name}</h3>
                          <p>{selectedProvider.id === 'kimi-coding'
                            ? 'Kimi Coding API · Anthropic-compatible · fixed endpoint'
                            : selectedProvider.id === 'openai-compatible'
                              ? 'Custom OpenAI-compatible endpoint'
                              : 'Managed by the embedded PCG Agent runtime'}</p>
                        </div>
                        <span className={`pcg-settings__connection is-${selectedProvider.connection.status}`}>
                          {selectedProvider.connection.status}
                        </span>
                      </div>

                      {selectedProvider.connection.status === 'connected' ? (
                        <div className="pcg-settings__connected-actions">
                          <div>{selectedProvider.models.length} model(s) available</div>
                          <button type="button" disabled={busy} onClick={() => void revalidate()}>Revalidate</button>
                          <button type="button" className="is-danger" disabled={busy} onClick={() => void disconnect()}>Disconnect</button>
                        </div>
                      ) : (
                        <>
                          {selectedProvider.authMethods.some((method) => method.type === 'api') && (
                            <div className="pcg-settings__credential">
                              {selectedProvider.id === 'openai-compatible' && (
                                <label className="pcg-settings__field">
                                  <span>Base URL</span>
                                  <input type="url" value={baseUrl} placeholder="https://provider.example/v1" onChange={(event) => setBaseUrl(event.target.value)} />
                                </label>
                              )}
                              <label className="pcg-settings__field">
                                <span>{selectedProvider.id === 'kimi-coding' ? 'Kimi Coding API Key' : 'API Key'}</span>
                                <input
                                  type="password"
                                  value={apiKey}
                                  autoComplete="off"
                                  spellCheck={false}
                                  placeholder="Stored securely by the local server"
                                  onChange={(event) => setApiKey(event.target.value)}
                                />
                              </label>
                              <button type="button" disabled={busy || !apiKey.trim()} onClick={() => void connectKey()}>Validate & Connect</button>
                            </div>
                          )}
                          {selectedProvider.authMethods.filter((method) => method.type === 'oauth').map((method) => (
                            <button
                              key={method.label}
                              type="button"
                              className="pcg-settings__oauth"
                              disabled={busy || !method.available}
                              title={method.unavailableReason}
                              onClick={() => void connectOAuth()}
                            >
                              Connect {method.label}{method.available ? '' : ' · unavailable'}
                            </button>
                          ))}
                        </>
                      )}
                    </>
                  )}
                </div>
              </div>
            )}
            {status && <div className="pcg-settings__status" role="status">{status}</div>}
          </main>
        </div>
      </section>
    </div>
  );
}
