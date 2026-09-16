# PCG Skills Index

These skills cover PCG graph authoring and complete-asset delivery. Shared scripts and asset contracts live under `shared/` and are referenced by relative path rather than installed as independent skills.

## Unity

| Skill | Typical triggers | Purpose |
| --- | --- | --- |
| [pcg-graph-authoring-unity](./pcg-graph-authoring-unity/SKILL.md) | `/pcg-graph-authoring-unity`, `.pcg`, PCG graph authoring, Houdini layout, Scene view review | Author a PCG graph and require clean-scene Unity validation and visual review. The legacy `pcg-graph-authoring` name redirects here. |
| [pcg-graph-authoring-unity-dev](./pcg-graph-authoring-unity-dev/SKILL.md) | `/pcg-graph-authoring-unity-dev`, pipeline validation, capability assessment | Adds the Pipeline Capability Gate to the Unity workflow and stops only for a verified capability gap. |

## Web

| Skill | Typical triggers | Purpose |
| --- | --- | --- |
| [pcg-graph-authoring-web](./pcg-graph-authoring-web/SKILL.md) | `/pcg-graph-authoring-web`, Web PCG authoring, three-view reconstruction, WebGL review | Author through the live Web editor and require Vite, `pcg-server`, cooking, capture, and complete-asset acceptance. |
| [pcg-graph-authoring-web-dev](./pcg-graph-authoring-web-dev/SKILL.md) | `/pcg-graph-authoring-web-dev`, Web pipeline validation, three-view reconstruction | Adds generator structure, seed, boundary, regeneration, performance, and capability-gap validation. |

## Compatibility redirect

| Skill | Purpose |
| --- | --- |
| [pcg-graph-authoring](./pcg-graph-authoring/SKILL.md) | Redirects the legacy name to `pcg-graph-authoring-unity`. |

## Shared resources

| Path | Purpose |
| --- | --- |
| [shared/pcg-scripts/](./shared/pcg-scripts/) | Python orchestration for plans, validation, layout, comparison sheets, and `.pcgr` parsing. |
| [shared/pcg-unity-complete-asset/](./shared/pcg-unity-complete-asset/) | Unity complete-asset workflow and acceptance contracts. |
| [shared/pcg-web-complete-asset/](./shared/pcg-web-complete-asset/) | Web complete-asset workflow and acceptance contracts. |

## Source of truth

`.agents/skills/` is the source of truth inside the PICG repository. Relative `SHARED_DIR` and `SHARED_SCRIPTS_DIR` references must continue to resolve from each skill. Never edit installed or linked copies as a second source.
