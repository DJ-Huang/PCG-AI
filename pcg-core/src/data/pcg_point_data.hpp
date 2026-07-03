#pragma once

#include "data/pcg_metadata.hpp"

#include <nlohmann/json.hpp>

#include <vector>

namespace pcg::internal::data {

struct PcgPoint {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    nlohmann::json attributes = nlohmann::json::object();
};

/** Spatial point cloud payload (UE PCGPointData analogue). */
class PcgPointData {
public:
    void add_point(const PcgPoint& point);
    std::vector<PcgPoint>& points_mut() { return points_; }
    const std::vector<PcgPoint>& points() const { return points_; }
    PcgMetadata& metadata() { return metadata_; }
    const PcgMetadata& metadata() const { return metadata_; }

    nlohmann::json to_json() const;
    static PcgPointData from_json(const nlohmann::json& json);

private:
    std::vector<PcgPoint> points_;
    PcgMetadata metadata_;
};

} // namespace pcg::internal::data
