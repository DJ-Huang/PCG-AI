#include "data/pcg_mesh_data.hpp"

namespace pcg::internal::data {

void PcgMeshData::add_vertex(const PcgVertex& vertex)
{
    vertices_.push_back(vertex);
}

void PcgMeshData::add_triangle(int a, int b, int c)
{
    triangles_.push_back(a);
    triangles_.push_back(b);
    triangles_.push_back(c);
}

void PcgMeshData::set_normals(std::vector<PcgVertex> n)
{
    if (n.size() != vertices_.size())
        return;
    normals_ = std::move(n);
    has_normals_ = true;
}

nlohmann::json PcgMeshData::to_json() const
{
    nlohmann::json verts = nlohmann::json::array();
    for (const auto& v : vertices_) {
        verts.push_back(nlohmann::json{{"x", v.x}, {"y", v.y}, {"z", v.z}});
    }

    nlohmann::json tris = nlohmann::json::array();
    for (int index : triangles_)
        tris.push_back(index);

    nlohmann::json out{
        {"dataType", "mesh"},
        {"vertices", std::move(verts)},
        {"triangles", std::move(tris)},
        {"vertexCount", vertices_.size()},
        {"triangleCount", triangles_.size() / 3},
    };
    if (!metadata_.raw().empty())
        out["metadata"] = metadata_.raw();
    if (has_normals_ && !normals_.empty()) {
        nlohmann::json norms = nlohmann::json::array();
        for (const auto& n : normals_)
            norms.push_back(nlohmann::json{{"x", n.x}, {"y", n.y}, {"z", n.z}});
        out["normals"] = std::move(norms);
    }
    return out;
}

PcgMeshData PcgMeshData::from_json(const nlohmann::json& json)
{
    PcgMeshData data;
    if (!json.contains("vertices") || !json["vertices"].is_array())
        return data;

    for (const auto& item : json["vertices"]) {
        if (!item.is_object())
            continue;
        data.vertices_.push_back(PcgVertex{
            item.value("x", 0.0),
            item.value("y", 0.0),
            item.value("z", 0.0),
        });
    }

    if (json.contains("triangles") && json["triangles"].is_array()) {
        for (const auto& item : json["triangles"]) {
            if (item.is_number_integer())
                data.triangles_.push_back(item.get<int>());
        }
    }

    if (json.contains("metadata"))
        data.metadata_ = PcgMetadata::from_json(json["metadata"]);

    if (json.contains("normals") && json["normals"].is_array()) {
        std::vector<PcgVertex> norms;
        for (const auto& item : json["normals"]) {
            if (!item.is_object())
                continue;
            norms.push_back(PcgVertex{
                item.value("x", 0.0),
                item.value("y", 0.0),
                item.value("z", 0.0),
            });
        }
        if (norms.size() == data.vertices_.size()) {
            data.normals_ = std::move(norms);
            data.has_normals_ = true;
        }
    }

    return data;
}

} // namespace pcg::internal::data
