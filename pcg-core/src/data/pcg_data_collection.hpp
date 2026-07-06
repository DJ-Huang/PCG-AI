#pragma once

#include "data/pcg_data_types.hpp"
#include "data/pcg_param_data.hpp"
#include "data/pcg_point_data.hpp"
#include "data/pcg_mesh_data.hpp"
#include "data/pcg_spline_data.hpp"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

namespace pcg::internal::data {

struct PcgTaggedData {
    std::string tag;
    PcgDataType type = PcgDataType::Unknown;
    nlohmann::json payload;
    std::optional<PcgMeshData> mesh;
};

/** Node input/output bus (UE PCGDataCollection analogue). */
class PcgDataCollection {
public:
    void add(const std::string& tag, PcgDataType type, nlohmann::json payload);
    void add_param(const std::string& tag, PcgParamData data);
    void add_points(const std::string& tag, PcgPointData data);
    void add_splines(const std::string& tag, PcgSplineData data);
    void add_mesh(const std::string& tag, PcgMeshData data);

    const PcgTaggedData* find(const std::string& tag) const;
    const nlohmann::json* find_json(const std::string& tag) const;
    const PcgMeshData* find_mesh(const std::string& tag) const;
    nlohmann::json primary_json() const;
    const PcgMeshData* primary_mesh() const;
    PcgDataType primary_type() const;

    const std::vector<PcgTaggedData>& items() const { return items_; }

private:
    std::vector<PcgTaggedData> items_;
};

} // namespace pcg::internal::data
