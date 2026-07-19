#pragma once

#include "data/pcg_metadata.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace pcg::internal::data {

struct PcgVertex {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct PcgColor {
    double r = 1.0;
    double g = 1.0;
    double b = 1.0;
    double a = 1.0;
};

struct PcgVec2 {
    double u = 0.0;
    double v = 0.0;
};

/** Triangle mesh payload (UE PCG surface / geometry analogue). */
class PcgMeshData {
public:
    void add_vertex(const PcgVertex& vertex);
    void add_triangle(int a, int b, int c);

    std::vector<PcgVertex>& vertices_mut() { return vertices_; }
    const std::vector<PcgVertex>& vertices() const { return vertices_; }
    std::vector<int>& triangles_mut() { return triangles_; }
    const std::vector<int>& triangles() const { return triangles_; }
    PcgMetadata& metadata() { return metadata_; }
    const PcgMetadata& metadata() const { return metadata_; }

    bool has_normals() const { return has_normals_; }
    const std::vector<PcgVertex>& normals() const { return normals_; }
    void set_normals(std::vector<PcgVertex> n);

    bool has_colors() const { return has_colors_; }
    const std::vector<PcgColor>& colors() const { return colors_; }
    void set_colors(std::vector<PcgColor> c);

    bool has_uvs() const { return has_uvs_; }
    const std::vector<PcgVec2>& uvs() const { return uvs_; }
    void set_uvs(std::vector<PcgVec2> uv);

    bool has_materials() const { return !material_slots_.empty() && triangle_materials_.size() == triangles_.size() / 3; }
    const std::vector<std::string>& material_slots() const { return material_slots_; }
    const std::vector<uint32_t>& triangle_materials() const { return triangle_materials_; }
    void set_materials(std::vector<std::string> slots, std::vector<uint32_t> triangle_materials);

    nlohmann::json to_json() const;
    static PcgMeshData from_json(const nlohmann::json& json);

private:
    std::vector<PcgVertex> vertices_;
    std::vector<int> triangles_;
    PcgMetadata metadata_;
    std::vector<PcgVertex> normals_;
    bool has_normals_ = false;
    std::vector<PcgColor> colors_;
    bool has_colors_ = false;
    std::vector<PcgVec2> uvs_;
    bool has_uvs_ = false;
    std::vector<std::string> material_slots_;
    std::vector<uint32_t> triangle_materials_;
};

} // namespace pcg::internal::data
