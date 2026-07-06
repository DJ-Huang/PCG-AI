#include "data/pcg_data_collection.hpp"

namespace pcg::internal::data {

void PcgDataCollection::add(const std::string& tag, PcgDataType type, nlohmann::json payload)
{
    PcgTaggedData item;
    item.tag = tag;
    item.type = type;
    item.payload = std::move(payload);
    items_.push_back(std::move(item));
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
    PcgTaggedData item;
    item.tag = tag;
    item.type = PcgDataType::Mesh;
    item.mesh = std::move(data);
    items_.push_back(std::move(item));
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
    if (!item || item->type == PcgDataType::Mesh)
        return nullptr;
    return &item->payload;
}

const PcgMeshData* PcgDataCollection::find_mesh(const std::string& tag) const
{
    const PcgTaggedData* item = find(tag);
    if (!item || item->type != PcgDataType::Mesh || !item->mesh)
        return nullptr;
    return &*item->mesh;
}

nlohmann::json PcgDataCollection::primary_json() const
{
    if (items_.empty())
        return nlohmann::json::object();

    const PcgTaggedData* preferred = find("out");
    if (preferred && preferred->type != PcgDataType::Mesh)
        return preferred->payload;

    for (const auto& item : items_) {
        if (item.type != PcgDataType::Mesh)
            return item.payload;
    }

    return nlohmann::json::object();
}

const PcgMeshData* PcgDataCollection::primary_mesh() const
{
    const PcgTaggedData* preferred = find("out");
    if (preferred && preferred->type == PcgDataType::Mesh && preferred->mesh)
        return &*preferred->mesh;

    for (const auto& item : items_) {
        if (item.type == PcgDataType::Mesh && item.mesh)
            return &*item.mesh;
    }

    return nullptr;
}

PcgDataType PcgDataCollection::primary_type() const
{
    if (items_.empty())
        return PcgDataType::Unknown;

    const PcgTaggedData* preferred = find("out");
    if (preferred)
        return preferred->type;

    return items_.back().type;
}

} // namespace pcg::internal::data
