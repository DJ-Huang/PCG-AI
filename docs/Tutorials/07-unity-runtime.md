# Unity Runtime

Unity serializes graph requests and sends them to the same-machine `pcg-server`. The server runs native execution and returns a versioned cook result that Unity converts into meshes, points, splines, terrain data, materials, and diagnostics.

The editor and Player paths share the HTTP model. The current Player deployment therefore requires a reachable local sidecar; it is not a self-contained offline native runtime. Product builds must package, start, monitor, and stop the compatible server or provide another supported endpoint.

## Verification

1. Build and start `pcg-server`.
2. Open the Unity project and run **PCG > Server > Health Check**.
3. Cook a representative graph twice.
4. Verify non-empty output, stable resource counts, material bindings, and clear errors when the server is stopped.

Do not copy native dylibs into `Assets/Plugins`; that path is obsolete for PICG.
