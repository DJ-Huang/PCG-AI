# Graph JSON and Schema

A graph document records nodes, edges, parameters, and optional subgraphs. The JSON Schema validates document shape; the node manifest validates node-specific properties, pins, defaults, and compatibility.

Use semantic unique IDs and exact manifest handle IDs. Treat pin IDs, property types, and enum values as serialized API. When the manifest changes, run `scripts/sync-manifest.sh` and the manifest/schema validators.

Validation should catch unknown node types, duplicate IDs, missing handles, incompatible pin types, malformed parameter targets, and cycles before native execution. Do not weaken validation to load a broken asset silently; add an explicit migration when preserving an older format.
