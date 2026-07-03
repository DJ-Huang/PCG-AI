#include "data/pcg_point_data.hpp"

namespace pcg::internal::data {

void PcgPointData::add_point(const PcgPoint& point)
{
    points_.push_back(point);
}

nlohmann::json PcgPointData::to_json() const
{
    nlohmann::json points = nlohmann::json::array();
    for (const auto& point : points_) {
        nlohmann::json item{
            {"x", point.x},
            {"y", point.y},
            {"z", point.z},
        };
        if (point.attributes.is_object() && !point.attributes.empty())
            item["attributes"] = point.attributes;
        points.push_back(std::move(item));
    }

    nlohmann::json out{{"points", std::move(points)}};
    if (!metadata_.raw().empty())
        out["metadata"] = metadata_.raw();
    return out;
}

PcgPointData PcgPointData::from_json(const nlohmann::json& json)
{
    PcgPointData data;
    if (!json.contains("points") || !json["points"].is_array())
        return data;

    for (const auto& item : json["points"]) {
        if (!item.is_object())
            continue;
        data.points_.push_back(PcgPoint{
            item.value("x", 0.0),
            item.value("y", 0.0),
            item.value("z", 0.0),
            item.contains("attributes") && item["attributes"].is_object()
                ? item["attributes"]
                : nlohmann::json::object(),
        });
    }

    if (json.contains("metadata"))
        data.metadata_ = PcgMetadata::from_json(json["metadata"]);

    return data;
}

} // namespace pcg::internal::data
