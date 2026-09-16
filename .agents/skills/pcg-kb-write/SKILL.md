---
name: pcg-kb-write
description: Capture or distill PICG knowledge into .picg/kb/ when asked to save a note, document a reusable pitfall, or process the project inbox.
---

# Write project knowledge

Write to this repository's `.picg/kb/`, not a global Vault or a hard-coded checkout path. Resolve the root with `pcg_kb_status` when available, otherwise inspect the repository. These MCP tools retrieve/index notes; use the available file-editing tool to write Markdown.

## Choose the requested operation

**Capture:** preserve the user's wording in `kb/inbox/inb-YYYYMMDD-HHmmss.md`. Use an actual UTC timestamp, matching `id`, `createdTime` with `Z`, and `tags: [type/inbox, project/picg]`. Avoid collisions; do not generalize unverified fragments.

**Distill:** search for an existing mechanism with `pcg_kb_search` / `pcg_kb_get` or repository search, then merge rather than duplicate. Route reproducible failures to `pitfalls/pit-*`, mechanisms to `concepts/cpt-*`, short guidance to `tips/tip-*`, dated investigations to `research/`, and unprocessed material to `inbox/` (all under `kb/`). Follow an existing note's type-specific frontmatter.

Pitfalls require `id`, `name`, `severity`, `rootCauseType`, `techStack`, `tags`, `verified_status`, `verified_by`, `verified_date` and a `## Validation` section. Concepts/tips require at least `id`, `name`, `tags` and `verified_status`. Use stable kebab-case identifiers and preserve valid existing evidence.

**Process inbox:** propose or carry out the requested create/merge/keep operations. Delete source notes only when deletion was authorized and their material is durably preserved and checked. A successful distillation alone is not permission to delete originals.

## Evidence and completion

Use `unverified` for unsupported claims, `limited` for evidence with restricted scope, and `verified_true` only for reproducible evidence with its platform/version boundaries. High-severity pitfalls need reproduction or validation steps and a pass criterion. Do not convert an assumption into a rule.

Read back changed notes, then reindex once per completed batch with `pcg_kb_reindex` and verify a relevant search hit. If the index service is unavailable, distinguish a saved note from unverified indexing; retain inbox sources. Report the written paths and verification state. Ask before publishing separate cross-project copies or making a destructive merge.
