---
domain: pcg
intents: author_graph,write_code
rag_index: true
rule_id: pcg/graph-contract
tags: [type/rule, domain/pcg, project/picg]
type: rule
verified_status: verified_true
verified_by: "Current manifest, graph parser, validators, and editors"
---

# PCG graph authoring contract

## Before writing

1. Read the live editor context and current graph.
2. Query the current manifest before using any node type, property, default, range, pin ID, or pin compatibility.
3. Load this rule, `pcg/assembly-bevel`, and the applicable object rule through the project knowledge tools.
4. Use project Golden Graph tools for reviewed references. Demo and example graphs are not strategy authorities.

## Graph structure

- Use graph version `1.0` with `nodes`, `edges`, and `parameters`; add `subgraphs` only when needed.
- Give every node and edge a unique semantic ID. Put a short unique title in `data.__nodeTitle`.
- Handles must exactly match manifest pin IDs and types. A runnable graph ends in `Output`.
- Repeated complete components or graphs approaching roughly 40 root nodes should become meaningful subgraphs. Avoid tiny wrappers and unrelated mega-subgraphs.

## Layout

Use Houdini-style top-down layout: the main chain shares an X coordinate, vertical rows are about 160 units apart, and siblings on one row are at least 320 units apart. Long cross-lane diagonals and left-to-right processing columns fail layout review.

After every structural write, run the repository layout pass, push the positions back to the live editor, and save the named graph.

## Parameters and scale

PICG units are metres. Use plausible physical dimensions unless the brief explicitly requests stylization. A root parameter must target a real property on a real root node, and its default must match the node value. Each dimension has one owner.

## Scene semantics

- Roads must remain visible at or above the ground surface.
- Buildings and other placed assets must avoid the full road footprint.
- Prototypes with a front or travel axis must face the intended access path; identity rotation is not an acceptable default.
- Doors and windows on one facade may not overlap.
- Neighbouring placed asset footprints may not intersect.
- Material data must be wired through `AssignMaterial`; engine asset creation is a separate decision.

## Validation

Use live graph operations rather than writing `.pcg` JSON directly. After coherent writes, save and run validation, cook, and preview capture. A syntax validator does not prove visual acceptance. Finish only when the graph is top-down, live checks pass, and the complete-asset acceptance state is `FINAL_ACCEPTED` or a documented legal stop applies.
