import { useCallback, useEffect, useMemo, useRef, useState } from 'react';

import AgentComposer, { type AgentAttachment } from './AgentComposer';
import AgentHistory from './AgentHistory';
import AgentMessageList from './AgentMessageList';
import {
  cancelTurn,
  decideTurn,
  deleteAgentSession,
  getAgentSession,
  getAgentSettings,
  getProviders,
  listAgentSessions,
  renameAgentSession,
  setAgentSettings,
  startTurn,
  type AgentMessageRecord,
  type AgentPart,
  type AgentSessionDescriptor,
  type ReasoningEffort,
  type AgentStreamEvent,
  type ProviderDescriptor,
  type ToolCallEvent,
  type TurnDecision,
} from './agentClient';
import type { AgentAction, AgentActionResult } from './agentCommands';

interface AgentPanelProps {
  onApplyActions: (actions: AgentAction[]) => AgentActionResult[];
  onOpenSettings?: () => void;
  syncEditorContext?: () => Promise<void>;
  editorSessionId?: string;
  providerRevision?: number;
  launchRequest?: AgentLaunchRequest | null;
  onLaunchConsumed?: (requestId: string) => void;
}

export interface AgentLaunchRequest {
  id: string;
  message: string;
  files: File[];
}

const MIN_WIDTH = 300;
const MAX_WIDTH = 760;
const LAST_SESSION_KEY = 'pcg-agent-last-session';
const SHOW_REASONING_KEY = 'pcg-agent-show-reasoning';

const newSessionId = () => `session-${crypto.randomUUID()}`;
const localId = (prefix: string) => `${prefix}-${crypto.randomUUID()}`;

function errorMessage(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}

function upsertAssistant(
  messages: AgentMessageRecord[], messageId: string, turnId: string, updater: (message: AgentMessageRecord) => AgentMessageRecord,
): AgentMessageRecord[] {
  const index = messages.findIndex((message) => message.id === messageId);
  if (index < 0) {
    return [...messages, updater({ id: messageId, role: 'assistant', turnId, status: 'running', createdAt: Date.now(), parts: [] })];
  }
  return messages.map((message, item) => item === index ? updater(message) : message);
}

function upsertPart(message: AgentMessageRecord, part: AgentPart): AgentMessageRecord {
  const existing = message.parts.findIndex((item) => item.id === part.id);
  const parts = existing < 0
    ? [...message.parts, part]
    : message.parts.map((item, index) => index === existing ? { ...item, ...part } : item);
  return { ...message, parts };
}

