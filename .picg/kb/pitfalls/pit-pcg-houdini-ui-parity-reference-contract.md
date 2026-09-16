---
id: pit-pcg-houdini-ui-parity-reference-contract
name: Houdini UI parity requires a reference captured in the same state
severity: medium
rootCauseType: reference-state mismatch and excessive generic controls
techStack: [PICG, Unity-UI-Toolkit, Houdini]
tags: [type/pitfall, area/unity-editor, area/houdini-ui]
verified_status: limited
verified_by: "Carve inspector state-matched review"
verified_date: "2026-07-30"
---

# Compare equivalent inspector states

Matching labels and controls is not enough. Capture the Houdini reference with the same tab, operation, enabled toggles, group type, and expanded sections as the PCG inspector.

Treat the reference as a parameter contract:

- preserve section order and hierarchy;
- show only controls relevant to the current mode;
- use the matching control type, range, unit, and default;
- keep advanced compatibility fields out of the primary workflow;
- distinguish intentionally unsupported behavior from missing UI.

Validate the manifest, serializer, native implementation, and inspector together. A visually similar panel that writes a different value type or exposes inactive parameters is not parity.
