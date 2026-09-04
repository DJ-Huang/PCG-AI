# pcg-server

`pcg-server` is the localhost HTTP/MCP host for `pcg-core`. It provides graph validation and cooking, editor synchronization, preview capture, the embedded Agent runtime and optional third-party generation services.

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

