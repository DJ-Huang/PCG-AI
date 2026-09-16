# Third-Party Image-to-3D Services

PICG integrates cloud generators such as Meshy and Tripo as explicit authoring actions. Node properties are defined by `schema/node-manifest.json`; provider APIs and prices can change independently.

## Security and cost boundaries

- API keys stay in protected local settings or environment variables and never enter graph files, logs, examples, or Git.
- Preview, parameter changes, and ordinary cook operations never start a paid cloud job.
- Only an explicit **Generate** action contacts a provider.
- Successful results are cached locally and may be saved as user-owned project assets.
- Native cooking reads a local result path and remains deterministic after generation.

Web credentials are managed under **Settings > 3D Generation** through `pcg-server`. Unity credentials are stored in local editor preferences. Use provider-specific environment variables for CI or managed machines.

## Typical workflow

1. Add the provider node and choose a clean single-subject image or URL.
2. Configure model, texture, remesh, scale, and axis options.
3. Click **Generate** and wait for the provider job to finish.
4. Save or retain the downloaded result in an ignored local cache.
5. Cook the graph; downstream nodes consume the local mesh.

Do not commit provider cache files or source images without confirmed redistribution rights. A generated model is not automatically licensed for open-source redistribution; follow the provider terms and include the relevant provenance in the pull request.

## Proceduralization and rig handling

An unrigged generated GLB can be treated as a measurement reference for a new procedural surface, semantic component plan, and optional rig. The resulting graph must retain editable parameters and pass deterministic multi-view review. It must not claim that unknown source components or skin weights were recovered with certainty.

If a self-contained GLB already has a valid rig and animation, use the preservation path so joints, inverse-bind matrices, weights, and animation channels remain intact. Do not discard a known rig and infer a replacement.

## Adding another provider

Keep provider transport and credentials outside `pcg-core`. Implement protected configuration, an explicit generate endpoint, polling and cancellation, local cache identity, size/type validation, actionable errors, and tests with a network-free stub. Add only manifest-backed node properties and document the provider's asset-license boundary.
