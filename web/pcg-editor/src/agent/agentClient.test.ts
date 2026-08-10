import { beforeEach, describe, expect, it, vi } from 'vitest';

import { connectApiKey, getProviders, setAgentToken, startTurn, type AgentStreamEvent } from './agentClient';

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
        controller.enqueue(encoder.encode('event: turn.completed\ndata: {"turnId":"turn-1"}\n\n'));
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
      reasoningEffort: 'max', attachments: [{ id: 'a1', file, previewUrl: null }],
    }, (event) => events.push(event));

    expect(events.map((event) => event.type)).toEqual(['turn.created', 'message.delta', 'turn.completed']);
    const request = fetchMock.mock.calls[0][1] as RequestInit;
    expect(request.body).toBeInstanceOf(FormData);
    const uploaded = (request.body as FormData).get('attachment') as File;
    const requestFile = (request.body as FormData).get('request') as Blob;
    const requestText = await new Promise<string>((resolve, reject) => {
      const reader = new FileReader();
      reader.onload = () => resolve(String(reader.result));
      reader.onerror = () => reject(reader.error);
      reader.readAsText(requestFile);
    });
    const payload = JSON.parse(requestText);
    expect(payload.reasoningEffort).toBe('max');
    expect(uploaded.name).toBe('graph.pcg');
    expect(uploaded.size).toBe(file.size);
  });

  it('surfaces an SSE connection that ends without a turn terminal event', async () => {
    const encoder = new TextEncoder();
    const stream = new ReadableStream<Uint8Array>({
      start(controller) {
        controller.enqueue(encoder.encode('event: turn.created\ndata: {"turnId":"turn-cut"}\n\n'));
        controller.enqueue(encoder.encode('event: reasoning.delta\ndata: {"turnId":"turn-cut","text":"unfinished"}\n\n'));
        controller.close();
      },
    });
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue(new Response(stream, {
      status: 200, headers: { 'Content-Type': 'text/event-stream' },
    })));
    const events: AgentStreamEvent[] = [];

    await startTurn({
      message: 'inspect', sessionId: 'session-cut', providerId: 'kimi-coding', modelId: 'k3', attachments: [],
    }, (event) => events.push(event));

    expect(events.at(-1)).toMatchObject({
      type: 'turn.error',
      data: { turnId: 'turn-cut', error: { code: 'agent_stream_interrupted', retryable: true } },
    });
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

  it('migrates cached Kimi K3 metadata to expose supported effort levels', async () => {
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue(new Response(JSON.stringify({ providers: [{
      id: 'kimi-coding', name: 'Kimi for Coding', authMethods: [], baseUrl: '',
      connection: { status: 'connected', authType: 'api', accountLabel: '', error: null },
      models: [{ id: 'k3', name: 'K3', capabilities: {
        toolCall: true, textInput: true, imageInput: true, reasoning: true,
        contextTokens: 0, outputTokens: 0,
      } }],
    }] }), { status: 200, headers: { 'Content-Type': 'application/json' } })));

    const providers = await getProviders();

    expect(providers[0].models[0].capabilities.reasoningEfforts).toEqual(['low', 'high', 'max']);
    expect(providers[0].models[0].capabilities.defaultReasoningEffort).toBe('high');
  });
});
