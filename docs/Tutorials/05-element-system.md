# Element System

Each registered element implements one node's execution behavior while reusable geometry or data algorithms remain separate from graph plumbing. Registration connects the serialized manifest type to an element factory.

An element should validate inputs, read parameters without throwing across public boundaries, produce typed outputs, report actionable diagnostics, and honour cancellation. The manifest, native registration, serializer, generated documentation, and tests must describe the same contract.

Prefer a small element adapter around a separately testable algorithm. This keeps graph execution concerns out of geometry code and makes host-independent regression tests possible.
