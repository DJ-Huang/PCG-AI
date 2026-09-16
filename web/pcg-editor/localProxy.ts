/** Server-only guard for the localhost editor proxy. Never import secrets into browser code. */
export function isLocalEditorRequest(
  headers: Readonly<Record<string, string | string[] | undefined>>,
): boolean {
  if (typeof headers.host !== 'string') return false;
  const fetchSite = headers['sec-fetch-site'];
  if (fetchSite !== undefined && fetchSite !== 'same-origin' && fetchSite !== 'none') return false;
  try {
    const target = new URL(`http://${headers.host}`);
    if (!['localhost', '127.0.0.1', '[::1]'].includes(target.hostname)) return false;
    if (target.username || target.password || target.pathname !== '/' || target.search || target.hash) return false;
    const origin = headers.origin;
    // CLI clients have no Origin. Browser requests must originate at this editor.
    if (origin === undefined) return true;
    if (typeof origin !== 'string') return false;
    const source = new URL(origin);
    return (source.protocol === 'http:' || source.protocol === 'https:')
      && source.origin === origin
      && source.host === target.host;
  } catch {
    return false;
  }
}

/** Explicit request credentials win; an invalid bearer token is never silently replaced. */
export function proxyAuthorization(
  authorization: string | undefined,
  environment: Readonly<Record<string, string | undefined>>,
): string | undefined {
  if (authorization) return authorization;
  // Compatibility alias only; there is no embedded chat runtime.
  const token = environment.PCG_SERVER_TOKEN || environment.PCG_AGENT_TOKEN;
  return token ? `Bearer ${token}` : undefined;
}
