# PICG skills

Select one authoring platform. Read only the reference needed for the current operation; shared documents are resources, not additional skills to activate.

| Skill | Use when |
| --- | --- |
| [pcg-graph-authoring-web](pcg-graph-authoring-web/SKILL.md) | Creating or editing a graph in the Web editor, or delivering an explicitly requested Web asset. |
| [pcg-graph-authoring-unity](pcg-graph-authoring-unity/SKILL.md) | Authoring a graph for Unity or validating/delivering a Unity asset. |
| [pcg-graph-authoring](pcg-graph-authoring/SKILL.md) | The generic command is used and the authoring platform needs routing. |
| [pcg-kb-write](pcg-kb-write/SKILL.md) | Capturing or distilling knowledge into the project's `.picg/kb/`. |

A node repair ends with the repaired graph and relevant checks. A requested blockout ends with a validated blockout. Only complete-asset requests activate materials, export/prefab, and final-asset acceptance. Generator reliability checks are available in either platform skill without a separate variant.

## Shared references

| Resource | Read for |
| --- | --- |
| [Graph authoring](shared/graph-authoring.md) | PICG document, layout, parameter, and geometry contracts. |
| [Live MCP](shared/pcg-mcp.md) | Session targeting, safe atomic edits, conflicts, validation and saves. |
| [Reference workflow](shared/reference-workflow.md) | Image evidence, assumptions, durable plans and visual iteration. |
| [Generator validation](shared/generator-validation.md) | Determinism, boundary inputs, regeneration and performance tests. |
| [Complete assets](shared/complete-asset-workflow.md) | Stage-specific delivery and evidence for finished assets. |
| [Script reference](shared/script-reference.md) | Optional planning, layout, validation and review helpers. |

`.agents/skills/` is the only maintained source. Keep references repository-relative and keep platform-specific review and integration details in their platform directory.
