---
id: pit-pcg-merge-coplanar-angle-deg
name: Merge-coplanar thresholds are expressed in degrees
severity: medium
rootCauseType: angular unit mismatch
tags: [type/pitfall, area/geometry]
verified_status: limited
verified_by: "PICG merge-coplanar regression"
---

# Keep coplanar tolerances in degrees

The public merge-coplanar property is an angle in degrees. Convert once at the algorithm boundary and compare normalized face normals with a documented tolerance. Do not compare degree values directly with radian-space functions, and do not reuse a positional epsilon as an angular threshold.
