---
id: pit-pcg-assign-material-binding
name: Material definitions do not bind themselves to geometry
severity: high
rootCauseType: disconnected data flow
techStack: [PICG, pcg-server, Three-js, PBR]
tags: [type/pitfall, area/pcg, area/material]
verified_status: limited
verified_by: "wooden-cabin graph material-slot and preview checks"
verified_date: "2026-08-11"
---

# Connect every material explicitly

A `Material` node only produces material data. Geometry uses it only when `Material.out` is connected to the matching `AssignMaterial.material` input.

Create and bind materials within the responsible component chain, such as walls, roof, foundation, and glazing. After cooking, verify the expected slot names and texture paths in the output, reload the graph, and inspect the material preview. An unconnected material node is not evidence of a successful binding.
