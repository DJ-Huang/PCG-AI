---
id: pit-react-flow-nodetypes-default-mapping
name: React Flow nodeTypes must map every rendered node kind
severity: medium
rootCauseType: renderer registration drift
techStack: [React-Flow, TypeScript, PICG-Web]
tags: [type/pitfall, area/web-editor]
verified_status: limited
verified_by: "PICG web editor node rendering regression"
---

# Keep node type registration exhaustive

React Flow renders a node through `nodeTypes[node.type]`. A graph loader that emits a new type without registering a renderer produces warnings, missing controls, or a fallback appearance.

Centralize the mapping, give ordinary manifest-driven nodes one stable renderer type, and reserve special types for genuinely different behavior. Add a test that every loader-produced type exists in the mapping. Do not derive renderer type from a user-editable display category.
