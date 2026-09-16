# Working in PICG

PICG combines a native procedural graph runtime, a Web editor, and Unity integration. Make the requested change; do not turn a documentation fix, graph tweak, or white-model request into an unrelated full-asset production job.

## GitHub communication

Write all pull request and issue titles, descriptions, and follow-up comments in English for this repository. Preserve code identifiers, paths, logs, and quoted source text where exact wording matters. Chat replies may follow the user's language.

## Find the relevant contract

- Graph creation or editing: select one platform through the [skills index](.agents/skills/index.md). A complete asset, reference reconstruction, and generator testing are different scopes; load their references only when needed.
- Native, server, editor, or Unity code changes: consult `.picg/rules/engineering/picg-development.md` and the affected subsystem's code/tests. These tasks do not require a live graph session merely to edit files.
- Project knowledge capture: use [pcg-kb-write](.agents/skills/pcg-kb-write/SKILL.md).

`.agents/skills/` is the repository source of truth. Use relative references, not installed copies under a particular editor's home directory. Skill entry points define task scope; shared contracts define mechanics. Older examples and recorded plans do not expand the user's request.

## Project evidence

Use the current node manifest and implementation for node types, pins, properties, and runtime behavior. For live graph work, follow the [MCP contract](.agents/skills/shared/pcg-mcp.md); saved schemas are useful for offline inspection, not proof of a running server's capabilities.

Project rules and experience live in `.picg/`: retrieve relevant notes with `pcg_kb_search` / `pcg_kb_get`, and reviewed templates with `pcg_golden_graph_list` / `pcg_golden_graph_get`. Read the same repository files directly when those tools are unavailable. Search for a specific mechanism or missing fact rather than loading the entire knowledge base. Demo graphs can explain an existing asset, but are not authoritative authoring recipes.

Do not use global `rule_search(domain="pcg")` or unscoped global rules for PICG. Global Vault material is optional cross-project background, not a source of PICG policy; ignore unrelated project rules. Repository contracts and current implementation take precedence over external examples. Report a material conflict rather than silently inventing behavior.

## Execution boundaries

Preserve unrelated files, editor documents, scene changes, and generated assets. Confirm the target from available context before a destructive operation; ask only when that context cannot resolve consequential ambiguity. Continue safe, relevant edits and local verification without asking whether to continue after each step.

Choose checks that can detect regressions caused by the change. Graph validation, cooking, target-platform rendering, and exported-asset reload test different things; none substitutes for the others. Report which checks ran, which failed or were unavailable, and any remaining limitation. Never label an unobserved visual result or unrun test as passing.
