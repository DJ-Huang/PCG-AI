---
id: tip-pcg-material-binding-acceptance
name: Material binding needs data, engine, and visual evidence
category: Tip
tags: [type/tip, area/pcg, area/material]
verified_status: limited
verified_by: "PICG web and Unity material workflows"
---

# Validate material binding at three layers

1. **Graph:** `Material.out` reaches the intended `AssignMaterial.material` pin.
2. **Cook result:** the mesh contains the expected material slot and texture references.
3. **Consumer:** Web or Unity resolves the binding and renders it on the intended faces.

A valid graph alone does not prove rendering, and a default-looking preview does not prove the material is absent. Record all three checks when accepting a complete asset.
