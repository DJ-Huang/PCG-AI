#pragma once

#include "data/pcg_metadata.hpp"

#include <nlohmann/json.hpp>

#include <vector>

namespace pcg::internal::data {

struct PcgSplinePoint {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

/** Polyline / edge segment payload (UE PCGSplineData analogue). */
struct PcgSpline {
    std::vector<PcgSplinePoint> points;
    bool closed = false;
    nlohmann::json attributes = nlohmann::json::object();
};

class PcgSplineData {
public:
    void add_spline(PcgSpline spline);
    std::vector<PcgSpline>& splines_mut() { return splines_; }
    const std::vector<PcgSpline>& splines() const { return splines_; }
    PcgMetadata& metadata() { return metadata_; }
    const PcgMetadata& metadata() const { return metadata_; }

    nlohmann::json to_json() const;
    static PcgSplineData from_json(const nlohmann::json& json);

private:
    std::vector<PcgSpline> splines_;
    PcgMetadata metadata_;
};

} // namespace pcg::internal::data
