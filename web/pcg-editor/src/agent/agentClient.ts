// Typed client for the embedded pcg-server Agent runtime. Provider secrets are
// submitted once and stored by the server in its protected credential store;
// they are never persisted in the browser or returned by these APIs.

import type { AgentAttachment } from './AgentComposer';

const TOKEN_KEY = 'pcg-agent-token';

export interface AgentError {
  code: string;
  message: string;
  retryable: boolean;
}

export interface ModelCapabilities {
  toolCall: boolean;
  textInput: boolean;
  imageInput: boolean;
  reasoning?: boolean;
  contextTokens: number;
  outputTokens: number;
}

export interface ModelDescriptor {
  id: string;
  name: string;
  capabilities: ModelCapabilities;
}

export interface AuthMethodDescriptor {
  type: 'api' | 'oauth';
  label: string;
  flow?: 'browser' | 'device';
  available: boolean;
  unavailableReason?: string;
}

export interface ProviderDescriptor {
  id: string;
  name: string;
  authMethods: AuthMethodDescriptor[];
  connection: {
    status: 'connected' | 'invalid' | 'expired' | 'unavailable';
    authType: string;
    accountLabel: string;
    error: AgentError | null;
  };
  models: ModelDescriptor[];
  baseUrl: string;
}

export interface AgentSettings {
  providerId: string;
  modelId: string;
}

export interface OAuthStart {
  attemptId: string;
  providerId: string;
  url: string;
  userCode: string;
  expiresAt: number;
  intervalSeconds: number;
}

export interface ToolCallEvent {
  turnId: string;
  toolCallId: string;
  name: string;
  arguments: Record<string, unknown>;
  requiresApproval?: boolean;
  partId?: string;
  messageId?: string;
  ordinal?: number;
}

export type AgentPartStatus = 'streaming' | 'running' | 'approval_required' | 'completed' | 'failed' | 'error';

export interface AgentPart {
  id: string;
  type: 'text' | 'reasoning' | 'tool' | 'attachment' | 'error';
  ordinal: number;
  status?: AgentPartStatus;
  text?: string;
  name?: string;
  mimeType?: string;
  size?: number;
  toolCallId?: string;
  arguments?: Record<string, unknown>;
  result?: unknown;
  cached?: boolean;
  durationMs?: number;
  error?: AgentError;
}

export interface AgentMessageRecord {
  id: string;
  role: 'user' | 'assistant';
  turnId?: string;
  status: 'running' | 'awaiting_approval' | 'completed' | 'interrupted' | 'error';
  createdAt: number;
  parts: AgentPart[];
}

export interface AgentSessionDescriptor {
  id: string;
  title: string;
  createdAt: number;
  updatedAt: number;
  providerId: string;
  modelId: string;
  graphName: string;
  status: 'running' | 'awaiting_approval' | 'completed' | 'interrupted' | 'error' | 'idle';
}

export interface AgentSession extends AgentSessionDescriptor {
  messages: AgentMessageRecord[];
}

export interface AgentStreamEvent {
  type:
    | 'turn.created'
    | 'reasoning.started'
    | 'reasoning.delta'
    | 'reasoning.completed'
    | 'message.started'
    | 'message.delta'
    | 'message.completed'
    | 'tool.call'
    | 'tool.result'
    | 'approval.required'
    | 'turn.completed'
    | 'turn.error';
  data: Record<string, unknown>;
}

export interface TurnDecision {
  toolCallId: string;
  decision: 'approve' | 'reject';
}

export function getAgentToken(): string {
  return localStorage.getItem(TOKEN_KEY) ?? '';
}

export function setAgentToken(token: string): void {
  if (token) localStorage.setItem(TOKEN_KEY, token);
  else localStorage.removeItem(TOKEN_KEY);
}

function authHeaders(jsonBody = false): Record<string, string> {
  const headers: Record<string, string> = {};
  if (jsonBody) headers['Content-Type'] = 'application/json';
  const token = getAgentToken();
  if (token) headers.Authorization = `Bearer ${token}`;
  return headers;
}

async function readError(response: Response): Promise<never> {
  const raw = await response.text();
  let parsed: { error?: AgentError | string } | null = null;
  try {
    parsed = JSON.parse(raw) as { error?: AgentError | string };
  } catch {
    // Upstream proxies may return plain text or HTML; the fallback below keeps
    // that diagnostic without trying to distinguish JSON parser messages.
  }
  if (typeof parsed?.error === 'object' && parsed.error?.message) throw new Error(parsed.error.message);
  if (typeof parsed?.error === 'string') throw new Error(parsed.error);
  throw new Error(`HTTP ${response.status}: ${raw || response.statusText}`);
}

async function jsonRequest<T>(path: string, init?: RequestInit): Promise<T> {
  const response = await fetch(`/api/agent${path}`, init);
  if (!response.ok) return readError(response);
  return (await response.json()) as T;
}

export async function getProviders(): Promise<ProviderDescriptor[]> {
  const result = await jsonRequest<{ providers: ProviderDescriptor[] }>('/providers', {
    headers: authHeaders(),
  });
  return result.providers;
}

export async function connectApiKey(providerId: string, apiKey: string, baseUrl?: string): Promise<void> {
  await jsonRequest(`/providers/${encodeURIComponent(providerId)}/connect/key`, {
    method: 'POST',
    headers: authHeaders(true),
    body: JSON.stringify({ apiKey, ...(baseUrl ? { baseUrl } : {}) }),
  });
}

