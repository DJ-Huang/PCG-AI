import { beforeEach, describe, expect, it, vi } from 'vitest';

import { connectApiKey, setAgentToken, startTurn, type AgentStreamEvent } from './agentClient';

describe('agentClient', () => {
  beforeEach(() => {
    localStorage.clear();
  });

  it('parses SSE split across network chunks and uploads real attachments', async () => {
    const encoder = new TextEncoder();
    const stream = new ReadableStream<Uint8Array>({
      start(controller) {
        controller.enqueue(encoder.encode('event: turn.created\ndata: {"turnId":"turn-1"}\n'));
        controller.enqueue(encoder.encode('\nevent: message.delta\ndata: {"turnId":"turn-1","text":"hello"}\n\n'));
        controller.close();
      },
    });
    const fetchMock = vi.fn().mockResolvedValue(new Response(stream, {
      status: 200,
      headers: { 'Content-Type': 'text/event-stream' },
    }));
    vi.stubGlobal('fetch', fetchMock);
    const events: AgentStreamEvent[] = [];
    const file = new File(['node data'], 'graph.pcg', { type: 'application/json' });

    await startTurn({
      message: 'inspect', sessionId: 'session-1', providerId: 'openai', modelId: 'gpt-test',
      attachments: [{ id: 'a1', file, previewUrl: null }],
    }, (event) => events.push(event));

    expect(events.map((event) => event.type)).toEqual(['turn.created', 'message.delta']);
    const request = fetchMock.mock.calls[0][1] as RequestInit;
    expect(request.body).toBeInstanceOf(FormData);
    const uploaded = (request.body as FormData).get('attachment') as File;
    expect(uploaded.name).toBe('graph.pcg');
    expect(uploaded.size).toBe(file.size);
  });

  it('sends a Provider key once without persisting it in browser storage', async () => {
    const fetchMock = vi.fn().mockResolvedValue(new Response('{"ok":true}', {
      status: 200,
      headers: { 'Content-Type': 'application/json' },
    }));
    vi.stubGlobal('fetch', fetchMock);
    setAgentToken('local-bridge-token');

    await connectApiKey('openai', 'provider-secret');

    expect(localStorage.getItem('pcg-agent-token')).toBe('local-bridge-token');
    expect(JSON.stringify(localStorage)).not.toContain('provider-secret');
    const request = fetchMock.mock.calls[0][1] as RequestInit;
    expect(request.headers).toMatchObject({ Authorization: 'Bearer local-bridge-token' });
    expect(request.body).toContain('provider-secret');
  });
});
