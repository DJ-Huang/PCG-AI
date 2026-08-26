```text
Geometry: cook=PASS | bounds≈24 x 23.6 x 16.4 m | silhouette=FAIL |
worstView=top | assembly=PASS | normals=PASS | scale=PASS
```

Cook: pcg-server `/v1/cook` seed 42, non-empty mesh, 5 material slots.

Scale: drawing metres (24 / 21 / 16). Roof bulkhead adds ~2.4 m above 21 m (expected).

Silhouette: front grid and side column are recognizable. Ground storefronts do not read as four glass bays. Top is a solid roof with penthouse, not the plan’s core/entrance recess. Worst required view = top.

Assembly: per-part bevel then merge; boolean podium openings are intentional.

Front axis: `+z`.
