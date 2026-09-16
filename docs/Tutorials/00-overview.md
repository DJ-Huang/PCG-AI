# System Overview

PICG separates authoring from execution. The Web editor and Unity serialize the same graph contract, `pcg-server` provides the local process boundary, and `pcg-core` performs validation and execution.

```text
Web or Unity -> graph JSON -> pcg-server -> pcg-core -> cook result -> preview/export
```

Start with [Getting Started](../getting-started.md), run the repository validators, and cook one example in the Web editor. Then repeat the same graph in Unity with **PCG > Server > Health Check**. This proves the shared contract more effectively than inspecting either client alone.

Important sources of truth:

- node contract: `schema/node-manifest.json`
- native behavior: `pcg-core` and its tests
- reusable built-ins: `library/`
- project rules and operational knowledge: `.picg/`
