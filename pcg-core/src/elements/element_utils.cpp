#include "elements/element_utils.hpp"

#include "internal/error_util.hpp"

#include <cmath>

namespace pcg::internal::elements {

PcgResultCode fail_ctx(PcgContext& ctx, PcgResultCode code, const char* message)
{
    write_error(ctx.err_buf, ctx.err_buf_size, message);
    return code;
}

const nlohmann::json* require_input_json(PcgContext& ctx, const char* pin, const char* node_label)
{
    const nlohmann::json* input = ctx.inputs.find_json(pin);
    if (!input)
        fail_ctx(ctx, PCG_ERR_EXECUTION, node_label);
    return input;
}

data::PcgPointData parse_point_input(const nlohmann::json& json)
{
    return data::PcgPointData::from_json(json);
}

data::PcgSplineData parse_spline_input(const nlohmann::json& json)
{
    return data::PcgSplineData::from_json(json);
}

nlohmann::json point_data_to_json(const data::PcgPointData& data)
{
    return data.to_json();
}

void emit_points(PcgContext& ctx, data::PcgPointData data)
{
    ctx.outputs.add_points("out", std::move(data));
}

void emit_splines(PcgContext& ctx, data::PcgSplineData data)
{
    ctx.outputs.add_splines("out", std::move(data));
}

uint32_t mix_seed(int a, int b)
{
    return static_cast<uint32_t>(a) ^ static_cast<uint32_t>(b * 2654435761);
}

uint32_t next_rand(uint32_t& state)
{
    state = state * 1664525u + 1013904223u;
    return state;
}

double simple_noise(double x, double z, int seed)
{
    const double nx = x * 0.1 + static_cast<double>(seed) * 0.013;
    const double nz = z * 0.1 + static_cast<double>(seed) * 0.017;
    return std::sin(nx * 1.7) * std::cos(nz * 1.3) * 0.5 +
           std::sin(nx * 3.1 + nz * 2.4) * 0.25;
}

std::vector<std::string> parse_name_list(const nlohmann::json& data, const char* key)
{
    std::vector<std::string> names;
    if (data.contains(key) && data[key].is_array()) {
        for (const auto& item : data[key]) {
            if (item.is_string())
                names.push_back(item.get<std::string>());
        }
        return names;
    }

    const std::string csv = data.value(key, "");
    std::string current;
    for (char ch : csv) {
        if (ch == ',' || ch == ';') {
            if (!current.empty())
                names.push_back(current);
            current.clear();
        } else if (ch != ' ') {
            current.push_back(ch);
        }
    }
    if (!current.empty())
        names.push_back(current);
    return names;
}

} // namespace pcg::internal::elements
