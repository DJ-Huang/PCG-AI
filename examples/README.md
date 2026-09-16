# Examples

This directory is the single repository-level home for PCG graph examples and case-study outputs.

## Layout

| Directory | Purpose | Commit policy |
| --- | --- | --- |
| `graphs/` | Runnable feature and production graphs | Commit reviewed `.pcg` files |
| `tests/` | Small regression fixtures | Keep deterministic and minimal |
| `subgraphs/` | Linked subgraph examples | Commit `.pcgsubgraph` sources |
| `showcases/` | Complete reference-driven cases | Keep final graph, spec, reference and concise evidence |
| `storyboards/` | Sequence/layout blockouts | Keep named, reusable graph files |

Raw captures, temporary comparison sheets, authoring plans, cache files and build exports do not belong here. Large final exports may be kept locally in a showcase `artifacts/` directory; that directory is ignored by Git.

## Recommended starting graphs

| Graph | Demonstrates |
| --- | --- |
| `graphs/stone-arch-bridge.pcg` | Multi-part procedural assembly |
| `graphs/spiral-staircase.pcg` | Spline-driven instancing |
| `graphs/terrain-demo.pcg` | Heightfield workflow |
| `graphs/lot-city-demo.pcg` | Lot subdivision and city assembly |

The `phase*` graphs are compatibility samples for earlier runtime milestones. Files under `tests/` are fixtures rather than polished content.

## Showcases

- `brickify-tool/` — reusable brickification graph and asset specification.
- `tripo-yoyo-puppy/` — third-party reference and procedural reconstruction.
- `wooden-cabin/` — reference-driven cabin graph for Web review.

Unity scenes, materials and import metadata live in `Unity/Assets/Samples/PICG/` because they must stay inside the Unity project. They are indexed in [Unity/README.md](../Unity/README.md).

## Review in the browser

Start the server and Web editor, then use a repository-relative path:

```text
http://127.0.0.1:5173/review?graph=examples/graphs/spiral-staircase.pcg
```

Showcase example:

```text
http://127.0.0.1:5173/review?graph=examples/showcases/wooden-cabin/wooden-cabin.pcg
```

## Adding an example

1. Put the final file in the narrowest matching directory.
2. Use top-down graph layout and manifest-backed node/pin names.
3. Remove local absolute paths, API keys, cache references and redundant captures.
4. Add a short README or asset spec for a multi-file showcase.
5. Run the graph validator, cook it, capture final evidence and keep only the evidence needed to understand the result.
