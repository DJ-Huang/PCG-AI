#include "data/pcg_data_collection.hpp"

namespace pcg::internal::data {

void PcgDataCollection::add(const std::string& tag, PcgDataType type, nlohmann::json payload)
{
    items_.push_back(PcgTaggedData{tag, type, std::move(payload)});
}

void PcgDataCollection::add_param(const std::string& tag, PcgParamData data)
{
    add(tag, PcgDataType::Param, data.to_json());
}

void PcgDataCollection::add_points(const std::string& tag, PcgPointData data)
{
    add(tag, PcgDataType::Point, data.to_json());
}

void PcgDataCollection::add_splines(const std::string& tag, PcgSplineData data)
{
    add(tag, PcgDataType::Spline, data.to_json());
}

void PcgDataCollection::add_mesh(const std::string& tag, PcgMeshData data)
{
    add(tag, PcgDataType::Mesh, data.to_json());
}

const PcgTaggedData* PcgDataCollection::find(const std::string& tag) const
{
    for (const auto& item : items_) {
        if (item.tag == tag)
            return &item;
    }
    return nullptr;
}

const nlohmann::json* PcgDataCollection::find_json(const std::string& tag) const
{
    const PcgTaggedData* item = find(tag);
    return item ? &item->payload : nullptr;
}

nlohmann::json PcgDataCollection::primary_json() const
{
    if (items_.empty())
        return nlohmann::json::object();

    const PcgTaggedData* preferred = find("out");
    if (preferred)
        return preferred->payload;

    return items_.back().payload;
}

} // namespace pcg::internal::data
