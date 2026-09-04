# pcg-core

`pcg-core` is the C++17 graph parser, validator and procedural geometry runtime. Its public boundary is the C API in `include/pcg_api.h`.

## Build and test

From the repository root:

```bash
./scripts/build-pcg-core.sh --run-tests
```

Or use CMake directly:

```bash
cmake -S pcg-core -B pcg-core/build -DCMAKE_BUILD_TYPE=Release
cmake --build pcg-core/build --parallel
ctest --test-dir pcg-core/build --output-on-failure
```

The build fetches pinned nlohmann/json and Assimp dependencies. Initialize the CDT submodule before compiling a fresh clone.

Unity does not load this library directly. `pcg-server` links the static target and exposes the runtime over localhost HTTP.

## Source layout

- `include/` — public C API.
- `src/data/` — geometry and attribute data models.
- `src/elements/` — node algorithms and registrations.
- `src/geometry/` — topology and geometry kernels.
- `tests/` — CTest executables and fixtures.

Node registration must stay synchronized with `schema/node-manifest.json`.

