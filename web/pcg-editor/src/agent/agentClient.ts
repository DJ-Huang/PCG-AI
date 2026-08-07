// agentClient.ts — HTTP client for the pcg-server /v1/agent/* endpoints
// (through the Vite dev-server /api/agent/* proxy). Bearer token is read from
// localStorage; an empty token means dev mode (server also passes through when
// PCG_AGENT_TOKEN is unset). On network failure the caller gets ok:false and
// falls back to a local echo.

import type { AgentAction } from './agentCommands';

const TOKEN_KEY = 'pcg-agent-token';

export interface AgentAttachmentMeta {
  name: string;
  size: number;
  type: string;
}

export interface AgentChatRequest {
  message: string;
  attachments: AgentAttachmentMeta[];
}

export interface AgentChatResponse {
  ok: boolean;
  reply?: string;
  actions?: AgentAction[];
  error?: string;
}

export function getAgentToken(): string {
  return localStorage.getItem(TOKEN_KEY) ?? '';
}

export function setAgentToken(token: string): void {
  if (token) {
    localStorage.setItem(TOKEN_KEY, token);
  } else {
    localStorage.removeItem(TOKEN_KEY);
  }
}

export async function sendAgentChat(
  request: AgentChatRequest,
  signal?: AbortSignal,
): Promise<AgentChatResponse> {
  try {
    const headers: Record<string, string> = { 'Content-Type': 'application/json' };
    const token = getAgentToken();
    if (token) {
      headers.Authorization = `Bearer ${token}`;
    }
    const res = await fetch('/api/agent/chat', {
      method: 'POST',
      headers,
      body: JSON.stringify(request),
      signal,
    });
    if (!res.ok) {
      const text = await res.text();
      let message = text;
      try {
        const parsed = JSON.parse(text) as { error?: string };
        if (parsed.error) message = parsed.error;
      } catch {
        // non-JSON error body — keep raw text
      }
      return { ok: false, error: `HTTP ${res.status}: ${message}` };
    }
    return (await res.json()) as AgentChatResponse;
  } catch (err) {
    if (err instanceof DOMException && err.name === 'AbortError') {
      return { ok: false, error: 'aborted' };
    }
    return { ok: false, error: String(err) };
  }
}

export function localEchoReply(request: AgentChatRequest, reason: string): AgentChatResponse {
  const attachNote =
    request.attachments.length > 0 ? ` (+${request.attachments.length} attachment(s))` : '';
  return {
    ok: true,
    reply: `local echo${attachNote}: ${request.message} — pcg-server agent unreachable (${reason})`,
  };
}
