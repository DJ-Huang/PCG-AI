// editorSession.ts — Persist the last editor session across page refreshes.

import type { GraphJson } from './graphSchema';
import { normalizeShotDocument, type ShotDocument } from './shot';

const STORAGE_KEY = 'pcg-editor-session';

export interface EditorSessionData {
  graph: GraphJson;
  filename: string;
  nodeCounter: number;
  shot?: ShotDocument;
}

export function loadEditorSession(): EditorSessionData | null {
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    if (!raw) return null;
    const parsed = JSON.parse(raw) as Partial<EditorSessionData>;
    if (!parsed.graph || typeof parsed.graph !== 'object') return null;
    return {
      graph: parsed.graph as GraphJson,
      filename: typeof parsed.filename === 'string' ? parsed.filename : '',
      nodeCounter: typeof parsed.nodeCounter === 'number' ? parsed.nodeCounter : 100,
      ...(parsed.shot ? { shot: normalizeShotDocument(parsed.shot) } : {}),
    };
  } catch {
    return null;
  }
}

export function saveEditorSession(data: EditorSessionData): void {
  try {
    localStorage.setItem(STORAGE_KEY, JSON.stringify(data));
  } catch {
    // Quota exceeded or private browsing — ignore.
  }
}

export function clearEditorSession(): void {
  localStorage.removeItem(STORAGE_KEY);
}