export async function disconnectProvider(providerId: string): Promise<void> {
  await jsonRequest(`/providers/${encodeURIComponent(providerId)}/connection`, {
    method: 'DELETE',
    headers: authHeaders(),
  });
}

export async function validateProvider(providerId: string): Promise<void> {
  await jsonRequest(`/providers/${encodeURIComponent(providerId)}/validate`, {
    method: 'POST',
    headers: authHeaders(true),
    body: '{}',
  });
}

export async function getAgentSettings(): Promise<AgentSettings> {
  return jsonRequest<AgentSettings>('/settings', { headers: authHeaders() });
}

export async function setAgentSettings(settings: AgentSettings): Promise<void> {
  await jsonRequest('/settings', {
    method: 'PUT',
    headers: authHeaders(true),
    body: JSON.stringify(settings),
  });
}

export async function startOAuth(providerId: string): Promise<OAuthStart> {
  return jsonRequest<OAuthStart>(`/providers/${encodeURIComponent(providerId)}/oauth/start`, {
    method: 'POST',
    headers: authHeaders(true),
    body: '{}',
  });
}

export async function getOAuthStatus(attemptId: string): Promise<{ status: string; error?: string }> {
  return jsonRequest(`/oauth/${encodeURIComponent(attemptId)}/status`, { headers: authHeaders() });
}

export async function listAgentSessions(query = '', cursor = 0): Promise<{ sessions: AgentSessionDescriptor[]; nextCursor: number }> {
  const params = new URLSearchParams({ limit: '50' });
  if (query.trim()) params.set('query', query.trim());
  if (cursor > 0) params.set('cursor', String(cursor));
  return jsonRequest(`/sessions?${params}`, { headers: authHeaders() });
}

export async function getAgentSession(sessionId: string): Promise<AgentSession> {
  const response = await jsonRequest<{ session: AgentSession }>(`/sessions/${encodeURIComponent(sessionId)}`, {
    headers: authHeaders(),
  });
  return response.session;
}

export async function renameAgentSession(sessionId: string, title: string): Promise<AgentSessionDescriptor> {
  const response = await jsonRequest<{ session: AgentSessionDescriptor }>(`/sessions/${encodeURIComponent(sessionId)}`, {
    method: 'PATCH', headers: authHeaders(true), body: JSON.stringify({ title }),
  });
  return response.session;
}

export async function deleteAgentSession(sessionId: string): Promise<void> {
  await jsonRequest(`/sessions/${encodeURIComponent(sessionId)}`, { method: 'DELETE', headers: authHeaders() });
}

async function consumeEventStream(response: Response, onEvent: (event: AgentStreamEvent) => void): Promise<void> {
  if (!response.ok) return readError(response);
  if (!response.body) throw new Error('Agent response stream is unavailable');
  const reader = response.body.getReader();
  const decoder = new TextDecoder();
  let buffer = '';

  const flush = (block: string) => {
    let type = '';
    const data: string[] = [];
    for (const line of block.split(/\r?\n/)) {
      if (line.startsWith('event:')) type = line.slice(6).trim();
      if (line.startsWith('data:')) data.push(line.slice(5).trimStart());
    }
    if (!type || data.length === 0) return;
    onEvent({ type: type as AgentStreamEvent['type'], data: JSON.parse(data.join('\n')) as Record<string, unknown> });
  };

  while (true) {
    const chunk = await reader.read();
    buffer += decoder.decode(chunk.value, { stream: !chunk.done });
    let boundary = buffer.search(/\r?\n\r?\n/);
    while (boundary >= 0) {
      const block = buffer.slice(0, boundary);
      const match = buffer.slice(boundary).match(/^\r?\n\r?\n/);
      buffer = buffer.slice(boundary + (match?.[0].length ?? 2));
      flush(block);
      boundary = buffer.search(/\r?\n\r?\n/);
    }
    if (chunk.done) break;
  }
  if (buffer.trim()) flush(buffer);
}

export async function startTurn(
  input: {
    message: string;
    sessionId: string;
    providerId: string;
    modelId: string;
    attachments: AgentAttachment[];
    retryTurnId?: string;
  },
  onEvent: (event: AgentStreamEvent) => void,
  signal?: AbortSignal,
): Promise<void> {
  const form = new FormData();
  form.append(
    'request',
    new Blob(
      [JSON.stringify({
        message: input.message,
        sessionId: input.sessionId,
        providerId: input.providerId,
        modelId: input.modelId,
        ...(input.retryTurnId ? { retryTurnId: input.retryTurnId } : {}),
      })],
      { type: 'application/json' },
    ),
    'request.json',
  );
  for (const attachment of input.attachments) form.append('attachment', attachment.file, attachment.file.name);
  const response = await fetch('/api/agent/turns', {
    method: 'POST',
    headers: authHeaders(),
    body: form,
    signal,
  });
  await consumeEventStream(response, onEvent);
}

export async function decideTurn(
  turnId: string,
  decisions: TurnDecision[],
  onEvent: (event: AgentStreamEvent) => void,
  signal?: AbortSignal,
): Promise<void> {
  const response = await fetch(`/api/agent/turns/${encodeURIComponent(turnId)}/decision`, {
    method: 'POST',
    headers: authHeaders(true),
    body: JSON.stringify({ decisions }),
    signal,
  });
  await consumeEventStream(response, onEvent);
}

export async function cancelTurn(turnId: string): Promise<void> {
  await jsonRequest(`/turns/${encodeURIComponent(turnId)}/cancel`, {
    method: 'POST',
    headers: authHeaders(true),
    body: '{}',
  });
}
