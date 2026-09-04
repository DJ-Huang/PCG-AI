# Security Policy

## Supported versions

PCG-AI is in active development. Security fixes are applied to the latest branch only until versioned releases are published.

## Reporting a vulnerability

Do not open a public issue for a vulnerability that exposes credentials, enables arbitrary file access, bypasses graph-write approval or affects the localhost trust boundary.

Use GitHub private vulnerability reporting when it is enabled for the repository. If no private channel is available, open a minimal public issue asking the maintainers for a private contact method without including exploit details or secrets.

Include the affected commit, platform, reproduction conditions, impact and any suggested mitigation. Remove API keys, access tokens, personal paths and user data from screenshots or logs.

## Localhost boundary

`pcg-server` is designed for local authoring. Keep it bound to loopback unless you have added authentication and reviewed the deployment boundary. Set `PCG_AGENT_TOKEN` when another local process should not have unauthenticated access to the Agent/MCP routes.

Provider credentials must be entered through the local Settings UI or supported environment variables. Never add them to graphs, examples, source files, issue reports or commits.

