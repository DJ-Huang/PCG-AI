import { useEffect, useRef, useState } from 'react';
import { Icon } from './EditorChrome';
import { assetFileRequest, type AssetFile, type AssetFolder } from './assetFilesClient';

const STORAGE_KEY = 'picg-asset-folders-v1';
function loadFolders(): { folders: string[]; selected: string } {
  try {
    const stored = JSON.parse(localStorage.getItem(STORAGE_KEY) ?? 'null');
    const folders = ['examples', ...((Array.isArray(stored?.folders) ? stored.folders : []) as unknown[]).filter((value): value is string => typeof value === 'string' && !!value.trim())];
    const unique = [...new Set(folders)];
    return { folders: unique, selected: unique.includes(stored?.selected) ? stored.selected : 'examples' };
  } catch { return { folders: ['examples'], selected: 'examples' }; }
}
export default function AssetLibraryPanel({ onOpen, onClose }: { onOpen: (file: { text: string; filename: string }) => void; onClose: () => void }) {
  const [config, setConfig] = useState(loadFolders);
  const [query, setQuery] = useState('');
  const [data, setData] = useState<AssetFolder | null>(null);
  const [error, setError] = useState('');
  const [loading, setLoading] = useState(true);
  const [revision, setRevision] = useState(0);
  const [configure, setConfigure] = useState(false);
  const [draft, setDraft] = useState('');
  const [adding, setAdding] = useState(false);
  const [opening, setOpening] = useState<string | null>(null);
  const readAbort = useRef<AbortController | null>(null);
  const folder = config.selected;
  useEffect(() => {
    try { localStorage.setItem(STORAGE_KEY, JSON.stringify(config)); }
    catch { setError('Folder preferences could not be saved in this browser.'); }
  }, [config]);
  useEffect(() => {
    const abort = new AbortController();
    setLoading(true); setData(null); setError('');
    void assetFileRequest<AssetFolder>({ action: 'list', folder }, abort.signal)
      .then((result) => { if (!abort.signal.aborted) setData(result); })
      .catch((failure) => { if (!abort.signal.aborted) setError(String(failure.message ?? failure)); })
      .finally(() => { if (!abort.signal.aborted) setLoading(false); });
    return () => { abort.abort(); readAbort.current?.abort(); };
  }, [folder, revision]);
  async function addFolder() {
    if (!draft.trim()) return;
    setAdding(true); setError('');
    try {
      const result = await assetFileRequest<AssetFolder>({ action: 'list', folder: draft.trim() });
      const selected = result.root === data?.root ? folder : result.root;
      setConfig((current) => ({ folders: [...new Set([...current.folders, selected])], selected }));
      setDraft(''); setConfigure(false);
    } catch (failure) { setError(failure instanceof Error ? failure.message : String(failure)); }
    finally { setAdding(false); }
  }
  async function openFile(file: AssetFile) {
    const abort = new AbortController();
    readAbort.current?.abort(); readAbort.current = abort;
    setOpening(file.path); setError('');
    try {
      const result = await assetFileRequest<{ text: string; filename: string }>({ action: 'read', folder, path: file.path }, abort.signal);
      if (!abort.signal.aborted) onOpen(result);
    } catch (failure) { if (!abort.signal.aborted) setError(failure instanceof Error ? failure.message : String(failure)); }
    finally { if (readAbort.current === abort) setOpening(null); }
  }
  const filtered = (data?.files ?? []).filter((file) => file.path.toLowerCase().includes(query.toLowerCase()));
  return <aside className="pcg-assets" aria-label="Assets">
    <div className="pcg-panel-heading"><Icon name="folder" /><strong>Assets</strong><div className="pcg-panel-heading__actions">
      <button type="button" className="pcg-icon-button" aria-label="Configure asset folders" title="Configure folders" aria-expanded={configure} onClick={() => setConfigure(!configure)}><Icon name="settings" /></button>
      <button type="button" className="pcg-icon-button" aria-label="Close Assets" onClick={onClose}><Icon name="close" /></button>
    </div></div>
    <div className="pcg-assets__folder-bar"><select aria-label="Asset folder" value={folder} disabled={opening !== null} onChange={(event) => setConfig({ ...config, selected: event.target.value })}>
      {config.folders.map((item) => <option key={item} value={item}>{item === 'examples' ? 'Examples (default)' : item}</option>)}
    </select><button type="button" className="pcg-icon-button" aria-label="Refresh assets" title="Refresh files" disabled={loading || opening !== null} onClick={() => setRevision(revision + 1)}><Icon name="redo" /></button></div>
    {configure && <form className="pcg-assets__config" onSubmit={(event) => { event.preventDefault(); void addFolder(); }}>
      <label htmlFor="asset-folder-path">Add a folder</label>
      <input id="asset-folder-path" value={draft} onChange={(event) => setDraft(event.target.value)} placeholder="/path/to/folder or project-relative path" disabled={adding} />
      <small>Includes .pcg files in subfolders. Saved in this browser.</small>
      <div><button type="submit" disabled={adding || !draft.trim()}>{adding ? 'Checking…' : 'Add folder'}</button>
      {folder !== 'examples' && <button type="button" disabled={adding} onClick={() => setConfig({ folders: config.folders.filter((item) => item !== folder), selected: 'examples' })}>Remove folder</button>}</div>
    </form>}
    {data && <div className="pcg-assets__root" title={data.root}>{data.root}</div>}
    <label className="pcg-assets__search"><Icon name="search" /><input aria-label="Search assets" placeholder="Search .pcg files…" value={query} onChange={(event) => setQuery(event.target.value)} /></label>
    {error && <div className="pcg-assets__error" role="alert">{error}</div>}
    <div className="pcg-assets__list" aria-busy={loading}>
      {loading && <p>Scanning folder…</p>}
      {!loading && !error && !filtered.length && <p>{query ? 'No matching .pcg files.' : 'No .pcg files in this folder or its subfolders.'}</p>}
      {filtered.map((file) => <button type="button" key={file.path} className="pcg-assets__item" title={`Open ${file.path}`} disabled={opening !== null} onClick={() => void openFile(file)}>
        <Icon name="file" /><span><strong>{file.name.replace(/\.pcg$/i, '')}</strong><small>{file.path}</small></span><span className="pcg-assets__open">{opening === file.path ? '…' : 'Open'}</span>
      </button>)}
    </div>
    {(data?.truncated || !!data?.warnings.length) && <div className="pcg-assets__error">{data.truncated && 'Folder is too large; some files are omitted. Choose a smaller folder. '}{data.warnings.join('; ')}</div>}
    <div className="pcg-assets__footer">{filtered.length} / {data?.files.length ?? 0} files · Click to open graph</div>
  </aside>;
}
