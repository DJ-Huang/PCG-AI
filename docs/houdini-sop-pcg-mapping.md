# Houdini SOP to PICG Mapping

PICG uses familiar procedural modelling concepts, but its nodes are not binary-compatible implementations of Houdini SOPs. The current [Node Reference](node-reference.md) and `schema/node-manifest.json` define PICG behavior.

## Common mappings

| Houdini concept | PICG node or pattern |
| --- | --- |
| Box / Grid / Tube / Torus | `CreateBoxMesh`, `CreateGridMesh`, `CreateCylinderMesh`, `Torus` |
| Transform / Mirror / Bend | `TransformMesh`, `MirrorMesh`, `Bend` |
| Boolean / Bevel / Subdivide | `BooleanMesh`, `BevelMesh`, `SubdivideMesh` |
| Curve / Spiral / Resample | `CreateSpline`, `CreateSpiralSpline`, `ResampleSpline` |
| Sweep / Loft / Revolve | `SweepAlongSpline`, `LoftMesh`, `RevolveMesh` |
| Group / Group Expression | `GroupCreate`, `GroupCombine`, and group-aware node properties |
| Attribute Wrangle | `AttributeWrangle` with PICG's restricted deterministic DSL |
| Scatter / Copy to Points | surface or spline sampling followed by `CopyMeshToPoints` or a compatible spawner |
| Material SOP | chained `AssignMaterial` nodes |
| HeightField operators | the `HeightField*` node family |
| Null / output marker | `Output` |

## Important differences

- Node names are only a navigation aid; properties, domains, and edge semantics come from the manifest.
- PICG uses metres and typed pins across Web, Unity, server, and native execution.
- The wrangle language is intentionally restricted and does not execute arbitrary Python or JavaScript.
- Materials are graph data until a host binds them to engine assets.
- Similar UI does not imply every Houdini mode or legacy behavior is supported.

When porting a network, translate the data flow and attribute domains first, then choose manifest-backed nodes. Validate and cook after each coherent module rather than assuming one-to-one semantic parity from a label.
