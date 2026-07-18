# Host Terrain Surface Contract

`HostTerrainSurface` is the engine-neutral boundary between a typed PCG
`PcgHeightField` and a host terrain system. Unity implements the P0 adapter with
`TerrainData`; a future Unreal adapter can implement the same field contract for
Landscape without introducing Unity or Unreal APIs into `pcg-core`.

## Stamp overlay (Unity Editor)

Terrain Host keeps **HeightField → Output** as the primary result. Stamp volumes
used by `HeightFieldMaskByObject` are drawn as Scene View wire overlays and edited
with Transform handles (`PcgStampOverlaySceneHandles`). Overlay edits write
`TransformMesh` node data; recook follows `Stamp Live Cook` (Manual / OnRelease /
WhileDragging). Node Preview of Mesh under Terrain Host is overlay-only and does
not clear or rewrite `TerrainData`.

## Data contract

| Field | Meaning | P0 rule |
|---|---|---|
| `resolutionX`, `resolutionZ` | Shared layer grid dimensions | Both are at least 2 |
| `sizeX`, `sizeZ` | Grid footprint in graph-space units | Finite and positive |
| `centerX/Y/Z` | Grid base-plane center in graph space | Height is displacement from the center plane |
| `sampling` | `center` or `corner` | Unity Terrain uses `corner` |
| `orientation` | `ZX`, `XY`, or `YZ` | Unity Terrain P0 accepts `ZX` |
| named layers | Stable name, tuple size, border mode/value, row-major floats | `height` and `mask` are required scalar layers |

Layer indexing is `((z * resolutionX) + x) * tupleSize + component`. All layers
share the same transform. Layer names are stable data identifiers, not localized
Inspector labels.

The native v9 transport uses `PcgHeightFieldSlot` for host input and a versioned
HeightField binary sidecar for output. ABI v1-v8 signatures are unchanged. The
sidecar preserves every named layer, including vector layers such as `flowdir`;
when the first output buffer is too small, v9 reports the required size and the
Unity caller retries with an exact-size buffer. P0 Unity writes only `height` and
retains the remaining layers for later adapters.

## Adapter interface

An adapter exposes two operations:

```text
ImportSurface(host terrain) -> HostTerrainSurface
ExportSurface(HostTerrainSurface) -> host terrain + apply report
```

The apply report must disclose resolution resampling, footprint scaling, height
clamping, and whether the target was already unchanged. Host APIs belong in the
adapter layer. `pcg-core` receives only the typed grid DTO.

The Unity implementation is `PcgUnityTerrainAdapter`. An Unreal implementation
should keep the same DTO and implement an `UnrealLandscapeAdapter`; no UE source
or dependency is part of P0.

## Unity binding behavior

`PcgGraphComponent` chooses a primary host output with `Host Output`:

| Mode | Primary result | Host object |
|---|---|---|
| `Mesh` | MeshFilter / MeshRenderer (points/splines also) | Mesh Bindings; optional legacy Terrain import list only when not on a Terrain |
| `Terrain` | This object's `TerrainData` | Put `PcgGraphComponent` on the Terrain GameObject — no Terrain Binding |

For Terrain host mode, wire **HeightField → Output** (no `ConvertHeightField`).
Cook writes heights into the Terrain on the same GameObject.

`GetTerrainData` prefers the Terrain on the component's GameObject (Self). A legacy
`TerrainBindings` list remains only as a Mesh-mode override when the component is
not attached to a Terrain.

## Synchronization and coordinate rules

- External Terrain height content participates in the whole-graph managed cache
  key and the native per-node `GetTerrainData` hash.
- Async results retain the component cook generation rule: stale workers never
  publish a Terrain result.
- Export compares desired normalized heights with the current TerrainData using
  one Terrain 16-bit quantization step. An unchanged target skips `SetHeights`,
  preventing redundant dirty events and feedback churn.
- Resolution mismatch is handled by explicit bilinear resampling and reported;
  samples are never cropped silently. Unity keeps the target Terrain resolution.
- Footprint mismatch is scaled to the bound Terrain footprint and reported.
- Values outside Unity's normalized height range are clamped and counted in the
  apply report.
- P0 requires the Terrain axes to be aligned with the graph component. Arbitrary
  rotated grids require a later transform extension rather than an implicit warp.

## Delivery phases

| Phase | Payload | Status |
|---|---|---|
| P0 | Bidirectional height, required mask, binding UX, cache dirty/generation guard, `Host Output` Mesh\|Terrain | Implemented |
| P1 | Unity alphamap/splat ↔ named scalar layers | Reserved |
| P2 | Tree/detail instances and stable TerrainLayer texture-name bindings | Reserved |
| Future | Unreal Landscape adapter | Interface/DTO only |

P1 layer names and P2 asset names must remain stable strings in graph/Core data.
Unity or Unreal asset references stay in their host binding tables, following the
same separation as Material Bindings.
