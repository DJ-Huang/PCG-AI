// Left-side embedded Agent panel: connected model selection, real multipart
// turns, SSE events, and explicit approval for every graph write.

import { useCallback, useEffect, useMemo, useRef, useState } from 'react';

import AgentComposer, { type AgentAttachment } from './AgentComposer';
import AgentMessageList, { type AgentMessage } from './AgentMessageList';
import {
  cancelTurn,
  decideTurn,
  getAgentSettings,
  getProviders,
  setAgentSettings,
  startTurn,
  type AgentStreamEvent,
  type ProviderDescriptor,
  type ToolCallEvent,
  type TurnDecision,
} from './agentClient';
import type { AgentAction, AgentActionResult } from './agentCommands';

interface AgentPanelProps {
  // Kept during the migration so App's existing graph command wiring remains
  // source-compatible. Real Agent writes now use the shared MCP dispatcher.
  onApplyActions: (actions: AgentAction[]) => AgentActionResult[];
  onOpenSettings?: () => void;
  providerRevision?: number;
}

const MIN_WIDTH = 280;
const MAX_WIDTH = 680;
const SESSION_ID = `web-${Date.now()}-${Math.random().toString(36).slice(2, 9)}`;

let messageCounter = 0;
const nextMessageId = () => `m${++messageCounter}`;

function errorMessage(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}

function toolResultSummary(data: Record<string, unknown>): string {
  const name = typeof data.name === 'string' ? data.name : 'tool';
  const result = data.result as { isError?: boolean; structuredContent?: Record<string, unknown> } | undefined;
  const failed = result?.isError === true;
  const detail = result?.structuredContent?.error;
  return `${failed ? '✗' : '✓'} ${name}${detail ? ` — ${String(detail)}` : ''}`;
}