export default function AgentPanel({
  onOpenSettings,
  syncEditorContext,
  editorSessionId = '',
  providerRevision = 0,
  launchRequest = null,
  onLaunchConsumed,
}: AgentPanelProps) {
  const [messages, setMessages] = useState<AgentMessageRecord[]>([]);
  const [sending, setSending] = useState(false);
  const [width, setWidth] = useState(() => Math.max(300, Math.min(380, Math.round(window.innerWidth * 0.23))));
  const [collapsed, setCollapsed] = useState(false);
  const [providers, setProviders] = useState<ProviderDescriptor[]>([]);
  const [providerId, setProviderId] = useState('');
  const [modelId, setModelId] = useState('');
  const [reasoningEffort, setReasoningEffort] = useState<ReasoningEffort>('high');
  const [connectionError, setConnectionError] = useState('');
  const [sessionId, setSessionId] = useState(() => localStorage.getItem(LAST_SESSION_KEY) || newSessionId());
  const [activeTurnId, setActiveTurnId] = useState('');
  const [pendingCalls, setPendingCalls] = useState<ToolCallEvent[]>([]);
  const [decisions, setDecisions] = useState<Record<string, 'approve' | 'reject'>>({});
  const [showHistory, setShowHistory] = useState(false);
  const [sessions, setSessions] = useState<AgentSessionDescriptor[]>([]);
  const [historyQuery, setHistoryQuery] = useState('');
  const [historyLoading, setHistoryLoading] = useState(false);
  const [historyCursor, setHistoryCursor] = useState(0);
  const [showReasoning, setShowReasoning] = useState(() => localStorage.getItem(SHOW_REASONING_KEY) !== 'false');
  const abortRef = useRef<AbortController | null>(null);
  const mountedRef = useRef(true);
  const consumedLaunchRequestRef = useRef('');

  const selectedProvider = useMemo(() => providers.find((provider) => provider.id === providerId), [providers, providerId]);
  const selectedModel = selectedProvider?.models.find((model) => model.id === modelId);
  const connectedProviders = useMemo(
    () => providers.filter((provider) => provider.connection.status === 'connected' && provider.models.length > 0),
    [providers],
  );
  const connected = selectedProvider?.connection.status === 'connected' && Boolean(selectedModel);

  const refreshHistory = useCallback(async (query: string) => {
    setHistoryLoading(true);
    try {
      const result = await listAgentSessions(query);
      if (mountedRef.current) {
        setSessions(result.sessions);
        setHistoryCursor(result.nextCursor);
      }
    } finally {
      if (mountedRef.current) setHistoryLoading(false);
    }
  }, []);

  const loadOlderHistory = useCallback(async () => {
    if (!historyCursor || historyLoading) return;
    setHistoryLoading(true);
    try {
      const result = await listAgentSessions(historyQuery, historyCursor);
      if (!mountedRef.current) return;
      setSessions((previous) => {
        const known = new Set(previous.map((session) => session.id));
        return [...previous, ...result.sessions.filter((session) => !known.has(session.id))];
      });
      setHistoryCursor(result.nextCursor);
    } catch (error) {
      setConnectionError(errorMessage(error));
    } finally {
      if (mountedRef.current) setHistoryLoading(false);
    }
  }, [historyCursor, historyLoading, historyQuery]);

  const refreshProviders = useCallback(async () => {
    const [nextProviders, settings] = await Promise.all([getProviders(), getAgentSettings()]);
    if (!mountedRef.current) return;
    setProviders(nextProviders);
    setProviderId(settings.providerId);
    setModelId(settings.modelId);
    const configuredModel = nextProviders.find((provider) => provider.id === settings.providerId)
      ?.models.find((model) => model.id === settings.modelId);
    const efforts = configuredModel?.capabilities.reasoningEfforts ?? [];
    const configuredEffort = settings.reasoningEffort ?? configuredModel?.capabilities.defaultReasoningEffort ?? 'high';
    setReasoningEffort(efforts.length === 0 || efforts.includes(configuredEffort) ? configuredEffort : (configuredModel?.capabilities.defaultReasoningEffort ?? efforts[0]));
    setConnectionError('');
  }, []);

  const openSession = useCallback(async (id: string) => {
    const session = await getAgentSession(id);
    setSessionId(session.id);
    localStorage.setItem(LAST_SESSION_KEY, session.id);
    setMessages(session.messages ?? []);
    setPendingCalls([]);
    setProviderId(session.providerId);
    setModelId(session.modelId);
    setShowHistory(false);
  }, []);

  useEffect(() => {
    mountedRef.current = true;
    void refreshProviders().catch((error) => setConnectionError(errorMessage(error)));
    void refreshHistory('').catch(() => undefined);
    const stored = localStorage.getItem(LAST_SESSION_KEY);
    if (stored) void openSession(stored).catch(() => localStorage.removeItem(LAST_SESSION_KEY));
    return () => {
      mountedRef.current = false;
      abortRef.current?.abort();
    };
  }, [providerRevision, refreshHistory, refreshProviders, openSession]);

  useEffect(() => {
    if (!showHistory) return;
    const timer = window.setTimeout(() => void refreshHistory(historyQuery).catch(() => undefined), 180);
    return () => window.clearTimeout(timer);
  }, [historyQuery, refreshHistory, showHistory]);

  useEffect(() => {
    const refresh = () => setShowReasoning(localStorage.getItem(SHOW_REASONING_KEY) !== 'false');
    window.addEventListener('pcg-agent-settings-changed', refresh);
    window.addEventListener('storage', refresh);
    return () => {
      window.removeEventListener('pcg-agent-settings-changed', refresh);
      window.removeEventListener('storage', refresh);
    };
  }, []);

  const handleEvent = useCallback((event: AgentStreamEvent) => {
    const data = event.data;
    const turnId = typeof data.turnId === 'string' ? data.turnId : activeTurnId;
    const messageId = typeof data.messageId === 'string' ? data.messageId : `assistant-${turnId}`;
    const partId = typeof data.partId === 'string' ? data.partId : localId('part');
    if (event.type === 'turn.created') {
      const nextSession = typeof data.sessionId === 'string' ? data.sessionId : sessionId;
      setSessionId(nextSession);
      localStorage.setItem(LAST_SESSION_KEY, nextSession);
      setActiveTurnId(turnId);
      setMessages((previous) => upsertAssistant(previous, messageId, turnId, (message) => message));
      return;
    }
    if (event.type === 'reasoning.started' || event.type === 'message.started') {
      const type = event.type.startsWith('reasoning') ? 'reasoning' : 'text';
      setMessages((previous) => upsertAssistant(previous, messageId, turnId, (message) => upsertPart(message, {
        id: partId, type, ordinal: Number(data.ordinal ?? message.parts.length), status: 'streaming', text: '',
      })));
      return;
    }
    if (event.type === 'reasoning.delta' || event.type === 'message.delta') {
      const type = event.type.startsWith('reasoning') ? 'reasoning' : 'text';
      const delta = String(data.delta ?? data.text ?? '');
      if (!delta) return;
      setMessages((previous) => upsertAssistant(previous, messageId, turnId, (message) => {
        const current = message.parts.find((part) => part.id === partId);
        return upsertPart(message, {
          id: partId, type, ordinal: current?.ordinal ?? message.parts.length,
          status: 'streaming', text: `${current?.text ?? ''}${delta}`,
        });
      }));
      return;
    }
    if (event.type === 'reasoning.completed' || event.type === 'message.completed') {
      const type = event.type.startsWith('reasoning') ? 'reasoning' : 'text';
      setMessages((previous) => upsertAssistant(previous, messageId, turnId, (message) => {
        const current = message.parts.find((part) => part.id === partId);
        return upsertPart(message, { id: partId, type, ordinal: current?.ordinal ?? message.parts.length, status: 'completed', text: String(data.text ?? current?.text ?? '') });
      }));
      return;
    }
    if (event.type === 'tool.call') {
      const call = data as unknown as ToolCallEvent;
      setMessages((previous) => upsertAssistant(previous, messageId, turnId, (message) => upsertPart(message, {
        id: call.partId ?? partId, type: 'tool', ordinal: call.ordinal ?? message.parts.length,
        status: call.requiresApproval ? 'approval_required' : 'running', toolCallId: call.toolCallId,
        name: call.name, arguments: call.arguments,
      })));
      return;
    }
    if (event.type === 'tool.result') {
      const result = data.result as { isError?: boolean } | undefined;
      setMessages((previous) => upsertAssistant(previous, messageId, turnId, (message) => {
        const current = message.parts.find((part) => part.id === partId || part.toolCallId === data.toolCallId);
        return upsertPart(message, {
          id: current?.id ?? partId, type: 'tool', ordinal: current?.ordinal ?? message.parts.length,
          status: result?.isError ? 'failed' : 'completed', toolCallId: String(data.toolCallId ?? current?.toolCallId ?? ''),
          name: String(data.name ?? current?.name ?? 'tool'), arguments: current?.arguments,
          result: data.result, cached: data.cached === true, durationMs: Number(data.durationMs ?? current?.durationMs ?? 0),
        });
      }));
      return;
    }
    if (event.type === 'approval.required') {
      const calls = Array.isArray(data.calls) ? data.calls as ToolCallEvent[] : [];
      setPendingCalls(calls);
      setDecisions(Object.fromEntries(calls.map((call) => [call.toolCallId, 'approve'])));
      setMessages((previous) => upsertAssistant(previous, messageId, turnId, (message) => {
        const withCalls = calls.reduce((current, call) => upsertPart(current, {
          id: call.partId ?? `tool-${call.toolCallId}`,
          type: 'tool',
          ordinal: call.ordinal ?? current.parts.length,
          status: 'approval_required',
          toolCallId: call.toolCallId,
          name: call.name,
          arguments: call.arguments,
        }), message);
        return { ...withCalls, status: 'awaiting_approval' };
      }));
      return;
    }
    if (event.type === 'turn.completed') {
      setMessages((previous) => upsertAssistant(previous, messageId, turnId, (message) => ({ ...message, status: 'completed' })));
      setPendingCalls([]);
      setActiveTurnId('');
      void refreshHistory('').catch(() => undefined);
      return;
    }
    if (event.type === 'turn.error') {
      const nested = data.error as { code?: string; message?: string; retryable?: boolean } | undefined;
      const status = nested?.code === 'cancelled' ? 'interrupted' : 'error';
      setMessages((previous) => upsertAssistant(previous, messageId, turnId, (message) => upsertPart(
        { ...message, status },
        { id: partId, type: 'error', ordinal: message.parts.length, status: 'error', error: { code: nested?.code ?? 'agent_error', message: nested?.message ?? 'Agent turn failed', retryable: nested?.retryable ?? false } },
      )));
      void refreshHistory('').catch(() => undefined);
      setActiveTurnId('');
    }
  }, [activeTurnId, refreshHistory, sessionId]);

  const selectModel = useCallback(async (nextProviderId: string, nextModelId?: string) => {
    const provider = providers.find((item) => item.id === nextProviderId);
    const model = nextModelId ?? provider?.models[0]?.id ?? '';
    if (!model) return onOpenSettings?.();
    setProviderId(nextProviderId);
    setModelId(model);
    const selected = provider?.models.find((item) => item.id === model);
    const efforts = selected?.capabilities.reasoningEfforts ?? [];
    const nextEffort = efforts.length === 0 || efforts.includes(reasoningEffort)
      ? reasoningEffort
      : (selected?.capabilities.defaultReasoningEffort ?? efforts[0]);
    setReasoningEffort(nextEffort);
    try { await setAgentSettings({ providerId: nextProviderId, modelId: model, reasoningEffort: nextEffort }); }
    catch (error) { setConnectionError(errorMessage(error)); }
  }, [onOpenSettings, providers, reasoningEffort]);

  const selectReasoningEffort = useCallback(async (effort: ReasoningEffort) => {
    if (!selectedModel?.capabilities.reasoningEfforts?.includes(effort)) return;
    setReasoningEffort(effort);
    try { await setAgentSettings({ providerId, modelId, reasoningEffort: effort }); }
    catch (error) { setConnectionError(errorMessage(error)); }
  }, [modelId, providerId, selectedModel]);

  const sendTurn = useCallback(async (text: string, attachments: AgentAttachment[]) => {
    if (!connected || !selectedProvider || !selectedModel) return onOpenSettings?.();
    if (attachments.some((attachment) => attachment.file.type.startsWith('image/')) && !selectedModel.capabilities.imageInput) {
      setConnectionError(`${selectedModel.name} does not support image input.`);
      return;
    }
    const optimistic: AgentMessageRecord = {
      id: localId('message'), role: 'user', status: 'completed', createdAt: Date.now(),
      parts: [
        ...(text ? [{ id: localId('part'), type: 'text' as const, ordinal: 0, status: 'completed' as const, text }] : []),
        ...attachments.map((attachment, index) => ({ id: localId('part'), type: 'attachment' as const, ordinal: index + 1, status: 'completed' as const, name: attachment.file.name, mimeType: attachment.file.type })),
      ],
    };
    setMessages((previous) => [...previous, optimistic]);
    setSending(true);
    setPendingCalls([]);
    const abort = new AbortController();
    abortRef.current = abort;
    try {
      await syncEditorContext?.();
      await startTurn({ message: text, sessionId, editorSessionId, providerId, modelId, reasoningEffort, attachments }, handleEvent, abort.signal);
    }
    catch (error) {
      if (!(error instanceof DOMException && error.name === 'AbortError')) setConnectionError(errorMessage(error));
    } finally {
      if (abortRef.current === abort) abortRef.current = null;
      setSending(false);
    }
  }, [connected, editorSessionId, handleEvent, modelId, onOpenSettings, providerId, reasoningEffort, selectedModel, selectedProvider, sessionId, syncEditorContext]);

  useEffect(() => {
    if (!launchRequest || consumedLaunchRequestRef.current === launchRequest.id) return;
    if (!connected || !selectedModel || sending || pendingCalls.length > 0 || showHistory) return;
    if (launchRequest.files.some((file) => file.type.startsWith('image/')) && !selectedModel.capabilities.imageInput) {
      setConnectionError(`${selectedModel.name} does not support the image evidence required for procedural reconstruction.`);
      return;
    }
    consumedLaunchRequestRef.current = launchRequest.id;
    onLaunchConsumed?.(launchRequest.id);
    const attachments: AgentAttachment[] = launchRequest.files.map((file, index) => ({
      id: `${launchRequest.id}-${index}`,
      file,
      previewUrl: null,
    }));
    void sendTurn(launchRequest.message, attachments);
  }, [connected, launchRequest, onLaunchConsumed, pendingCalls.length, selectedModel, sendTurn, sending, showHistory]);

  const retryTurn = useCallback(async (retryTurnId: string) => {
    if (!connected || !selectedProvider || !selectedModel || sending) return;
    setConnectionError('');
    setMessages((previous) => previous.filter((message) => message.turnId !== retryTurnId));
    setSending(true);
    setPendingCalls([]);
    const abort = new AbortController();
    abortRef.current = abort;
    try {
      await syncEditorContext?.();
      await startTurn({
        message: '', sessionId, editorSessionId, providerId, modelId, reasoningEffort, attachments: [], retryTurnId,
      }, handleEvent, abort.signal);
    } catch (error) {
      if (!(error instanceof DOMException && error.name === 'AbortError')) setConnectionError(errorMessage(error));
    } finally {
      if (abortRef.current === abort) abortRef.current = null;
      setSending(false);
    }
  }, [connected, editorSessionId, handleEvent, modelId, providerId, reasoningEffort, selectedModel, selectedProvider, sending, sessionId, syncEditorContext]);

  const handleStop = useCallback(() => {
    abortRef.current?.abort();
    abortRef.current = null;
    setSending(false);
    if (activeTurnId) void cancelTurn(activeTurnId).catch(() => undefined);
    setMessages((previous) => previous.map((message) => message.turnId === activeTurnId ? { ...message, status: 'interrupted' } : message));
    setActiveTurnId('');
  }, [activeTurnId]);

  const submitDecisions = useCallback(async () => {
    if (!activeTurnId || pendingCalls.length === 0) return;
    const payload: TurnDecision[] = pendingCalls.map((call) => ({ toolCallId: call.toolCallId, decision: decisions[call.toolCallId] ?? 'approve' }));
    setPendingCalls([]);
    setSending(true);
    const abort = new AbortController();
    abortRef.current = abort;
    try { await decideTurn(activeTurnId, payload, handleEvent, abort.signal); }
    finally { if (abortRef.current === abort) abortRef.current = null; setSending(false); }
  }, [activeTurnId, decisions, handleEvent, pendingCalls]);

  const startNewChat = useCallback(() => {
    if (sending || pendingCalls.length > 0) handleStop();
    const id = newSessionId();
    setSessionId(id);
    localStorage.removeItem(LAST_SESSION_KEY);
    setMessages([]);
    setPendingCalls([]);
    setShowHistory(false);
  }, [handleStop, pendingCalls.length, sending]);

  const onResizeStart = useCallback((event: React.MouseEvent) => {
    event.preventDefault();
    const startX = event.clientX;
    const startWidth = width;
    const onMove = (move: MouseEvent) => setWidth(Math.min(MAX_WIDTH, Math.max(MIN_WIDTH, startWidth + move.clientX - startX)));
    const onUp = () => { window.removeEventListener('mousemove', onMove); window.removeEventListener('mouseup', onUp); document.body.classList.remove('pcg-agent--resizing'); };
    document.body.classList.add('pcg-agent--resizing');
    window.addEventListener('mousemove', onMove);
    window.addEventListener('mouseup', onUp);
  }, [width]);

  return (
    <div className={`pcg-agent ${collapsed ? 'pcg-agent--collapsed' : ''}`} style={{ width: collapsed ? 36 : width }}>
      {collapsed ? (
        <button type="button" className="pcg-agent__collapse-button" title="Expand Agent panel" aria-label="Expand Agent panel" onClick={() => setCollapsed(false)}>›</button>
      ) : (
        <>
          <div className="pcg-agent__header">
            <span className="pcg-agent__title">Agent</span>
            <span className="pcg-agent__session-title">{sessions.find((session) => session.id === sessionId)?.title ?? 'New chat'}</span>
            <button type="button" className="pcg-agent__header-button" title="New chat" aria-label="New chat" onClick={startNewChat}>＋</button>
            <button type="button" className={`pcg-agent__header-button ${showHistory ? 'is-active' : ''}`} title="Show chat history" aria-label="Show chat history" onClick={() => setShowHistory((value) => !value)}>◷</button>
            <button type="button" className="pcg-agent__header-button" title="Collapse Agent panel" aria-label="Collapse Agent panel" onClick={() => setCollapsed(true)}>‹</button>
          </div>

          {!connected && <div className="pcg-agent__connection-notice"><span>{connectionError || 'Connect an AI Provider in PCG Settings to start.'}</span><button type="button" onClick={onOpenSettings}>Open Settings</button></div>}
          {connected && connectionError && <div className="pcg-agent__connection-notice"><span>{connectionError}</span><button type="button" onClick={() => setConnectionError('')}>Dismiss</button></div>}

          {showHistory ? (
        <AgentHistory
          sessions={sessions} activeSessionId={sessionId} query={historyQuery} loading={historyLoading}
          hasMore={historyCursor > 0} onLoadMore={() => void loadOlderHistory()}
          onQueryChange={setHistoryQuery} onOpen={(id) => void openSession(id)} onClose={() => setShowHistory(false)}
          onRename={(session) => {
            const title = window.prompt('Rename chat', session.title);
            if (title?.trim()) void renameAgentSession(session.id, title).then(() => refreshHistory('')).catch((error) => setConnectionError(errorMessage(error)));
          }}
          onDelete={(session) => {
            if (!window.confirm(`Delete “${session.title}”? This cannot be undone.`)) return;
            void deleteAgentSession(session.id).then(() => { if (session.id === sessionId) startNewChat(); return refreshHistory(''); }).catch((error) => setConnectionError(errorMessage(error)));
          }}
        />
          ) : (
        <AgentMessageList
          messages={messages} pendingCalls={pendingCalls} decisions={decisions}
          onDecision={(callId, decision) => setDecisions((stored) => ({ ...stored, [callId]: decision }))}
          onContinue={() => void submitDecisions()}
          onRetry={(turnId) => void retryTurn(turnId)}
          showReasoning={showReasoning}
        />
          )}

          <AgentComposer
        sending={sending} agentLabel={connected ? `${selectedProvider?.name} · ${selectedModel?.name}` : 'Connect Provider'}
        disabled={!connected || pendingCalls.length > 0 || showHistory} onSend={sendTurn} onStop={handleStop}
        providers={connectedProviders} providerId={providerId} modelId={modelId} reasoningEffort={reasoningEffort}
        onModelChange={(nextProvider, nextModel) => void selectModel(nextProvider, nextModel)}
        onReasoningEffortChange={(effort) => void selectReasoningEffort(effort)}
          />
          <div className="pcg-agent__resize-handle" onMouseDown={onResizeStart} title="Drag to resize" />
        </>
      )}
    </div>
  );
}
