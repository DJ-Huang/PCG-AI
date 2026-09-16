---
domain: pcg
intents: author_graph
rag_index: true
rule_id: pcg/vehicle
tags: [type/rule, domain/pcg, project/picg]
type: rule
verified_status: limited
verified_by: "PICG vehicle examples and current manifest"
---

# Vehicle authoring strategy

Choose geometry by component shape. Use loft or profile/backbone sweeps for bodies with changing longitudinal sections, boxes for genuinely rectangular bumpers and bed walls, cylinders or circular sweeps for wheels, and validated boolean cutters for openings.

Build body, wheels, glazing, doors, and lights as named modules. Finish each module's topology, bevel, UVs, and material before the vehicle merge. Reuse complete modules for symmetric or repeated parts.

Use plausible metre scale and record stylized deviations. A typical passenger car is approximately 4-5 m long, 1.8-2.0 m wide, 1.4-1.5 m high, with a 0.6-0.7 m wheel diameter. Treat these as sanity ranges, not reference measurements.
