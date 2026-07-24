#pragma once

#include "data/pcg_context.hpp"
#include "data/pcg_point_data.hpp"
#include "data/pcg_mesh_data.hpp"
#include "data/pcg_geometry.hpp"
#include "data/pcg_heightfield.hpp"
#include "data/pcg_spline_data.hpp"
#include "pcg_api.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace pcg::internal::elements {

PcgResultCode fail_ctx(PcgContext& ctx, PcgResultCode code, const char* message);

const nlohmann::json* require_input_json(PcgContext& ctx, const char* pin, const char* node_label);

data::PcgPointData get_points_input(PcgContext& ctx, const char* pin, const char* label);
data::PcgPointData parse_point_input(const nlohmann::json& json);
data::PcgSplineData get_splines_input(PcgContext& ctx, const char* pin, const char* label);
data::PcgSplineData parse_spline_input(const nlohmann::json& json);
data::PcgMeshData parse_mesh_input(const nlohmann::json& json);
data::PcgMeshData get_mesh_input(PcgContext& ctx, const char* pin, const char* label);
data::PcgGeometry get_geometry_input(PcgContext& ctx, const char* pin, const char* label);
/// Returns nullptr when the pin is disconnected or empty (optional second inputs).
const data::PcgGeometry* optional_geometry_input(PcgContext& ctx,
                                                 const char* pin,
                                                 data::PcgGeometry& storage);
data::PcgHeightField get_heightfield_input(PcgContext& ctx, const char* pin, const char* label);
void emit_geometry(PcgContext& ctx, data::PcgGeometry data);
void emit_geometry_shared(PcgContext& ctx,
                          const std::string& tag,
                          std::shared_ptr<const data::PcgGeometry> geometry);
nlohmann::json point_data_to_json(const data::PcgPointData& data);
void emit_points(PcgContext& ctx, data::PcgPointData data);
void emit_points_with_meta(PcgContext& ctx,
                           data::PcgPointData data,
                           nlohmann::json sidecar);
void emit_points_shared_with_meta(PcgContext& ctx,
                                  std::shared_ptr<const data::PcgPointData> points,
                                  nlohmann::json sidecar);
void emit_splines(PcgContext& ctx, data::PcgSplineData data);
void emit_mesh(PcgContext& ctx, data::PcgMeshData data);
void emit_heightfield(PcgContext& ctx, data::PcgHeightField data);
void emit_heightfield_shared(PcgContext& ctx,
                             const std::string& tag,
                             std::shared_ptr<const data::PcgHeightField> heightfield);
void emit_mesh_shared(PcgContext& ctx,
                      const std::string& tag,
                      std::shared_ptr<const data::PcgMeshData> mesh);

uint32_t mix_seed(int a, int b);
uint32_t next_rand(uint32_t& state);
double simple_noise(double x, double z, int seed);
double perlin_noise_3d(double x, double y, double z, int seed);

/// Houdini-style seed: whole numbers keep integer identity (2.0 → 2); fractional
/// values are hashed so 2.3 ≠ 2. Shared entry point for nodes that promote seed
/// from manifest `integer` to `number`.
int normalize_seed_number(double seed);

/// Read JSON seed that may be integer or number, preserving the float domain.
/// Missing/null → default_value. Prefer this over `data.value("seed", 0)` — the
/// latter truncates floats like 2.3 → 2 (nlohmann get<int>).
double read_seed_param_number(const nlohmann::json& data,
                              const char* key,
                              double default_value = 0.0);

/// Read JSON seed as normalized int (whole floats keep identity; fractionals hash).
int read_seed_param(const nlohmann::json& data, const char* key, int default_value = 0);

/// Mix Houdini-style float node seed with graph seed into LCG state.
uint32_t rng_state_from_seed(double node_seed, int graph_seed = 0);

std::vector<std::string> parse_name_list(const nlohmann::json& data, const char* key);

data::PcgVec3 read_vector_param(const nlohmann::json& data,
                                const char* key,
                                const data::PcgVec3& fallback = {0.0, 0.0, 0.0});

} // namespace pcg::internal::elements
