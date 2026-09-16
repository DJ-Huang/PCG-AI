---
id: pit-multi-path-node-explicit-input-mode
name: Multi-path nodes require an explicit input mode
severity: medium
rootCauseType: routing user intent from incidental data presence
techStack: [PICG, Unity-GraphView, node-manifest]
tags: [type/pitfall, area/pcg, area/graph-authoring]
verified_status: limited
verified_by: "OutlineSolid explicit inputMode implementation"
verified_date: "2026-08-01"
---

# Use an explicit mode for mutually exclusive inputs

Do not choose a node's execution path because a pin happens to be connected or a saved JSON field is non-empty. Old data can remain on more than one path and silently select the wrong behavior.

Add one enum such as `inputMode` and make it the only route selector. Execution reads only the selected path and reports a missing-input error instead of falling back. The inspector and ports use conditional visibility to show only the active mode. Legacy assets should be migrated once rather than keeping implicit routing in the runtime.

Test with multiple paths populated: only the explicit mode may affect the result.
