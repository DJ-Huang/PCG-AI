# Asset and Third-Party Licensing

The repository's future software license will not automatically grant rights to every image, model, texture, scene or generated output. Confirm that each redistributed media asset is owned by the project or covered by a compatible license before publishing a release.

## Documented sources

| Content | Location | Terms |
| --- | --- | --- |
| Poly Haven HDR environments | `web/pcg-editor/public/environments/polyhaven/` | CC0 1.0; see the included `LICENSE.md` |
| CDT source | `pcg-core/third_party/CDT/` | See the licenses included with the submodule |
| Assimp used by the FBX exporter | Downloaded during native configuration | See `pcg-fbx-exporter/THIRD_PARTY_NOTICES.md` |

## Maintainer review required before public release

- Reference images and evidence under `examples/showcases/`.
- Generated models and source images under `Unity/Assets/Samples/PCG-AI/Showcases/`.
- Texture sets copied into `assets/`, the Web public directory and Unity resources.

For each item, record its author/source, applicable license or service terms, and whether redistribution is permitted. Remove anything whose provenance cannot be verified. Large local showcase exports under `examples/**/artifacts/` are ignored and are not part of the repository.
