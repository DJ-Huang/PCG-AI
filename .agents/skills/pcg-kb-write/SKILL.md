---
name: pcg-kb-write
description: Safely capture or distill PICG engineering knowledge into the project-local `.picg/kb/` BM25 knowledge base. Use for quick capture, reusable pitfalls, concepts, tips, research notes, or inbox distillation.
---

# PCG Knowledge Base Write

Write only to the project-local `.picg/kb/` knowledge base. Never hard-code an absolute repository path. Before writing, call `pcg_kb_status` to confirm the index root and state. Use `pcg_kb_search`, `pcg_kb_get`, and `pcg_kb_list` for discovery. After a successful write, call `pcg_kb_reindex` and verify retrieval with `pcg_kb_search`.

Project-specific knowledge does not go to the Obsidian Vault. Suggest the separate `obsidian-write` skill only when the result is genuinely reusable across projects.

## Modes

### Quick Capture

Use for fragments, reminders, or the user's original wording. Write to `kb/inbox/inb-{YYYYMMDD-HHmmss}.md` without generalizing the content. The `id` must match the filename.

```markdown
---
id: inb-{YYYYMMDD-HHmmss}
createdTime: YYYY-MM-DDTHH:mm:ssZ
tags: [type/inbox, project/picg]
---

# Inbox

<original content>
```

### Distill

Use when the user asks for reusable project knowledge.

1. Confirm the knowledge-base root with `pcg_kb_status`.
2. Search by mechanism, platform, and note type. Merge with an existing note when one already covers the topic.
3. Decide whether the content remains true across features and model types. Feature-specific timelines, temporary workarounds, and unverified conclusions belong in the inbox or a feature work log.
4. Route the note:
   - `kb/pitfalls/pit-*`: reproducible failure, root cause, remediation, and validation;
   - `kb/concepts/cpt-*`: reusable mechanism, architecture, or pattern;
   - `kb/tips/tip-*`: short, actionable guidance;
   - `kb/research/`: dated research and decision evidence;
   - `kb/inbox/inb-*`: unprocessed fragments.
5. Follow the frontmatter of an existing note of the same type. Pitfalls require `id`, `name`, `severity`, `rootCauseType`, `techStack`, `tags`, `verified_status`, `verified_by`, `verified_date`, and a `## Validation` section. Concepts and tips require at least `id`, `name`, `tags`, and `verified_status`.
6. Preview the destination, identifier, frontmatter summary, and reusable mechanism when the user did not already specify them.
7. Merge rather than silently overwrite. Preserve valid existing material and update the verification date when evidence changed.
8. Read the written file back, reindex, and confirm that the new or updated note is retrievable.

Identifiers use a type prefix and a kebab-case mechanism name. Replace path-unsafe punctuation with hyphens and append `-2`, `-3`, and so on for genuine collisions.

### Distill Inbox

List `kb/inbox/inb-*.md`, summarize each item, and propose create, merge, keep, or delete. Delete an inbox source only after the destination note has been written, validated, reindexed, and retrieved successfully.

## Evidence policy

`verified_status` describes confidence, not severity:

- `unverified`: no reproducible evidence or validation method;
- `limited`: supported in a specific project, platform, or version boundary;
- `verified_true`: supported by reproducible evidence with a stated scope.

Never turn an assumption into `verified_true`. High-severity pitfalls require executable reproduction or validation steps and a pass criterion. Preserve platform, version, device, and measurement boundaries.

## Routing boundary

- PICG graph, native, server, Web editor, Unity integration, and project workflow knowledge belongs in `.picg/kb/`.
- Cross-project Unity editor issues, collaboration practices, and general platform capabilities may be proposed for `obsidian-write`.
- When a topic is useful in both places, write the project-local note first and ask before creating a separate global note. Never silently duplicate it.

## Completion checklist

- [ ] `pcg_kb_status` confirmed the project-local root.
- [ ] Distillation passed the reuse test and searched for duplicates.
- [ ] The path, identifier, and frontmatter match the selected note type.
- [ ] Evidence scope and validation are explicit.
- [ ] Existing notes were merged rather than silently replaced.
- [ ] The final file was read back.
- [ ] `pcg_kb_reindex` completed.
- [ ] `pcg_kb_search` retrieves the new or updated note.
