#include "pcg_api.h"

#include <cstdio>
#include <cstring>

/* ── Version ────────────────────────────────────────── */

const char* pcg_get_version(void)
{
    static char buf[64];
    std::snprintf(buf, sizeof(buf),
                  "pcg-core %d.%d.%d",
                  PCG_API_VERSION_MAJOR,
                  PCG_API_VERSION_MINOR,
                  PCG_API_VERSION_PATCH);
    return buf;
}

/* ── Graph API (Phase 0 stubs — Phase 1 will implement) ── */

PcgResultCode pcg_validate_graph(const char* /*json*/,
                                 char* err_buf,
                                 int err_buf_size)
{
    if (err_buf && err_buf_size > 0)
        err_buf[0] = '\0';
    return PCG_OK;
}

PcgResultCode pcg_execute_graph(const char* /*json*/,
                                int /*seed*/,
                                char* out_json,
                                int out_json_size)
{
    if (out_json && out_json_size > 0)
    {
        std::snprintf(out_json, out_json_size,
                       "{\"status\":\"ok\",\"points\":[]}");
    }
    return PCG_OK;
}
