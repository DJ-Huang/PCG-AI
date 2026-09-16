# Security Policy

## Supported versions

PICG is in active development. Security fixes are applied to the latest branch only until versioned releases are published.

## Reporting a vulnerability

Do not open a public issue for a vulnerability that exposes credentials, enables arbitrary file access, bypasses graph-write validation or authentication, or affects the localhost trust boundary.

Use GitHub private vulnerability reporting when it is enabled for the repository. If no private channel is available, open a minimal public issue asking the maintainers for a private contact method without including exploit details or secrets.

Include the affected commit, platform, reproduction conditions, impact and any suggested mitigation. Remove API keys, access tokens, personal paths and user data from screenshots or logs.

## Localhost boundary

`pcg-server` is designed for local authoring. Keep it bound to loopback unless you have added authentication and reviewed the deployment boundary. Set `PCG_SERVER_TOKEN` when another local process should not have unauthenticated access to the editor-bridge, Tripo, or MCP routes. This token does not turn the entire development stack into a hardened remote service.

Start Vite and `pcg-server` with the same token environment. The server-side editor proxy supplies it for accepted localhost editor requests; external MCP clients must send a matching bearer token. Never expose the token through browser build variables or commit it to client configuration. See [the server authentication guide](docs/pcg-server.md#localhost-authentication).

3D generation credentials such as Tripo API keys must be entered through **Settings → 3D Generation** or supported server environment variables. AI model credentials are configured in the external MCP client, not PICG. Never add credentials to graphs, examples, source files, issue reports or commits.
