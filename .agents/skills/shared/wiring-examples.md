# Wiring patterns

These are topology sketches, not serialized graphs or authoritative property presets. Query current node types and pin schemas before applying one. For full reviewed assets, use project Golden Graphs; do not infer production readiness from a short example.

## Part and assembly

A simple part can follow `CreateBoxMesh -> shape operations -> BevelMesh -> AssignMaterial -> Output`. Choose the actual shape operations and topology needed by the brief; subdivision and bevel are not mandatory just because they appear in a sample. For multiple parts, finish suitable bevels per part before `MergeMesh`, then output the assembly. Merge is not a Boolean/fuse.

## Spline-based construction

A bridge-like construction can use one path to drive a `SweepAlongSpline` deck and `InstanceAlongSpline` supports. Supply a real cross-section to the sweep's profile input and a finished prototype to the instancer's mesh input, using the manifest's exact handle IDs. Derive spacing, support heights, profile shape and orientation from the specification rather than copying arbitrary sample dimensions.

## Subgraph interface

A root instance uses `type: "Subgraph"` with `data.subgraphId` pointing to a definition in root `subgraphs[]`. Declared port IDs must match the instance edges and internal `SubgraphInput` / `SubgraphOutput` edges. A small transform wrapper can illustrate I/O, but production modules should represent meaningful independently editable or repeated components.

Lay each scope out top-down with readable lanes and unique titles. These examples do not bypass the [graph contract](graph-authoring.md), current schema checks or target-platform validation.
