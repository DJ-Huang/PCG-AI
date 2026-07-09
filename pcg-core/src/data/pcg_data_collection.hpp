#pragma once

#include "data/pcg_data_types.hpp"
#include "data/pcg_param_data.hpp"
#include "data/pcg_point_data.hpp"
#include "data/pcg_mesh_data.hpp"
#include "data/pcg_geometry.hpp"
#include "data/pcg_spline_data.hpp"

#include <nlohmann/json.hpp>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pcg::internal::data {

struct PcgTaggedData {
    std::string tag;
    PcgDataType type = PcgDataType::Unknown;
    /** JSON payload for Param nodes and optional point sidecar fields (status/prefab/...). */
    nlohmann::json payload;
    std::shared_ptr<const PcgMeshData> mesh;
    std::shared_ptr<const PcgGeometry> geometry;
    std::shared_ptr<const PcgPointData> points;
    std::optional<PcgSplineData> splines;
};

/** Node input/output bus (UE PCGDataCollection analogue). */
class PcgDataCollection {
public:
    void add(const std::string& tag, PcgDataType type, nlohmann::json payload);
    void add_param(const std::string& tag, PcgParamData data);
    void add_points(const std::string& tag, PcgPointData data);
    void add_points_shared(const std::string& tag, std::shared_ptr<const PcgPointData> points);
    void add_points_with_meta(const std::string& tag,
                              PcgPointData data,
                              nlohmann::json sidecar);
    void add_points_shared_with_meta(const std::string& tag,
                                     std::shared_ptr<const PcgPointData> points,
                                     nlohmann::json sidecar);
    void add_splines(const std::string& tag, PcgSplineData data);
    void add_mesh(const std::string& tag, PcgMeshData data);
    void add_mesh_shared(const std::string& tag, std::shared_ptr<const PcgMeshData> mesh);
    void add_geometry(const std::string& tag, PcgGeometry data);
    void add_geometry_shared(const std::string& tag, std::shared_ptr<const PcgGeometry> geometry);

    const PcgTaggedData* find(const std::string& tag) const;
    const nlohmann::json* find_json(const std::string& tag) const;
    const PcgPointData* find_points(const std::string& tag) const;
    std::shared_ptr<const PcgPointData> find_points_shared(const std::string& tag) const;
    const PcgSplineData* find_splines(const std::string& tag) const;
    const PcgMeshData* find_mesh(const std::string& tag) const;
    std::shared_ptr<const PcgMeshData> find_mesh_shared(const std::string& tag) const;
    const PcgGeometry* find_geometry(const std::string& tag) const;
    std::shared_ptr<const PcgGeometry> find_geometry_shared(const std::string& tag) const;

    nlohmann::json primary_json() const;
    const PcgMeshData* primary_mesh() const;
    std::shared_ptr<const PcgMeshData> primary_mesh_shared() const;
    const PcgGeometry* primary_geometry() const;
    std::shared_ptr<const PcgGeometry> primary_geometry_shared() const;
    PcgDataType primary_type() const;

    const std::vector<PcgTaggedData>& items() const { return items_; }

private:
    static nlohmann::json build_point_sink_json(const PcgTaggedData& item);

    std::vector<PcgTaggedData> items_;
};

} // namespace pcg::internal::data
