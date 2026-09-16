---
recipe_id: pcg.recipe:blockout:placed-box
version: 1
roles: [wall, floor, ceiling, table, door, prop]
root_node_types: [TransformMesh]
---

# Placed Box

Use a mesh source followed by `TransformMesh` for blockout surfaces and rigid props. The transform node is the semantic owner; `memberNodeIds` contains the source and transform.

Safe edits are the source dimensions and the owner's `translate`, `rotation`, and `scale`. Coordinates use Unity world space with +Y up; dimensions are meters and rotation is Euler degrees.

Keep the geometry output connected to `TransformMesh.in`, and keep the transform as the final component output. Validate positive dimensions, unique component ownership, explicit bounds for camera framing, and a successful cook.

Common failures are duplicate member ownership, bounds that were not updated after resizing, and applying placement to both the source and transform.
