# Maintainer Workflows

PICG is designed for reviewable collaboration between maintainers and coding agents. The repository keeps the graph contract, native execution path, host integrations, validation rules, and agent instructions together so a change can be inspected and verified end to end.

## Codex and agent-assisted maintenance

Maintainers use the repository instructions and project-local skills for work such as:

- inspecting a live graph and its manifest-backed node contracts;
- authoring or repairing editable `.pcg` graphs through the MCP server;
- tracing behavior across `schema`, `pcg-core`, `pcg-server`, Web, and Unity;
- running focused tests and repository contract checks;
- reviewing changes for generated artifacts, local paths, credential leaks, and missing asset provenance;
- preparing release notes and compatibility documentation.

Agent output is treated as a proposed change, not as acceptance evidence. Native tests, Web tests, schema validation, Unity compilation, graph cooking, and visual review remain distinct checks.

## Pull request review

Every pull request should identify the affected contract and provide validation that matches the change:

- native behavior: build and focused CTest coverage;
- Web behavior: lint, production build, and Vitest coverage;
- schema or library behavior: repository validation scripts and synchronized generated copies;
- Unity behavior: editor compilation, relevant editor tests, and sample review;
- graph or asset behavior: validate, cook, capture, and inspect the requested result.

Generated output, private paths, credentials, and raw review dumps must not be committed.

## Release preparation

Before a release, maintainers should:

1. Verify that the version is consistent across `VERSION`, CMake projects, and the Web package.
2. Run all CI jobs from a clean checkout with submodules initialized.
3. Confirm that every distributed media asset has recorded provenance and redistribution rights.
4. Review security-sensitive localhost, filesystem, and credential boundaries.
5. Update `CHANGELOG.md`, compatibility notes, and the release tag.
