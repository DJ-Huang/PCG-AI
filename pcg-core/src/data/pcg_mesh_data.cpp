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

void PcgMeshData::set_colors(std::vector<PcgColor> c)
{
    if (c.size() != vertices_.size())
        return;
    colors_ = std::move(c);
    has_colors_ = true;
}

void PcgMeshData::set_uvs(std::vector<PcgVec2> uv)
{
    if (uv.size() != vertices_.size())
        return;
    uvs_ = std::move(uv);
    has_uvs_ = true;
}

void PcgMeshData::set_materials(std::vector<std::string> slots,
                                std::vector<uint32_t> triangle_materials)
{
    if (slots.empty() || triangle_materials.size() != triangles_.size() / 3) {
        material_slots_.clear();
        triangle_materials_.clear();
        return;
    }
    for (uint32_t slot : triangle_materials) {
        if (slot >= slots.size()) {
            material_slots_.clear();
            triangle_materials_.clear();
            return;
        }
    }
    material_slots_ = std::move(slots);
    triangle_materials_ = std::move(triangle_materials);
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
    if (has_colors_ && !colors_.empty()) {
        nlohmann::json cols = nlohmann::json::array();
        for (const auto& c : colors_)
            cols.push_back(nlohmann::json{{"r", c.r}, {"g", c.g}, {"b", c.b}, {"a", c.a}});
        out["colors"] = std::move(cols);
    }
    if (has_uvs_ && !uvs_.empty()) {
        nlohmann::json uv_arr = nlohmann::json::array();
        for (const auto& uv : uvs_)
            uv_arr.push_back(nlohmann::json{{"u", uv.u}, {"v", uv.v}});
        out["uvs"] = std::move(uv_arr);
    }
    if (has_materials()) {
        out["materialSlots"] = material_slots_;
        out["triangleMaterials"] = triangle_materials_;
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

    if (json.contains("materialSlots") && json["materialSlots"].is_array() &&
        json.contains("triangleMaterials") && json["triangleMaterials"].is_array()) {
        std::vector<std::string> slots;
        std::vector<uint32_t> triangle_materials;
        for (const auto& item : json["materialSlots"]) {
            if (item.is_string())
                slots.push_back(item.get<std::string>());
        }
        for (const auto& item : json["triangleMaterials"]) {
            if (item.is_number_unsigned() || item.is_number_integer())
                triangle_materials.push_back(item.get<uint32_t>());
        }
        data.set_materials(std::move(slots), std::move(triangle_materials));
    }

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

    if (json.contains("colors") && json["colors"].is_array()) {
        std::vector<PcgColor> cols;
        for (const auto& item : json["colors"]) {
            if (!item.is_object())
                continue;
            cols.push_back(PcgColor{
                item.value("r", 1.0),
                item.value("g", 1.0),
                item.value("b", 1.0),
                item.value("a", 1.0),
            });
        }
        if (cols.size() == data.vertices_.size()) {
            data.colors_ = std::move(cols);
            data.has_colors_ = true;
        }
    }

    if (json.contains("uvs") && json["uvs"].is_array()) {
        std::vector<PcgVec2> uv_arr;
        for (const auto& item : json["uvs"]) {
            if (!item.is_object())
                continue;
            uv_arr.push_back(PcgVec2{
                item.value("u", 0.0),
                item.value("v", 0.0),
            });
        }
        if (uv_arr.size() == data.vertices_.size()) {
            data.uvs_ = std::move(uv_arr);
            data.has_uvs_ = true;
        }
    }

    return data;
}

} // namespace pcg::internal::data
