import { useEffect, useRef, useState } from 'react';

import {
  clearTripoKey,
  getTripoStatus,
  saveTripoKey,
  type TripoStatus,
} from './thirdPartyClient';

interface SettingsDialogProps {
  open: boolean;
  onClose: () => void;
}

function errorMessage(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}

export default function SettingsDialog({ open, onClose }: SettingsDialogProps) {
  const [tripoStatus, setTripoStatus] = useState<TripoStatus | null>(null);
  const [tripoKey, setTripoKey] = useState('');
  const [status, setStatus] = useState('');
  const [busy, setBusy] = useState(false);
  const generation = useRef(0);
  const dialogRef = useRef<HTMLElement>(null);

  useEffect(() => {
    const current = ++generation.current;
    setTripoKey('');
    setStatus('');
    setTripoStatus(null);
    setBusy(open);
    if (!open) return;

    void getTripoStatus()
      .then((next) => {
        if (generation.current === current) setTripoStatus(next);
      })
      .catch((error) => {
        if (generation.current === current) setStatus(errorMessage(error));
      })
      .finally(() => {
        if (generation.current === current) setBusy(false);
      });

    const previousFocus = document.activeElement;
    dialogRef.current?.focus();
    const onKeyDown = (event: KeyboardEvent) => {
      if (event.key === 'Escape') {
        event.preventDefault();
        event.stopPropagation();
        onClose();
      } else if (event.key === 'Tab') {
        const controls = dialogRef.current?.querySelectorAll<HTMLElement>(
          'button:not(:disabled), input:not(:disabled), [tabindex="0"]',
        );
        if (!controls?.length) return;
        const first = controls[0];
        const last = controls[controls.length - 1];
        if (event.shiftKey && (document.activeElement === first || document.activeElement === dialogRef.current)) {
          event.preventDefault();
          last.focus();
        } else if (!event.shiftKey && document.activeElement === last) {
          event.preventDefault();
          first.focus();
        }
      }
    };
    window.addEventListener('keydown', onKeyDown);
    return () => {
      generation.current += 1;
      window.removeEventListener('keydown', onKeyDown);
      if (previousFocus instanceof HTMLElement) previousFocus.focus();
    };
  }, [onClose, open]);

  const saveTripo = async () => {
    if (busy || !tripoKey.trim()) return;
    const current = generation.current;
    setBusy(true);
    setStatus('');
    try {
      const next = await saveTripoKey(tripoKey.trim());
      if (generation.current !== current) return;
      setTripoStatus(next);
      setTripoKey('');
      setStatus('Tripo API key saved by the local server.');
    } catch (error) {
      if (generation.current === current) setStatus(errorMessage(error));
    } finally {
      if (generation.current === current) setBusy(false);
    }
  };

  const clearTripo = async () => {
    if (busy) return;
    const current = generation.current;
    setBusy(true);
    setStatus('');
    try {
      const next = await clearTripoKey();
      if (generation.current !== current) return;
      setTripoStatus(next);
      setTripoKey('');
      setStatus(next.source === 'env'
        ? 'Stored key cleared — the PCG_TRIPO_API_KEY environment variable is still active.'
        : 'Tripo API key cleared.');
    } catch (error) {
      if (generation.current === current) setStatus(errorMessage(error));
    } finally {
      if (generation.current === current) setBusy(false);
    }
  };

  if (!open) return null;

  return (
    <div className="pcg-settings-backdrop" role="presentation" onMouseDown={(event) => {
      if (event.target === event.currentTarget) onClose();
    }}>
      <section ref={dialogRef} tabIndex={-1} className="pcg-settings" role="dialog" aria-modal="true" aria-labelledby="pcg-settings-title">
        <header className="pcg-settings__header">
          <div>
            <h2 id="pcg-settings-title">PCG Settings</h2>
            <p>API connections for 3D model generation.</p>
          </div>
          <button type="button" className="pcg-settings__close" onClick={onClose} aria-label="Close settings">×</button>
        </header>
        <div className="pcg-settings__body">
          <nav className="pcg-settings__nav" aria-label="Settings sections">
            <button type="button" className="is-active" aria-current="page">3D Generation</button>
          </nav>
          <main className="pcg-settings__content" aria-busy={busy}>
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
                    : (busy ? 'Loading…' : 'Unavailable')}
                </span>
              </div>
              <p className="pcg-settings__hint">
                The key is stored by the local pcg-server credential store ({tripoStatus?.credentialStore ?? 'protected-file'}), never in browser storage or in .pcg files.
                {tripoStatus?.configured && tripoStatus.keyHint ? ` Active key: ${tripoStatus.keyHint}` : ''}
              </p>
              {tripoStatus?.source === 'env' ? (
                <p className="pcg-settings__hint">
                  Provided by the PCG_TRIPO_API_KEY environment variable. Restart pcg-server without it to use a stored key instead.
                </p>
              ) : (
                <form className="pcg-settings__credential" onSubmit={(event) => {
                  event.preventDefault();
                  void saveTripo();
                }}>
                  <label className="pcg-settings__field">
                    <span>Tripo API Key</span>
                    <input
                      type="password"
                      value={tripoKey}
                      autoComplete="off"
                      spellCheck={false}
                      disabled={busy}
                      placeholder="Stored securely by the local server"
                      onChange={(event) => setTripoKey(event.target.value)}
                    />
                  </label>
                  <div className="pcg-settings__connected-actions">
                    <button type="submit" disabled={busy || !tripoKey.trim()}>Save key</button>
                    {tripoStatus?.configured && (
                      <button type="button" className="is-danger" disabled={busy} onClick={() => void clearTripo()}>Clear key</button>
                    )}
                  </div>
                </form>
              )}
            </div>
            {status && <div className="pcg-settings__status" role="status">{status}</div>}
          </main>
        </div>
      </section>
    </div>
  );
}
