# Wooden Cabin Reference — PCG pipeline receipt

## PCG pipeline

`PASS`

```text
structure=PASS | parameters=N/A | seed=PASS
boundaries=N/A | regeneration=PASS
performance=BASELINE (2769ms/1728ms cold synchronous recook) | outputContract=PASS
demands=8 covered=8 gap=0 inefficient=0
evidence: graph=wooden-cabin-reference.pcg | reviewScene=Assets/Samples/PICG/Showcases/WoodenCabinReference/WoodenCabinReference.scene
```

The graph has 8 root nodes, 7 root edges, 6 complete functional Subgraphs, and 0 static validation warnings/errors. The generator is intentionally reference-locked with no exposed variation parameters; two consecutive same-default recooks produced the same signature:

```text
completed | 23808 vertices | bounds=(6.350, 5.470, 9.380) | 1 MeshFilter | 5 material slots
```

## Geometry receipt

```text
Geometry: cook=PASS | bounds=6.350,5.470,9.380m including porch/chimney |
main-shell=6.0,5.4,7.5m | silhouette=PASS | assembly=PASS |
normals=PASS | scale=PASS | hard-surface=PASS
```

The main shell follows the three-view dimensions. The larger total Z bound is intentional: it includes the front porch projection and stairs. Per-part bevels occur before the final assembly merge; no heterogeneous MergeMesh-to-BevelMesh chain is used.

## Unity integration receipt

```text
materials=PASS | textures=PASS | bindings=5 | prefab=PASS | reload=PASS
```

Persistent outputs are under the asset-specific review folder. The prefab reload test confirmed the graph reference and all five bindings survive prefab instantiation.

## Final acceptance

`REWORK` — not `FINAL_ACCEPTED` under the strict 0.90 reference threshold.

```text
Geometry and silhouette: 0.90 × 0.35
Materials and textures: 0.82 × 0.30
Technical construction: 0.95 × 0.15
Unity integration: 0.92 × 0.10
Final presentation: 0.82 × 0.10
Score: 0.88 (rounded)
```

The structural refinement now matches the supplied shell dimensions, roof pitch, three-view side-window layout, recessed window assemblies, and the split stair/rail opening. Residual gaps are visual rather than pipeline blockers: roof shingle relief and trim variation are simplified, and the lighting/low-poly finish is less photoreal than the reference. The delivered graph is valid and reproducible; further work should be a material/presentation refinement pass if the 0.90 acceptance target is mandatory.
