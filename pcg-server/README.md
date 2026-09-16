# pcg-server

`pcg-server` is the localhost HTTP/MCP host for `pcg-core`. It provides graph validation and cooking, editor synchronization, preview capture, protected 3D generation credentials and optional third-party generation services.

External MCP clients can operate the live Web graph through the server. AI model accounts and conversations belong to those clients; Web Settings configures 3D generation APIs such as Tripo.

## Run

```bash
./scripts/build-pcg-server.sh
./scripts/run-pcg-server.sh
```

Verify:

```bash
curl http://127.0.0.1:17890/v1/health
./scripts/verify-pcg-server.sh
```

The server binds to loopback by default. See [docs/pcg-server.md](../docs/pcg-server.md) for endpoints, authentication, credentials and MCP behavior.
