#pragma once

#include "data/pcg_context.hpp"
#include "data/pcg_point_data.hpp"
#include "data/pcg_spline_data.hpp"
#include "pcg_api.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace pcg::internal::elements {

PcgResultCode fail_ctx(PcgContext& ctx, PcgResultCode code, const char* message);

const nlohmann::json* require_input_json(PcgContext& ctx, const char* pin, const char* node_label);

data::PcgPointData parse_point_input(const nlohmann::json& json);
data::PcgSplineData parse_spline_input(const nlohmann::json& json);
nlohmann::json point_data_to_json(const data::PcgPointData& data);
void emit_points(PcgContext& ctx, data::PcgPointData data);
void emit_splines(PcgContext& ctx, data::PcgSplineData data);

uint32_t mix_seed(int a, int b);
uint32_t next_rand(uint32_t& state);
double simple_noise(double x, double z, int seed);

std::vector<std::string> parse_name_list(const nlohmann::json& data, const char* key);

} // namespace pcg::internal::elements
