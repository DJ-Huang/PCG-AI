# End-to-End Debugging

Debug from the contract boundary inward:

1. validate the graph and manifest;
2. check `pcg-server` health and restart it after native changes;
3. reproduce through a direct server request;
4. inspect native diagnostics and focused tests;
5. verify host parsing and presentation;
6. confirm the final result in the affected Web or Unity client.

Use `./scripts/build-pcg-core.sh --run-tests` for native regression, repository validators for synchronized contracts, and Web lint/build/tests for the browser client. A green native suite does not prove that a running server was rebuilt, and a successful server cook does not prove a host rendered the result correctly.
