# PICG skill architecture audit

Issue: #14. Baseline: `0661fb5da41ee8e72fde9db3ce7586fbc0f2dbd0`.

## Assessment

The six original skill entries covered four responsibilities but duplicated Web/Unity authoring into standard and development variants. The main problem was not a lack of instructions: overlapping triggers and repeated mandatory workflows expanded small edits into complete asset jobs. Root instructions leaked live-editor requirements into unrelated code/documentation work; the generic command selected an installed Cursor/Unity skill instead of a repository-relative target.

The audit covered every skill entry, root routing, the major shared authoring/production and reference-review documents, command references, and the knowledge-rule entries that could reintroduce old scope constraints. Selected contracts were checked against server redaction behavior and helper/template interfaces. This is not a line-by-line audit of the entire native runtime or an end-to-end model benchmark.

## Changes and retained boundaries

- Remove both development-suffixed PCG skills and their duplicated gate documents. Keep generator reliability tests as an on-demand shared reference.
- Keep four entries: Web authoring, Unity authoring, a generic platform router and project knowledge writing. Add the previously omitted KB skill to the index.
- Keep descriptions specific and brief. Entry points route to shared graph/MCP contracts, image evidence, generator testing and complete-asset delivery only when relevant.
- Consolidate duplicated orchestration, script instructions, graph rules, wiring examples, acceptance and error routing. Preserve platform-specific geometry, UV/material/texture and integration references.
- Replace mandatory three-view collection after any image with evidence-driven single-image or supplied-view work. Remove universal iteration quotas and subjective score gates from ordinary tasks; existing scored specifications remain binding.
- Preserve manifest-backed pins/properties, graph/hash safety, readable layout, part-level beveling, parameter synchronization, evidence scope and independent target-platform acceptance.
- Add the server's redacted-payload caveat: targeted patches preserve omitted data; a redacted snapshot must not become a full replacement document.
- Protect Unity review scene changes and existing files; use fresh `.unity` paths. Keep source-graph Web review distinct from actual exported-asset reload evidence.
- Keep existing structured planning helpers compatible and optional. Their strict schema and recorded pass thresholds were not silently relaxed.

## Behavioral review cases

These are expected decision boundaries for human/model evaluation, not claims of executed model trials.

| Request/context | Expected behavior |
| --- | --- |
| Fix a README typo | Relevant file edit/check; no editor startup, cook or AssetSpec. |
| Change one Web node value | Targeted schema/state inspection, hash-protected patch, applicable checks and save; no prefab/export pipeline. |
| Reconstruct a crate from one concept image | Use that image, record hidden-surface assumptions; do not demand three additional uploads. |
| Match supplied front/side/top drawings | Preserve sources/frame and verify each required view; no averaging away a failed view. |
| Deliver a materialized Unity prefab | Correct Unity workspace, protected review scene, actual prefab reload and target evidence. |
| Stress-test a seeded generator | Select relevant seed/boundary/regeneration/performance checks, restore final state, report evidence. |
| Graph read redacts payloads or a write times out | Preserve omitted data; inspect current state before another write. |
| Distill a project pitfall while indexing is offline | Save/check the note if possible; report indexing unverified and preserve inbox originals. |

## Regression checks

Python 3.10+; no runtime, browser or network needed for linter unit tests:

```bash
python3 -m unittest discover -s scripts/tests -p 'test_validate_skills.py' -v
python3 scripts/validate-skills.py --json
```

The linter checks live instruction Markdown for retired PCG references, editor-home paths, broken/escaping local links, entry/index consistency, description length (300 characters) and entry size (4096 UTF-8 bytes). These are maintainability budgets, not token counts or model quality metrics. It supports the simple flat/folded frontmatter used here, not arbitrary YAML or every Markdown extension.

During the chat implementation, 17 disposable-fixture tests and Python byte-compilation passed. The container could not resolve public GitHub hosts, so a full checkout and full-repository lint/native test run were not available. GitHub-side tree/ref and diff review are separate checks. The Unity template was reviewed against the API, not compiled or executed in Unity; Web cooking/rendering/export and model A/B trials were not performed. Do not interpret the unit-test result as those validations.

## Rationale and sources

The design follows the [OpenAI guidance on revisiting skills and prompts for Astra](https://developers.openai.com/blog/rethinking-skills-and-prompts-for-gpt-6-astra): narrow triggers, task-relevant references and less prescribed scaffolding. That motivates this architecture; it does not prove a success-rate or latency improvement in PICG.

Repository evidence includes [MCP server payload redaction](../pcg-server/src/mcp_service.cpp) and the [Unity review template](../.agents/skills/pcg-graph-authoring-unity/scripts/unity/create_review_scene.cs.txt). Unity's [SaveScene API](https://docs.unity3d.com/ScriptReference/SceneManagement.EditorSceneManager.SaveScene.html) documents project-relative `.unity` paths and save-result handling.

No CI workflow, application Agent feature, runtime node implementation or installed global skill copy is changed by this refactor. The branch is intended for review before merging.
