# Unity Graph Editor

The Unity editor builds nodes, ports, and inspector controls from the canonical manifest. It validates connections before serialization and uses scene handles for supported spatial inputs such as splines.

Keep node rendering generic unless a behavior genuinely requires specialized UI. Mode-dependent properties and ports must use explicit state and conditional visibility. Undo/redo, file reload, and node-level preview should preserve graph identity and avoid stale cooks.

When changing editor behavior, verify script compilation, graph round-tripping, port compatibility, undo/redo, and a live cook. A screenshot alone does not prove serialization or execution correctness.
