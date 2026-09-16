---
id: pit-pcg-unity-dylib-integration
name: PICG no longer embeds a native dylib in Unity
severity: medium
rootCauseType: obsolete integration path
tags: [type/pitfall, area/unity-editor, area/build-system]
verified_status: verified_true
verified_by: "Current Unity HTTP client and pcg-server workflow"
---

# Use the HTTP server boundary

Do not add `DllImport`, copy `libPcgCore` into `Unity/Assets/Plugins`, or configure per-platform native plugin import settings. Unity communicates with the same-machine `pcg-server` over HTTP. Rebuild and restart the server, run the health check, and validate a real cook instead.