export default function AgentPanel({ onOpenSettings, providerRevision = 0 }: AgentPanelProps) {
  const [messages, setMessages] = useState<AgentMessage[]>([]);
  const [sending, setSending] = useState(false);
  const [width, setWidth] = useState(340);
  const [providers, setProviders] = useState<ProviderDescriptor[]>([]);
  const [providerId, setProviderId] = useState('');
  const [modelId, setModelId] = useState('');
  const [connectionError, setConnectionError] = useState('');
  const [activeTurnId, setActiveTurnId] = useState('');
  const [pendingCalls, setPendingCalls] = useState<ToolCallEvent[]>([]);
  const [decisions, setDecisions] = useState<Record<string, 'approve' | 'reject'>>({});
  const abortRef = useRef<AbortController | null>(null);
  const mountedRef = useRef(true);

  const selectedProvider = useMemo(
    () => providers.find((provider) => provider.id === providerId),
    [providers, providerId],
  );
  const selectedModel = selectedProvider?.models.find((model) => model.id === modelId);
  const connected = selectedProvider?.connection.status === 'connected' && Boolean(selectedModel);
  const connectedProviders = useMemo(
    () => providers.filter((provider) => provider.connection.status === 'connected' && provider.models.length > 0),
    [providers],
  );

  const refreshProviders = useCallback(async () => {
    const [nextProviders, settings] = await Promise.all([getProviders(), getAgentSettings()]);
    if (!mountedRef.current) return;
    setProviders(nextProviders);
    setProviderId(settings.providerId);
    setModelId(settings.modelId);
    setConnectionError('');
  }, []);

  useEffect(() => {
    mountedRef.current = true;
    void refreshProviders().catch((error) => setConnectionError(errorMessage(error)));
    return () => {
      mountedRef.current = false;
      abortRef.current?.abort();
    };
  }, [providerRevision, refreshProviders]);

  const push = useCallback((message: AgentMessage) => {
    setMessages((previous) => [...previous, message]);
  }, []);

  const handleEvent = useCallback((event: AgentStreamEvent) => {
    const data = event.data;
    if (event.type === 'turn.created') {
      const id = typeof data.turnId === 'string' ? data.turnId : '';
      setActiveTurnId(id);
      return;
    }
    if (event.type === 'message.delta') {
      const text = typeof data.text === 'string' ? data.text : '';
      const turnId = typeof data.turnId === 'string' ? data.turnId : '';
      if (!text) return;
      setMessages((previous) => {
        const last = previous.at(-1);
        if (last?.role === 'assistant' && last.turnId === turnId) {
          return [...previous.slice(0, -1), { ...last, text: last.text + text }];
        }
        return [...previous, { id: nextMessageId(), role: 'assistant', text, turnId }];
      });
      return;
    }
    if (event.type === 'tool.call') {
      const call = data as unknown as ToolCallEvent;
      if (!call.requiresApproval) push({ id: nextMessageId(), role: 'action', text: `… ${call.name}` });
      return;
    }
    if (event.type === 'tool.result') {
      push({ id: nextMessageId(), role: 'action', text: toolResultSummary(data) });
      return;
    }
    if (event.type === 'approval.required') {
      const turnId = typeof data.turnId === 'string' ? data.turnId : '';
      const calls = Array.isArray(data.calls)
        ? (data.calls as ToolCallEvent[]).map((call) => ({ ...call, turnId, requiresApproval: true }))
        : [];
      setActiveTurnId(turnId);
      setPendingCalls(calls);
      setDecisions(Object.fromEntries(calls.map((call) => [call.toolCallId || (call as unknown as { id: string }).id, 'reject'])));
      return;
    }
    if (event.type === 'turn.error') {
      const nested = data.error as { message?: string } | undefined;
      push({ id: nextMessageId(), role: 'action', text: `✗ ${nested?.message ?? 'Agent turn failed'}` });
    }
  }, [push]);

  const selectModel = useCallback(async (nextProviderId: string, nextModelId?: string) => {
    const provider = providers.find((item) => item.id === nextProviderId);
    const model = nextModelId ?? provider?.models[0]?.id ?? '';
    setProviderId(nextProviderId);
    setModelId(model);
    if (!model) {
      onOpenSettings?.();
      return;
    }
    try {
      await setAgentSettings({ providerId: nextProviderId, modelId: model });
    } catch (error) {
      setConnectionError(errorMessage(error));
      onOpenSettings?.();
    }
  }, [onOpenSettings, providers]);

  const handleSend = useCallback(async (text: string, attachments: AgentAttachment[]) => {
    if (!connected || !selectedProvider || !selectedModel) {
      setConnectionError('Connect a Provider and select a validated model first.');
      onOpenSettings?.();
      return;
    }
    if (attachments.some((attachment) => attachment.file.type.startsWith('image/')) && !selectedModel.capabilities.imageInput) {
      push({ id: nextMessageId(), role: 'action', text: `✗ ${selectedModel.name} does not support image input` });
      return;
    }
    push({
      id: nextMessageId(),
      role: 'user',
      text: text || '(attachments only)',
      attachments: attachments.map((attachment) => ({ name: attachment.file.name })),
    });
    setSending(true);
    setPendingCalls([]);
    const abort = new AbortController();
    abortRef.current = abort;
    try {
      await startTurn(
        { message: text, sessionId: SESSION_ID, providerId, modelId, attachments },
        handleEvent,
        abort.signal,
      );
    } catch (error) {
      if (!(error instanceof DOMException && error.name === 'AbortError')) {
        push({ id: nextMessageId(), role: 'action', text: `✗ ${errorMessage(error)}` });
      }
    } finally {
      if (abortRef.current === abort) abortRef.current = null;
      setSending(false);
    }
  }, [connected, handleEvent, modelId, onOpenSettings, providerId, push, selectedModel, selectedProvider]);

  const handleStop = useCallback(() => {
    abortRef.current?.abort();
    abortRef.current = null;
    setSending(false);
    if (activeTurnId) void cancelTurn(activeTurnId).catch(() => undefined);
    push({ id: nextMessageId(), role: 'action', text: '■ Turn stopped' });
  }, [activeTurnId, push]);

  const submitDecisions = useCallback(async () => {
    if (!activeTurnId || pendingCalls.length === 0) return;
    const payload: TurnDecision[] = pendingCalls.map((call) => {
      const callId = call.toolCallId || (call as unknown as { id: string }).id;
      return { toolCallId: callId, decision: decisions[callId] ?? 'reject' };
    });
    setPendingCalls([]);
    setSending(true);
    const abort = new AbortController();
    abortRef.current = abort;
    try {
      await decideTurn(activeTurnId, payload, handleEvent, abort.signal);
    } catch (error) {
      push({ id: nextMessageId(), role: 'action', text: `✗ ${errorMessage(error)}` });
    } finally {
      if (abortRef.current === abort) abortRef.current = null;
      setSending(false);
    }
  }, [activeTurnId, decisions, handleEvent, pendingCalls, push]);

  const onResizeStart = useCallback((event: React.MouseEvent) => {
    event.preventDefault();
    const startX = event.clientX;
    const startWidth = width;
    const onMove = (move: MouseEvent) => setWidth(Math.min(MAX_WIDTH, Math.max(MIN_WIDTH, startWidth + move.clientX - startX)));
    const onUp = () => {
      window.removeEventListener('mousemove', onMove);
      window.removeEventListener('mouseup', onUp);
      document.body.classList.remove('pcg-agent--resizing');
    };
    document.body.classList.add('pcg-agent--resizing');
    window.addEventListener('mousemove', onMove);
    window.addEventListener('mouseup', onUp);
  }, [width]);

  return (
    <div className="pcg-agent" style={{ width }}>
      <div className="pcg-agent__header">
        <span className="pcg-agent__title">Agent</span>
        <select
          className="pcg-agent__provider-select"
          value={providerId}
          aria-label="Agent Provider"
          onChange={(event) => void selectModel(event.target.value)}
        >
          <option value="">Choose Provider</option>
          {connectedProviders.map((provider) => (
            <option key={provider.id} value={provider.id}>
              {provider.name}
            </option>
          ))}
        </select>
        <select
          className="pcg-agent__model-select"
          value={modelId}
          aria-label="Agent model"
          disabled={!selectedProvider?.models.length}
          onChange={(event) => void selectModel(providerId, event.target.value)}
        >
          {!selectedProvider?.models.length && <option value="">No models</option>}
          {selectedProvider?.models.map((model) => <option key={model.id} value={model.id}>{model.name}</option>)}
        </select>
      </div>

      {!connected && (
        <div className="pcg-agent__connection-notice">
          <span>{connectionError || 'Connect an AI Provider in PCG Settings to start.'}</span>
          <button type="button" onClick={onOpenSettings}>Open Settings</button>
        </div>
      )}

      <AgentMessageList messages={messages} />

      {pendingCalls.length > 0 && (
        <div className="pcg-agent-approval">
          <div className="pcg-agent-approval__title">Approve graph writes</div>
          {pendingCalls.map((call) => {
            const callId = call.toolCallId || (call as unknown as { id: string }).id;
            return (
              <div key={callId} className="pcg-agent-approval__call">
                <div><strong>{call.name}</strong><pre>{JSON.stringify(call.arguments, null, 2)}</pre></div>
                <div className="pcg-agent-approval__choices">
                  <button type="button" className={decisions[callId] === 'approve' ? 'is-selected' : ''} onClick={() => setDecisions((stored) => ({ ...stored, [callId]: 'approve' }))}>Approve</button>
                  <button type="button" className={decisions[callId] !== 'approve' ? 'is-selected is-reject' : ''} onClick={() => setDecisions((stored) => ({ ...stored, [callId]: 'reject' }))}>Reject</button>
                </div>
              </div>
            );
          })}
          <button type="button" className="pcg-agent-approval__continue" onClick={() => void submitDecisions()}>Continue</button>
        </div>
      )}

      <AgentComposer
        sending={sending}
        agentLabel={connected ? `${selectedProvider?.name} · ${selectedModel?.name}` : 'Connect Provider'}
        disabled={!connected || pendingCalls.length > 0}
        onSend={handleSend}
        onStop={handleStop}
      />
      <div className="pcg-agent__resize-handle" onMouseDown={onResizeStart} title="Drag to resize" />
    </div>
  );
}
