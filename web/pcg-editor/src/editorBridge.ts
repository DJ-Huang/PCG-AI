import { useCallback, useEffect, useRef } from 'react';

import { getAgentToken } from './agent/agentClient';
import type { GraphCommandResult, QueuedGraphCommand } from './graphCommands';
import type { GraphJson } from './graphSchema';
import type { NodeManifest } from './nodeManifest';
import type { PreviewCapture } from './PreviewViewport';

const BRIDGE_BASE = '/api/editor-bridge';
const HEARTBEAT_MS = 5_000;
const POLL_MS = 400;

interface EditorBridgeOptions {
  graph: GraphJson;
  nodeManifest: NodeManifest;
  graphPath: string;
  editPath: string[];
  selectedNodeId: string | null;
  previewTargetNodeId: string | null;
  applyCommands: (commands: QueuedGraphCommand[]) => Promise<GraphCommandResult[]>;
  capturePreview: () => PreviewCapture | null;
}

interface SessionResponse {
  ok: boolean;
  captureRequestId?: number;
}

interface PatchResponse {
  ok: boolean;
  patches?: QueuedGraphCommand[];
}

function bridgeHeaders(): Record<string, string> {
  const headers: Record<string, string> = { 'Content-Type': 'application/json' };
  const token = getAgentToken();
  if (token) headers.Authorization = `Bearer ${token}`;
  return headers;
}

async function graphHash(serializedGraph: string): Promise<string> {
  const bytes = new TextEncoder().encode(serializedGraph);
  const digest = await crypto.subtle.digest('SHA-256', bytes);
  return [...new Uint8Array(digest)].map((byte) => byte.toString(16).padStart(2, '0')).join('');
}

async function putJson(path: string, body: unknown): Promise<Response> {
  return fetch(`${BRIDGE_BASE}${path}`, {
    method: 'PUT',
    headers: bridgeHeaders(),
    body: JSON.stringify(body),
  });
}

async function postJson(path: string, body: unknown): Promise<Response> {
  return fetch(`${BRIDGE_BASE}${path}`, {
    method: 'POST',
    headers: bridgeHeaders(),
    body: JSON.stringify(body),
  });
}

/** Keeps pcg-server synchronized with the authoritative in-memory Web document. */
export function useEditorBridge(options: EditorBridgeOptions): void {
  const graphText = JSON.stringify(options.graph);
  const graphPath = options.graphPath;
  const editPathKey = JSON.stringify(options.editPath);
  const selectedNodeId = options.selectedNodeId;
  const previewTargetNodeId = options.previewTargetNodeId;
  const applyCommandsRef = useRef(options.applyCommands);
  const capturePreviewRef = useRef(options.capturePreview);
  const hashRef = useRef('');
  const cursorRef = useRef(0);
  const captureRequestRef = useRef(0);
  const pollingRef = useRef(false);
  const sessionIdRef = useRef(crypto.randomUUID());
  const clientRevisionRef = useRef(0);
  const sessionKey = JSON.stringify([graphText, graphPath, editPathKey, selectedNodeId, previewTargetNodeId]);
  const latestSessionKeyRef = useRef(sessionKey);
  latestSessionKeyRef.current = sessionKey;
  applyCommandsRef.current = options.applyCommands;
  capturePreviewRef.current = options.capturePreview;

  const pushSession = useCallback(async () => {
    const scheduledGraphText = graphText;
    const scheduledSessionKey = sessionKey;
    const hash = await graphHash(scheduledGraphText);
    if (scheduledSessionKey !== latestSessionKeyRef.current) return;
    hashRef.current = hash;
    const response = await putJson('/session', {
      sessionId: sessionIdRef.current,
      clientRevision: ++clientRevisionRef.current,
      graphPath,
      editPath: JSON.parse(editPathKey) as string[],
      selectedNodeId,
      previewTargetNodeId,
      graphHash: hash,
      graph: JSON.parse(scheduledGraphText) as GraphJson,
      nodeManifest: options.nodeManifest,
      updatedAt: Date.now(),
    });
    if (!response.ok) throw new Error(`session sync failed: HTTP ${response.status}`);
  }, [graphText, graphPath, editPathKey, selectedNodeId, previewTargetNodeId, sessionKey, options.nodeManifest]);

  useEffect(() => {
    const debounce = window.setTimeout(() => void pushSession().catch(console.warn), 250);
    const heartbeat = window.setInterval(
      () => void (async () => {
        try {
          const response = await postJson('/session/heartbeat', { sessionId: sessionIdRef.current });
          if (response.status === 409) await pushSession();
        } catch (error) {
          console.warn('[editor-bridge] heartbeat failed', error);
        }
      })(),
      HEARTBEAT_MS,
    );
    return () => {
      window.clearTimeout(debounce);
      window.clearInterval(heartbeat);
    };
  }, [pushSession]);

  const uploadCapture = useCallback(async (requestId: number) => {
    const capture = capturePreviewRef.current();
    if (!capture) return false;
    const response = await putJson('/preview/screenshot', { requestId, ...capture });
    if (!response.ok) throw new Error(`preview upload failed: HTTP ${response.status}`);
    return true;
  }, []);

  // Maintain a recent screenshot even before an Agent explicitly requests one.
  useEffect(() => {
    const debounce = window.setTimeout(
      () => void uploadCapture(captureRequestRef.current).catch(console.warn),
      900,
    );
    return () => window.clearTimeout(debounce);
  }, [graphText, selectedNodeId, previewTargetNodeId, uploadCapture]);

  useEffect(() => {
    const poll = async () => {
      if (pollingRef.current) return;
      pollingRef.current = true;
      try {
        const headers = bridgeHeaders();
        const [sessionResponse, patchesResponse] = await Promise.all([
          fetch(`${BRIDGE_BASE}/session`, { headers }),
          fetch(`${BRIDGE_BASE}/graph/patches?after=${cursorRef.current}`, { headers }),
        ]);
        if (sessionResponse.ok) {
          const session = (await sessionResponse.json()) as SessionResponse;
          const requested = session.captureRequestId ?? 0;
          if (requested > captureRequestRef.current && await uploadCapture(requested)) {
            captureRequestRef.current = requested;
          }
        }
        if (patchesResponse.ok) {
          const payload = (await patchesResponse.json()) as PatchResponse;
          const patches = payload.patches ?? [];
          if (patches.length > 0) {
            // Process one command at a time. The resulting graph hash must be
            // published before a second command based on the old snapshot can run.
            const patch = patches[0];
            const currentPath = JSON.parse(editPathKey) as string[];
            const samePath = JSON.stringify(patch.editPath) === editPathKey;
            const sameGraph = !hashRef.current || patch.baseGraphHash === hashRef.current;
            const results = samePath && sameGraph
              ? await applyCommandsRef.current([patch])
              : [{
                id: patch.id,
                ok: false,
                error: samePath ? 'graph_conflict' : `edit_path_changed:${currentPath.join('/')}`,
              }];
            await postJson('/graph/patches/ack', {
              ids: [patch.id],
              results,
            });
            cursorRef.current = patch.id;
          }
        }
      } catch (error) {
        console.warn('[editor-bridge] poll failed', error);
      } finally {
        pollingRef.current = false;
      }
    };
    void poll();
    const timer = window.setInterval(() => void poll(), POLL_MS);
    return () => window.clearInterval(timer);
  }, [editPathKey, uploadCapture]);
}
