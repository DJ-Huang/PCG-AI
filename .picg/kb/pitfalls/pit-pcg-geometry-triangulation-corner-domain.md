---
id: pit-pcg-geometry-triangulation-corner-domain
name: PCGG triangulation indices use the corner domain
severity: high
rootCauseType: binary format domain mismatch
techStack: [PCG, pcg-server, TypeScript, Three-js]
tags: [type/pitfall, area/binary-protocol]
verified_status: limited
verified_by: "Web parser fixture and browser rendering"
verified_date: "2026-08-07"
---

# Render triangles from PCGM, not PCGG point data

The `TRIANGULATION` chunk in a PCGG geometry block indexes face-corner vertices produced for flat shading. It does not index the shared `POINTS` chunk. A box therefore has eight shared points but triangulation indices that reach the 24-corner domain.

Use the PCGM mesh blob for triangle rendering because it includes the expanded positions, indices, normals, colours, and UVs. Use PCGG for shared points, face loops, and edge visualization. Validate index bounds against the appropriate domain and keep a golden box fixture in parser tests.
