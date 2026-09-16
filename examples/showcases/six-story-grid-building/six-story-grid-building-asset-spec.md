# AssetSpec — six-story-grid-building

| Field | Value |
|---|---|
| assetId | `six-story-grid-building` |
| intent | Six-story grid office/retail building from archived front/side/top drawings, for web cook/review and later glTF export. |
| references | `ref_six-story-grid-building_{front,side,top}.png` (facts: 24×21×16 m, ground 5 m, typical 4 m, 4 bays). Perspective is integrity-only. |
| scale | width 24 m, height 21 m, depth 16 m, frontAxis `+z` |
| modules | podium, upper mass, roof+penthouse, front frame, upper window, storefront, side window, entrance |
| geometryDoD | Box matches 24×21×16; dark 5 m podium; 4-bay storefronts; 4×4 upper 3-pane windows; side window column; rear-center roof bulkhead |
| materialSlots | `bldg_podium`, `bldg_upper`, `bldg_frame`, `bldg_glass`, `bldg_roof` (Standard PBR, bound via AssignMaterial) |
| variation | `BayCount` 2–8, `TypicalFloorCount` 1–8, `BaySpacing` 4–8 m |
| outputs | The procedural graph was removed; reference and evidence images remain for record. |
| acceptance | worst required ortho view ≥ 0.9 before FINAL_ACCEPTED |
