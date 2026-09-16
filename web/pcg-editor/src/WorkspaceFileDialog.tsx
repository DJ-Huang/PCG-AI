import { useState } from 'react';

interface Props {
  mode: 'open' | 'save';
  initialPath: string;
  onSubmit: (path: string) => Promise<void>;
  onClose: () => void;
}

export default function WorkspaceFileDialog({ mode, initialPath, onSubmit, onClose }: Props) {
  const [path, setPath] = useState(initialPath);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState('');
  const title = mode === 'save' ? 'Save PICG project' : 'Open PICG project';
  return <div className="pcg-settings-backdrop" onKeyDown={(event) => {
    event.stopPropagation();
    if (event.key === 'Escape' && !busy) onClose();
  }}>
    <form className="picg-file-dialog" role="dialog" aria-modal="true" aria-label={title} onSubmit={async (event) => {
      event.preventDefault(); setError(''); setBusy(true);
      try { await onSubmit(path.trim()); onClose(); }
      catch (reason) { setError(reason instanceof Error ? reason.message : String(reason)); }
      finally { setBusy(false); }
    }}>
      <h2>{title}</h2>
      <p>{mode === 'save' ? 'Saves geometry and the complete camera setup together in this workspace.' : 'Opens the geometry and its camera setup, including motion curves and keyframes.'}</p>
      <label>Project path<input autoFocus required aria-label="Project path" value={path} onChange={(event) => setPath(event.target.value)} placeholder="examples/my-shot.picg" /></label>
      <small>Use a project-relative .picg path. Existing .pcg files are also supported.</small>
      {error && <p role="alert">{error}</p>}
      <div className="picg-file-dialog__actions">
        <button type="button" disabled={busy} onClick={onClose}>Cancel</button>
        <button disabled={busy || !path.trim()} type="submit">{busy ? 'Working…' : mode === 'save' ? 'Save project' : 'Open project'}</button>
      </div>
    </form>
  </div>;
}
