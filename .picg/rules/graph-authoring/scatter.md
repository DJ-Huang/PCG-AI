---
domain: pcg
intents: author_graph
rag_index: true
rule_id: pcg/scatter
tags: [type/rule, domain/pcg, project/picg]
type: rule
verified_status: limited
verified_by: "PICG scatter examples and current manifest"
---

# Scatter authoring strategy

Choose the chain by data type: generate points, filter or mask them, project to a compatible surface when needed, then place a real prototype with a manifest-compatible spawner or copy node.

Expose seed, density or spacing, scale range, and prototype style as graph parameters when they are meaningful controls. Reuse prototypes rather than expanding identical geometry chains.

## Orientation

For prototypes with a defined front, write orientation before instancing. Use `rotationY` or `orient`, or a frame with `N` as up and `tangent` as forward. Large random rotation does not satisfy path-facing behavior.

## Ground contact

Centered box and cylinder prototypes need `TransformMesh.translateY = height / 2` before `CopyMeshToPoints`. Applying the translation after instancing moves the whole population and does not fix each prototype origin.

## Surface sampling

When sampling a lot or top surface, select the intended face group and rebuild that group after topology operations that discard it. `edgeMargin` is measured from the selected region boundary; without a face group, walls and cuts can remain eligible. Keep the margin below the usable half-width so the requested count can be satisfied.
