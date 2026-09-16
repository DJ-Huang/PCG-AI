---
recipe_id: pcg.recipe:previs:character-proxy
version: 1
roles: [character, extra]
root_node_types: [TransformMesh]
---

# Character Proxy

Instantiate a character Library Subgraph once per scene character and connect it to a final `TransformMesh`. The transform is the scene semantic owner; the shared Subgraph definition remains generic.

Safe edits are Library parameter overrides, the owner transform, component anchors, action clips, object transform keys, and visibility ranges. Coordinates use Unity world space with +Y up and local +Z as forward.

Do not duplicate the Library definition for each actor, do not reuse a scene `componentId`, and do not put the same ordinary node in multiple `memberNodeIds` lists. Validate human-scale bounds, feet/head/look anchors, shot duration, and final preview framing.

Common failures are duplicated definitions, generic Library component IDs leaking into instances, action clips assigned to the wrong component, and transform animation applied to an unbound component.
