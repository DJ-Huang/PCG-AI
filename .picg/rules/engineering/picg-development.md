---
domain: pcg
intents: write_code,fix_bug,refactor
rag_index: true
rule_id: pcg/project-engineering
tags: [type/rule, domain/pcg, project/picg, area/engineering]
type: rule
verified_status: verified_true
verified_by: "Repository architecture, build scripts, and test suites"
---

# PICG engineering rules

## Authoritative contracts

- `schema/node-manifest.json` is the source of truth for node types, pins, properties, defaults, and ranges.
- Keep generated manifest copies synchronized with the repository scripts; do not hand-edit copies.
- The native core owns graph execution and geometry algorithms. Web and Unity are consumers of the same contracts.
- Unity uses the local HTTP `pcg-server`; do not reintroduce an in-process native plugin path.

## Change workflow

1. Locate the source contract and all consumers before editing.
2. Add or update the smallest relevant regression test.
3. Build the affected target and run focused tests.
4. Run repository contract validators.
5. When editor state or rendering is part of the claim, verify in the actual Web or Unity client.

Do not treat compilation as visual or runtime acceptance. Do not commit build directories, logs, caches, credentials, generated captures, or machine-specific paths.

## Node changes

Adding or changing a node normally requires aligned updates to the native registration, manifest, serializer/parser behavior, generated reference documentation, and tests. Pin IDs and value types are compatibility boundaries. Changes to them require explicit migration handling.

## Runtime deployment

After native backend changes, rebuild and restart `pcg-server`, run its health check, and cook a representative graph. Passing core tests alone does not update a running server.
