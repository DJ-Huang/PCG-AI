# Subgraphs

Subgraphs package a complete, named unit of procedural work behind typed inputs and outputs. Use them for repeated parts or to keep a large root graph understandable; avoid tiny wrappers and unrelated mega-subgraphs.

`library/` is the canonical source for built-in reusable assets. Each subgraph has a `.pcgsubgraph` document and library metadata. Run `python3 scripts/build_library_index.py` after editing so validation, indexing, and Unity/Web synchronization happen together.

Authoring-time linked assets must be flattened or resolved before native execution. Validate interface changes against every parent graph because promoted pins are serialized compatibility boundaries.
