# Tripo YOYO Puppy — Procedural AssetSpec

## Intent and evidence boundary

Reconstruct the single-subject Tripo result as an editable Web PCG graph with
near-pixel alignment. The source GLB is used only during the measurement bake;
the delivered graph must not contain `ImportMesh`, `Tripo3DGenerator`, source
faces, or source indices.

- Admitted source image: `examples/showcases/tripo-yoyo-puppy/reference.png`
- Tripo reference GLB:
  `library/web-cache/TripoCache/dc92d764f741471cc68d46ac6ed34d3c3b2c49704db58d6748173b9a5d830fea.glb`
- Source GLB SHA-256:
  `1cf05bd61c35ff7a5de75e31e38c4bbfa1c5e6d6fc40f2e6895fa75583a7dd89`
- Source geometry: 28,822 vertices, 47,299 triangles, one textured material.
- Source rig state: one fused mesh/node, zero skins, zero animations; component
  names and bone placement are therefore explicit reconstruction hypotheses.
- Measured source bounds: about `0.8467 × 1.0000 × 0.9672 m`, Y-up.

The earlier primitive dog was rejected: recognizable semantics alone are not
sufficient. The required evidence is fixed-camera pixel agreement with the GLB.

## Procedural representation

The graph contains five nodes and four edges:

1. `puppy_sdf / OrientedSdfSurface` decodes 112,025 quantized oriented
   measurements and reconstructs a new sparse MLS-SDF.
2. Surface Nets creates new quad topology at `cellSize=0.003 m` with a
   `2.5-cell` support radius and zero iso offset.
3. The nearest orientation-compatible source measurement transfers the source
   normal, UV, and baked linear base colour to every reconstructed vertex.
4. `puppy_material → puppy_assign` binds a map-free material; `puppy_action_rig`
   adds a schema-v2 component tree, derived skeleton, skinning and clips without
   changing the reconstructed rest geometry.
5. `puppy_output` emits the complete action-ready asset.

The representation is procedural because `cellSize`, support radius, iso offset,
transfer switches, semantic regions, component hierarchy, bind mode and clips
remain editable and every Cook regenerates topology from measurements. It is
not a compressed copy of the source triangle mesh.

## Component, rig and action contract

`ActionRig` uses one schema-v2 `componentTree` as the source of truth for ten
named components and ten derived bones: body, head, muzzle, left/right ear,
tail, left/right front leg and left/right rear leg. Body is the only root;
every other component references a parent. Sphere/capsule semantic regions and
ownership priorities make triangle assignment explicit and deterministic.

- Smooth structural regions use solid geodesic binding at resolution 48 with
  at most four influences and falloff 8.
- Muzzle (`detail`) and ears (`hair`) are rigidly bound to protect thin fur and
  facial detail from interpolation stretch.
- `splitComponents=true` partitions all triangles into ten mutually exclusive
  visual meshes. They share one skeleton and use root-direct identity binding,
  so no triangle is duplicated and component transforms do not double-apply.
- Clips: looping two-second `idle` (head/tail) and 1.2-second `walk` (four legs
  plus tail), using quaternion tracks.
- Runtime metadata also includes `collar_socket`, `tail_tip`, body/head
  colliders, and `head_assembly` / `limbs` destruction groups.

## Appearance contract

- Embedded Tripo base colour is sampled into the oriented point field with
  bilinear filtering and sRGB-to-linear conversion.
- GLB V is converted to Canvas coordinates with `canvasY = 1 - sourceV`.
- UV texture maps are disabled on the reconstructed material because new
  Surface Nets triangles may span unrelated atlas islands.
- Source normals are transferred as authored point `N`; scalar material factors
  are metallic `0`, roughness `0.85`, opacity `1`.
- The exported asset carries colour in glTF `COLOR_0`, so it has no external
  image dependency.

## Parameters and determinism

| Parameter | Default | Range / role |
| --- | ---: | --- |
| `Surface Detail (m)` | 0.003 | 0.0025–0.008; grid resolution |
| `SDF Support Radius` | 2.5 | 1.5–4; reconstruction support |
| `Surface Offset (m)` | 0 | -0.01–0.01; dilation/erosion |
| `Transfer UVs` | true | preserves measured UV for downstream experiments |
| `Skin Binding` | geodesic | solid-volume distance prevents cross-limb leakage |
| `Geodesic Resolution` | 48 | skin-distance voxel resolution |
| `Build Separable Components` | true | ten one-owner visual partitions |
| `Autoplay Clip` | walk | preview default; review/export also expose idle |

The reconstruction and Hammersley triangle-interior measurement sequence are
deterministic. Acceptance uses seed 42.

## Shared browser capture profile

- Resolution: `1024×1024`, DPR 1.
- Projection: orthographic; frustum height `1.25 m`.
- Target `[0,0,0]`, distance `3 m`, elevation `10°`, up `[0,1,0]`.
- Azimuths: `0°, +35°, -35°, 90°, 145°, 180°`.
- Lens metadata: 50 mm, 24 mm sensor; f/8; exposure 1; DOF off.

## Acceptance contract

1. Every required view has silhouette IoU at least `0.98`.
2. Every required view has symmetric boundary-distance P95 at most `2 px`.
3. RGB error is reported independently; target mean is below `0.03`.
4. Live validate/cook/capture succeed after the final save.
5. Graph layout is Houdini-style top-down.
6. Cook emits one connected recognizable puppy with head, muzzle, ears, torso,
   four legs/paws, curled tail, fur tufts, black/white markings, and yellow harness.
7. Final Web GLB exports and re-imports without the Tripo cache.
8. Bind pose contains ten visible `SkinnedMesh` instances, every one bound to
   the shared skeleton; sampled positions and weights must stay finite.
9. Exported glTF must contain one shared skin, ten bones, ten visual meshes, ten
   logical components, and both animation clips; playing a re-imported clip
   must change the targeted bone quaternion.

## Accepted result

- Live Cook (`seed=42`): 891,047 vertices / 1,779,556 triangles;
  5,338,668 indices, 71,254,063 response bytes.
- Final action-ready six-view silhouette: mean IoU `0.99546`, worst IoU
  `0.99436`.
- Final six-view boundary: worst P95 `2.000 px` (acceptance limit `2 px`).
- Final six-view mean normalized RGB error: `0.02150`.
- Bind diagnostics: ten visible meshes, ten visible `SkinnedMesh` instances,
  all bound; 151,403 rigid vertices; zero unreachable and zero non-finite
  samples; maximum weight-sum error `4.84e-8`; rest-position delta `2.35e-8`.
- Animated export: 85,531,188 bytes, SHA-256
  `a26e72b04d429781c01374645a399acd0d6d9f4845521578e8c9ac9f709d6430`.
- Export re-import: one shared skin, ten `SkinnedMesh` visuals, ten bones, ten
  logical components, `idle` + `walk`; sampled weights remain normalized and
  `walk` changes the `front_l` quaternion.

Status: `FINAL_ACCEPTED`.
