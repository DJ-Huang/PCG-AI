---
id: pit-native-plugin-build-target-mismatch
name: Passing core tests does not deploy the runtime backend
severity: medium
rootCauseType: build target and deployment mismatch
techStack: [CPlusPlus, CMake, pcg-server, Unity]
tags: [type/pitfall, area/build-system, area/pcg]
verified_status: limited
verified_by: "PICG server migration and historical native-plugin incidents"
verified_date: "2026-07-28"
---

# Rebuild the backend that the editor actually uses

PICG Unity clients cook and export through the local HTTP `pcg-server`; they do not load `libPcgCore` or the FBX exporter in-process.

After a native change:

1. run `scripts/build-pcg-server.sh`;
2. restart with `scripts/run-pcg-server.sh`;
3. run the Unity **PCG > Server > Health Check** command;
4. cook a representative graph.

Core `ctest` results validate algorithms but do not prove that a running server contains the new code. Do not copy a dylib into the Unity project. Older projects that still load a native plugin require a shared-library build, deployment, macOS signing, and a full editor restart, but that is not the current PICG path.
