// Phase 0 smoke test — verifies pcg_get_version() is callable.
// Phase 1 will add real graph execution tests.

#include "pcg_api.h"

#include <cassert>
#include <cstdio>
#include <cstring>

int main()
{
    const char* ver = pcg_get_version();
    assert(ver != nullptr);
    assert(std::strstr(ver, "pcg-core") != nullptr);
    std::printf("PASS: %s\n", ver);

    // Validate stub
    PcgResultCode rc = pcg_validate_graph("{}", nullptr, 0);
    assert(rc == PCG_OK);

    // Execute stub
    char out[256];
    rc = pcg_execute_graph("{}", 42, out, sizeof(out));
    assert(rc == PCG_OK);
    assert(std::strstr(out, "ok") != nullptr);
    std::printf("PASS: execute_graph -> %s\n", out);

    return 0;
}
