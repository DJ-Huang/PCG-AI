# Graph Executor

The executor validates the graph, computes a topological order, gathers typed inputs, executes registered elements, and collects sink outputs and diagnostics. Cycles and incompatible edges fail before node work begins.

Cook caching is content-based. A cache key must include node parameters, input content, implementation or dependency versions, seed, and relevant context. Invalidation propagates downstream from a changed node. Targeted preview failures stay targeted instead of silently executing the whole graph.

Use executor and cache tests to verify cycle detection, sink selection, deterministic results, cache hits, and correct invalidation.
