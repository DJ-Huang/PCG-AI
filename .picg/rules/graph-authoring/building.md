---
domain: pcg
intents: author_graph
rag_index: true
rule_id: pcg/building
tags: [type/rule, domain/pcg, project/picg]
type: rule
verified_status: limited
verified_by: "PICG building and lot-city examples"
---

# Building authoring strategy

## Structure

Separate massing, facade modules, openings, roof, foundation, access, and materials into named chains or subgraphs. Prefer repeated modules for windows, columns, floors, and roof elements. Complete topology, bevel, UV, and material work per part before the final building merge.

Use real-world metres. Record the owner of width, height, depth, floor height, opening dimensions, and roof dimensions so a measurement is not independently controlled by several nodes.

## Openings and facade

- Door and window bounds on the same facade must not overlap.
- Reserve a door clearance region before distributing window columns.
- Windows, frames, and glazing need non-zero depth or a documented planar treatment.
- Repeated facade elements must stay inside the wall bounds after parameter changes.

## Lot and road placement

Buildings may not overlap the combined road footprint. Filter placement points before instancing and keep `LotSubdivision.minSize` larger than the largest building footprint, adding point relaxation when necessary.

An oriented prototype must face its primary access route. For placement point `P`, find the closest point on the selected road or path in the horizontal plane and derive a facing vector toward it. Use the correct frame property: `rotationY` or `orient`, or `N` as up with `tangent` as forward. `CopyMeshToPoints` maps `N` to local Y, so a horizontal access vector must not be written to `N` alone.

Large random yaw is not a substitute for access-facing orientation. Small variation may be added only after the primary direction is established.

## Ground contact and spacing

Centered primitive prototypes need a pre-instance vertical offset of half their height so their base rests at local Y=0. Instance horizontal bounds may not overlap. Verify corner lots against the selected primary route and inspect both top-down and street-level views.

## Acceptance

- The silhouette and dimensions match the intended reference or design brief.
- Openings do not overlap and remain inside their facade.
- Buildings avoid roads, do not intersect neighbours, and rest on the ground.
- Primary facades face the selected access route.
- Material slots appear on the intended components.
