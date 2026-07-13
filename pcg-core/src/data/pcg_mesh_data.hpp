#pragma once

#include "data/pcg_metadata.hpp"

#include <nlohmann/json.hpp>

#include <vector>

namespace pcg::internal::data {

struct PcgVertex {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
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

    nlohmann::json to_json() const;
    static PcgMeshData from_json(const nlohmann::json& json);

private:
    std::vector<PcgVertex> vertices_;
    std::vector<int> triangles_;
    PcgMetadata metadata_;
    std::vector<PcgVertex> normals_;
    bool has_normals_ = false;
};

} // namespace pcg::internal::data
