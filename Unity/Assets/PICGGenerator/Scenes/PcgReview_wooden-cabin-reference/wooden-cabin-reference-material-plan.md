# Wooden Cabin Reference — material plan

The graph uses five stable material names: `cabin_wood`, `cabin_roof`, `cabin_stone`, `cabin_door`, and `cabin_glass`. All use the project URP Lit shader and are bound on the persistent `PcgGraphComponent`.

| Slot | Owners | Intent | Mapping | Source |
|---|---|---|---|---|
| `cabin_wood` | walls, siding, porch, posts, gables | warm matte timber with horizontal grain | planar Y/Z-aligned UV projection; repeat at cabin scale | `Textures/T_cabin_wood_albedo.png` |
| `cabin_roof` | main roof planes and porch roof | dark grey shingle pattern, rough dielectric | planar roof projection; repeat along slope | `Textures/T_cabin_roof_albedo.png` |
| `cabin_stone` | foundation and chimney | cool grey masonry, rough dielectric | planar/cylindrical projection by part | `Textures/T_cabin_stone_albedo.png` |
| `cabin_door` | door panels, trim, fascia, sills | darker warm wood, semi-matte | planar projection | `Textures/T_cabin_door_albedo.png` |
| `cabin_glass` | window panes | smoky blue-grey, high smoothness | planar projection | `Textures/T_cabin_glass_albedo.png` |

Flat colour remains the fallback if a texture import is unavailable; no visible renderer is left on Unity Default-Material or a missing shader. Review lighting is the clean-scene directional studio light plus the dedicated camera.
