#include "data/pcg_spline_data.hpp"

namespace pcg::internal::data {

void PcgSplineData::add_spline(PcgSpline spline)
{
    splines_.push_back(std::move(spline));
}

nlohmann::json PcgSplineData::to_json() const
{
    nlohmann::json splines = nlohmann::json::array();
    for (const auto& spline : splines_) {
        nlohmann::json points = nlohmann::json::array();
        for (const auto& point : spline.points) {
            points.push_back(nlohmann::json{
                {"x", point.x},
                {"y", point.y},
                {"z", point.z},
            });
        }
        nlohmann::json item{
            {"points", std::move(points)},
            {"closed", spline.closed},
        };
        if (spline.attributes.is_object() && !spline.attributes.empty())
            item["attributes"] = spline.attributes;
        splines.push_back(std::move(item));
    }

    nlohmann::json out{{"splines", std::move(splines)}};
    if (!metadata_.raw().empty())
        out["metadata"] = metadata_.raw();
    return out;
}

PcgSplineData PcgSplineData::from_json(const nlohmann::json& json)
{
    PcgSplineData data;
    if (!json.contains("splines") || !json["splines"].is_array())
        return data;

    for (const auto& item : json["splines"]) {
        if (!item.is_object() || !item.contains("points") || !item["points"].is_array())
            continue;

        PcgSpline spline;
        spline.closed = item.value("closed", false);
        if (item.contains("attributes") && item["attributes"].is_object())
            spline.attributes = item["attributes"];

        for (const auto& pt : item["points"]) {
            if (!pt.is_object())
                continue;
            spline.points.push_back(PcgSplinePoint{
                pt.value("x", 0.0),
                pt.value("y", 0.0),
                pt.value("z", 0.0),
            });
        }
        if (!spline.points.empty())
            data.splines_.push_back(std::move(spline));
    }

    if (json.contains("metadata"))
        data.metadata_ = PcgMetadata::from_json(json["metadata"]);

    return data;
}

} // namespace pcg::internal::data
