import { describe, expect, it } from 'vitest';
import { isLocalEditorRequest, proxyAuthorization } from './localProxy';

describe('local editor proxy', () => {
  for (const host of ['localhost:5173', '127.0.0.1:5173', '[::1]:5173']) {
    it(`accepts local CLI and same-origin requests at ${host}`, () => {
      expect(isLocalEditorRequest({ host })).toBe(true);
      expect(isLocalEditorRequest({ host, origin: `http://${host}` })).toBe(true);
      expect(isLocalEditorRequest({ host, origin: `http://${host}`, 'sec-fetch-site': 'same-origin' })).toBe(true);
      expect(isLocalEditorRequest({ host, 'sec-fetch-site': 'none' })).toBe(true);
    });
  }

  it.each([
    {},
    { host: ['localhost:5173'] },
    { host: 'evil.example:5173' },
    { host: 'user@localhost:5173' },
    { host: 'localhost:5173/path' },
    { host: 'localhost:5173?x=1' },
    { host: 'localhost:5173#x' },
    { host: 'localhost:5173', origin: 'null' },
    { host: 'localhost:5173', origin: 'https://evil.example' },
    { host: 'localhost:5173', origin: 'http://localhost:9999' },
    { host: 'localhost:5173', origin: 'http://localhost:5173/path' },
    { host: 'localhost:5173', origin: 'file://localhost:5173' },
    { host: 'localhost:5173', origin: ['http://localhost:5173'] },
    { host: 'localhost:5173', 'sec-fetch-site': 'cross-site' },
    { host: 'localhost:5173', 'sec-fetch-site': 'same-site' },
  ])('rejects foreign or malformed request headers: %j', (headers) => {
    expect(isLocalEditorRequest(headers)).toBe(false);
  });

  it('does not invent credentials when server authentication is disabled', () => {
    expect(proxyAuthorization(undefined, {})).toBeUndefined();
  });

  it('uses the generic server token and supports its migration alias', () => {
    expect(proxyAuthorization(undefined, { PCG_SERVER_TOKEN: 'current' })).toBe('Bearer current');
    expect(proxyAuthorization(undefined, { PCG_AGENT_TOKEN: 'legacy' })).toBe('Bearer legacy');
    expect(proxyAuthorization(undefined, { PCG_SERVER_TOKEN: 'new', PCG_AGENT_TOKEN: 'old' })).toBe('Bearer new');
  });

  it('does not replace an explicit invalid token with privileged server credentials', () => {
    expect(proxyAuthorization('Bearer invalid', { PCG_SERVER_TOKEN: 'new' })).toBe('Bearer invalid');
  });
});
