# C API Boundary

The C API is the stable native boundary beneath `pcg-server`. It accepts versioned graph requests, returns versioned result envelopes, and keeps C++ exceptions and ownership details inside the library.

Callers must release buffers through the matching API, respect cancellation and job scope, and inspect structured errors before reading result payloads. New API versions may extend capability, but older readers need a deliberate fallback path.

Test malformed JSON, empty graphs, cancellation, repeated calls, and ownership cleanup. A C++ exception must never escape across the C ABI.
