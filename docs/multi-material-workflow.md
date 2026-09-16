# Multi-Material Workflow

PICG stores material assignment per polygon face. Face groups select regions, `AssignMaterial` writes a stable material name, and a later assignment overrides an earlier one on the same face.

## Recommended graph

```text
final topology
  -> AssignMaterial(group="", materialName="body_paint")
  -> AssignMaterial(group="windows", materialName="glass")
  -> AssignMaterial(group="tires", materialName="rubber")
  -> Output
```

Apply a base material first, then narrower overrides. Perform boolean, subdivision, and bevel operations before final assignment so newly created faces receive an intentional material. `MergeMesh` preserves the material tables of its inputs.

## Unity binding

The graph stores names, not Unity object references. In `PcgGraphComponent`, add a Material Binding for each name and select the project `Material` asset. Unbound names use the component's fallback mesh material instead of dropping the submesh.

## Data path

```text
geometry face materials
  -> sink triangulation and stable material slots
  -> mesh binary material section
  -> Unity submeshes or Web PBR materials
```

When group-driven assignment is required, keep the pipeline in editable geometry form until the final sink. Mesh-only upstream data has no polygon face-group contract. Validate graph wiring, cook-result slots, and rendered host output before accepting the material pass.
