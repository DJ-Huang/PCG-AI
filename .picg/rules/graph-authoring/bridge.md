---
domain: pcg
intents: author_graph
rag_index: true
rule_id: pcg/bridge
tags: [type/rule, domain/pcg, project/picg]
type: rule
verified_status: limited
verified_by: "PICG bridge examples and current manifest"
---

# Bridge authoring strategy

Model the bridge as named systems: deck, supports, rails or cables, anchors, and optional approach geometry. Use splines for span paths, sweeps for constant sections, and instancing for repeated supports or hangers.

- Keep the deck cross-section and travel clearance explicit.
- Orient repeated parts from the path frame rather than world axes.
- Build and bevel each system before the final merge.
- Expose span, deck width, support spacing, and structural style as high-value parameters.
- Check scale, support contact, railing continuity, and underside clearance in more than one view.

Node selection and pin compatibility must come from the current manifest.
