#include "pcg_api.h"

#include "graph_executor.hpp"
#include "graph_parser.hpp"
#include "internal/error_util.hpp"

#include <cstdio>
#include <cstring>

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

PcgResultCode pcg_validate_graph(const char* json,
                                 char* err_buf,
                                 int err_buf_size)
{
    pcg::internal::write_error(err_buf, err_buf_size, "");

    pcg::internal::Graph graph;
    const PcgResultCode parse_code =
        pcg::internal::parse_graph(json, graph, err_buf, err_buf_size);
    if (parse_code != PCG_OK)
        return parse_code;

    return pcg::internal::validate_graph_structure(graph, err_buf, err_buf_size);
}

PcgResultCode pcg_execute_graph(const char* json,
                                int seed,
                                char* out_json,
                                int out_json_size)
{
    char local_err[1024] = {};
    pcg::internal::write_error(local_err, sizeof(local_err), "");

    const PcgResultCode validate_code = pcg_validate_graph(json, local_err, sizeof(local_err));
    if (validate_code != PCG_OK) {
        if (out_json && out_json_size > 0)
            out_json[0] = '\0';
        return validate_code;
    }

    pcg::internal::Graph graph;
    const PcgResultCode parse_code =
        pcg::internal::parse_graph(json, graph, local_err, sizeof(local_err));
    if (parse_code != PCG_OK) {
        if (out_json && out_json_size > 0)
            out_json[0] = '\0';
        return parse_code;
    }

    nlohmann::json result;
    const PcgResultCode exec_code =
        pcg::internal::execute_graph(graph, seed, result, local_err, sizeof(local_err));
    if (exec_code != PCG_OK) {
        if (out_json && out_json_size > 0)
            out_json[0] = '\0';
        return exec_code;
    }

    if (!out_json || out_json_size <= 0)
        return PCG_OK;

    const std::string serialized = result.dump();
    if (static_cast<int>(serialized.size()) >= out_json_size)
        return PCG_ERR_EXECUTION;

    std::strncpy(out_json, serialized.c_str(), static_cast<size_t>(out_json_size - 1));
    out_json[out_json_size - 1] = '\0';
    return PCG_OK;
}
