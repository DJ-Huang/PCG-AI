#include "data/pcg_data_collection.hpp"

#include "data/pcg_geometry.hpp"

namespace pcg::internal::data {
namespace {

void append_sidecar(nlohmann::json& out, const nlohmann::json& sidecar)
{
    if (!sidecar.is_object())
        return;
    for (auto it = sidecar.begin(); it != sidecar.end(); ++it)
        out[it.key()] = it.value();
}

} // namespace

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
    PcgTaggedData item;
    item.tag = tag;
    item.type = PcgDataType::Point;
    item.points = std::make_shared<PcgPointData>(std::move(data));
    items_.push_back(std::move(item));
}

void PcgDataCollection::add_points_shared(const std::string& tag,
                                          std::shared_ptr<const PcgPointData> points)
{
    if (!points)
        return;

    PcgTaggedData item;
    item.tag = tag;
    item.type = PcgDataType::Point;
    item.points = std::move(points);
    items_.push_back(std::move(item));
}

void PcgDataCollection::add_points_with_meta(const std::string& tag,
                                             PcgPointData data,
                                             nlohmann::json sidecar)
{
    PcgTaggedData item;
    item.tag = tag;
    item.type = PcgDataType::Point;
    item.points = std::make_shared<PcgPointData>(std::move(data));
    item.payload = std::move(sidecar);
    items_.push_back(std::move(item));
}

void PcgDataCollection::add_points_shared_with_meta(const std::string& tag,
                                                    std::shared_ptr<const PcgPointData> points,
                                                    nlohmann::json sidecar)
{
    if (!points)
        return;

    PcgTaggedData item;
    item.tag = tag;
    item.type = PcgDataType::Point;
    item.points = std::move(points);
    item.payload = std::move(sidecar);
    items_.push_back(std::move(item));
}

void PcgDataCollection::add_splines(const std::string& tag, PcgSplineData data)
{
    PcgTaggedData item;
    item.tag = tag;
    item.type = PcgDataType::Spline;
    item.splines = std::move(data);
    items_.push_back(std::move(item));
}

void PcgDataCollection::add_mesh(const std::string& tag, PcgMeshData data)
{
    PcgTaggedData item;
    item.tag = tag;
    item.type = PcgDataType::Mesh;
    item.mesh = std::make_shared<PcgMeshData>(std::move(data));
    items_.push_back(std::move(item));
}

void PcgDataCollection::add_mesh_shared(const std::string& tag,
                                        std::shared_ptr<const PcgMeshData> mesh)
{
    if (!mesh)
        return;

    PcgTaggedData item;
    item.tag = tag;
    item.type = PcgDataType::Mesh;
    item.mesh = std::move(mesh);
    items_.push_back(std::move(item));
}

void PcgDataCollection::add_geometry(const std::string& tag, PcgGeometry data)
{
    PcgTaggedData item;
    item.tag = tag;
    item.type = PcgDataType::Geometry;
    item.geometry = std::make_shared<PcgGeometry>(std::move(data));
    items_.push_back(std::move(item));
}

void PcgDataCollection::add_geometry_shared(const std::string& tag,
                                            std::shared_ptr<const PcgGeometry> geometry)
{
    if (!geometry)
        return;

    PcgTaggedData item;
    item.tag = tag;
    item.type = PcgDataType::Geometry;
    item.geometry = std::move(geometry);
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
    if (!item || item->type == PcgDataType::Mesh || item->type == PcgDataType::Geometry ||
        item->points || item->splines)
        return nullptr;
    return &item->payload;
}

const PcgPointData* PcgDataCollection::find_points(const std::string& tag) const
{
    const auto shared = find_points_shared(tag);
    return shared ? shared.get() : nullptr;
}

std::shared_ptr<const PcgPointData> PcgDataCollection::find_points_shared(
    const std::string& tag) const
{
    const PcgTaggedData* item = find(tag);
    if (!item || !item->points)
        return nullptr;
    return item->points;
}

const PcgSplineData* PcgDataCollection::find_splines(const std::string& tag) const
{
    const PcgTaggedData* item = find(tag);
    if (!item || !item->splines)
        return nullptr;
    return &*item->splines;
}

const PcgMeshData* PcgDataCollection::find_mesh(const std::string& tag) const
{
    const auto shared = find_mesh_shared(tag);
    return shared ? shared.get() : nullptr;
}

std::shared_ptr<const PcgMeshData> PcgDataCollection::find_mesh_shared(
    const std::string& tag) const
{
    const PcgTaggedData* item = find(tag);
    if (!item || item->type != PcgDataType::Mesh || !item->mesh)
        return nullptr;
    return item->mesh;
}

const PcgGeometry* PcgDataCollection::find_geometry(const std::string& tag) const
{
    const auto shared = find_geometry_shared(tag);
    return shared ? shared.get() : nullptr;
}

std::shared_ptr<const PcgGeometry> PcgDataCollection::find_geometry_shared(
    const std::string& tag) const
{
    const PcgTaggedData* item = find(tag);
    if (!item || item->type != PcgDataType::Geometry || !item->geometry)
        return nullptr;
    return item->geometry;
}

nlohmann::json PcgDataCollection::build_point_sink_json(const PcgTaggedData& item)
{
    nlohmann::json out = item.points->to_json();
    append_sidecar(out, item.payload);
    if (!out.contains("pointCount") && item.points)
        out["pointCount"] = item.points->points().size();
    return out;
}

nlohmann::json PcgDataCollection::primary_json() const
{
    if (items_.empty())
        return nlohmann::json::object();

    const PcgTaggedData* preferred = find("out");
    if (preferred) {
        if (preferred->points)
            return build_point_sink_json(*preferred);
        if (preferred->splines)
            return preferred->splines->to_json();
        if (preferred->type != PcgDataType::Mesh)
            return preferred->payload;
    }

    for (const auto& item : items_) {
        if (item.points)
            return build_point_sink_json(item);
        if (item.splines)
            return item.splines->to_json();
        if (item.type != PcgDataType::Mesh)
            return item.payload;
    }

    return nlohmann::json::object();
}

const PcgMeshData* PcgDataCollection::primary_mesh() const
{
    const auto shared = primary_mesh_shared();
    return shared ? shared.get() : nullptr;
}

std::shared_ptr<const PcgMeshData> PcgDataCollection::primary_mesh_shared() const
{
    const PcgTaggedData* preferred = find("out");
    if (preferred) {
        if (preferred->type == PcgDataType::Mesh && preferred->mesh)
            return preferred->mesh;
        if (preferred->type == PcgDataType::Geometry && preferred->geometry)
            return nullptr;
    }

    for (const auto& item : items_) {
        if (item.type == PcgDataType::Mesh && item.mesh)
            return item.mesh;
    }

    return nullptr;
}

const PcgGeometry* PcgDataCollection::primary_geometry() const
{
    const auto shared = primary_geometry_shared();
    return shared ? shared.get() : nullptr;
}

std::shared_ptr<const PcgGeometry> PcgDataCollection::primary_geometry_shared() const
{
    const PcgTaggedData* preferred = find("out");
    if (preferred && preferred->type == PcgDataType::Geometry && preferred->geometry)
        return preferred->geometry;

    for (const auto& item : items_) {
        if (item.type == PcgDataType::Geometry && item.geometry)
            return item.geometry;
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
