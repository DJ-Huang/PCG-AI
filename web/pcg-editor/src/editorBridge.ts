import { useCallback, useEffect, useRef } from 'react';

import { getAgentToken } from './agent/agentClient';
import { canonicalShotPayload } from './cameraTrack';
import { isShotCommand, type GraphCommandResult, type QueuedGraphCommand } from './graphCommands';
import type { GraphJson } from './graphSchema';
import type { NodeManifest } from './nodeManifest';
import type { CameraCommand } from './physicalCamera';
import type { CaptureOptions, PreviewCapture } from './PreviewViewport';
import type { ShotDocument } from './shot';

const BRIDGE_BASE = '/api/editor-bridge';
const HEARTBEAT_MS = 5_000;
const POLL_MS = 400;

interface EditorBridgeOptions {
  sessionId: string;
  graph: GraphJson;
  nodeManifest: NodeManifest;
  graphPath: string;
  editPath: string[];
  selectedNodeId: string | null;
  previewTargetNodeId: string | null;
  shot: ShotDocument;
  applyCommands: (commands: QueuedGraphCommand[]) => Promise<GraphCommandResult[]>;
  capturePreview: (options?: CaptureOptions) => PreviewCapture | null;
  applyCameraCommand: (command: CameraCommand) => Record<string, unknown> | null;
}

interface SessionResponse {
  ok: boolean;
  captureRequestId?: number;
  captureOptions?: CaptureOptions;
  cameraCommandId?: number;
  cameraCommand?: CameraCommand;
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
export function useEditorBridge(options: EditorBridgeOptions): () => Promise<void> {
  const graphText = JSON.stringify(options.graph);
  const graphPath = options.graphPath;
  const editPathKey = JSON.stringify(options.editPath);
  const selectedNodeId = options.selectedNodeId;
  const previewTargetNodeId = options.previewTargetNodeId;
  const shotText = canonicalShotPayload(options.shot);
  const applyCommandsRef = useRef(options.applyCommands);
  const capturePreviewRef = useRef(options.capturePreview);
  const applyCameraCommandRef = useRef(options.applyCameraCommand);
  const hashRef = useRef('');
  const shotHashRef = useRef('');
  const cursorRef = useRef(0);
  const captureRequestRef = useRef(0);
  const cameraCommandRef = useRef(0);
  const pollingRef = useRef(false);
  const clientRevisionRef = useRef(0);
  const sessionKey = JSON.stringify([graphText, graphPath, editPathKey, selectedNodeId, previewTargetNodeId, shotText]);
  const latestSessionKeyRef = useRef(sessionKey);
  latestSessionKeyRef.current = sessionKey;
  applyCommandsRef.current = options.applyCommands;
  capturePreviewRef.current = options.capturePreview;
  applyCameraCommandRef.current = options.applyCameraCommand;

  const pushSession = useCallback(async () => {
    const scheduledGraphText = graphText;
    const scheduledSessionKey = sessionKey;
    const hash = await graphHash(scheduledGraphText);
    const nextShotHash = await graphHash(shotText);
    if (scheduledSessionKey !== latestSessionKeyRef.current) return;
    hashRef.current = hash;
    shotHashRef.current = nextShotHash;
    const response = await putJson('/session', {
      sessionId: options.sessionId,
      clientRevision: ++clientRevisionRef.current,
      graphPath,
      editPath: JSON.parse(editPathKey) as string[],
      selectedNodeId,
      previewTargetNodeId,
      selectedCameraId: options.shot.activeCameraId,
      graphHash: hash,
      shotHash: nextShotHash,
      graph: JSON.parse(scheduledGraphText) as GraphJson,
      shot: JSON.parse(shotText),
      nodeManifest: options.nodeManifest,
      updatedAt: Date.now(),
    });
    if (response.status === 409) {
      const conflict = await response.json().catch(() => null) as { error?: string } | null;
      // A concurrent push with a higher client revision already published this
      // snapshot (or a newer one), so the synchronization barrier is satisfied.
      if (conflict?.error === 'stale_session_update') return;
    }
    if (!response.ok) throw new Error(`session sync failed: HTTP ${response.status}`);
  }, [graphText, graphPath, editPathKey, selectedNodeId, previewTargetNodeId, shotText, sessionKey, options.nodeManifest, options.sessionId, options.shot.activeCameraId]);

  useEffect(() => {
    const debounce = window.setTimeout(() => void pushSession().catch(console.warn), 250);
    const heartbeat = window.setInterval(
      () => void (async () => {
        try {
          const response = await postJson('/session/heartbeat', { sessionId: options.sessionId });
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
  }, [pushSession, options.sessionId]);

  const uploadCapture = useCallback(async (requestId: number, captureOptions?: CaptureOptions) => {
    const capture = capturePreviewRef.current(captureOptions);
    if (!capture) return false;
    const response = await putJson('/preview/screenshot', { sessionId: options.sessionId, requestId, ...capture });
    if (!response.ok) throw new Error(`preview upload failed: HTTP ${response.status}`);
    return true;
  }, [options.sessionId]);

  const echoCameraState = useCallback(async (commandId: number, state: Record<string, unknown> | null) => {
    const response = await putJson('/camera/state', {
      sessionId: options.sessionId,
      commandId,
      state: state ?? {},
    });
    if (!response.ok) throw new Error(`camera echo failed: HTTP ${response.status}`);
  }, [options.sessionId]);

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
          fetch(`${BRIDGE_BASE}/session?sessionId=${encodeURIComponent(options.sessionId)}`, { headers }),
          fetch(`${BRIDGE_BASE}/graph/patches?sessionId=${encodeURIComponent(options.sessionId)}&after=${cursorRef.current}`, { headers }),
        ]);
        if (sessionResponse.ok) {
          const session = (await sessionResponse.json()) as SessionResponse;
          const cameraCommandId = session.cameraCommandId ?? 0;
          if (cameraCommandId > cameraCommandRef.current) {
            const applied = applyCameraCommandRef.current(session.cameraCommand ?? {});
            cameraCommandRef.current = cameraCommandId;
            await echoCameraState(cameraCommandId, applied);
          }
          const requested = session.captureRequestId ?? 0;
          if (requested > captureRequestRef.current && await uploadCapture(requested, session.captureOptions)) {
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
            const shotCommand = isShotCommand(patch);
            const sameGraph = !hashRef.current || patch.baseGraphHash === hashRef.current;
            const sameShot = !patch.baseShotHash || patch.baseShotHash === shotHashRef.current;
            const lockOk = shotCommand
              ? (patch.type === 'previewShot' || sameShot)
              : sameGraph;
            const results = samePath && lockOk
              ? await applyCommandsRef.current([patch])
              : [{
                id: patch.id,
                ok: false,
                error: samePath
                  ? (shotCommand ? 'shot_conflict' : 'graph_conflict')
                  : `edit_path_changed:${currentPath.join('/')}`,
              }];
            await postJson('/graph/patches/ack', {
              sessionId: options.sessionId,
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
  }, [editPathKey, uploadCapture, echoCameraState, options.sessionId]);

  // Callers that are about to start an Agent turn can await this barrier so
  // MCP tools observe the current selection instead of the debounced session.
  return pushSession;
}
