#pragma once

#include "data/pcg_geometry.hpp"
#include "data/pcg_mesh_data.hpp"

#include <nlohmann/json.hpp>
#include <string>

namespace pcg::internal::elements {

struct TransformMeshOptions {
    std::string group;
    std::string group_type = "guess";
    std::string transform_order = "srt";
    std::string rotate_order = "xyz";
    data::PcgVec3 translate{0.0, 0.0, 0.0};
    data::PcgVec3 rotation_deg{0.0, 0.0, 0.0};
    data::PcgVec3 scale{1.0, 1.0, 1.0};
    data::PcgVec3 shear{0.0, 0.0, 0.0};
    double uniform_scale = 1.0;
    data::PcgVec3 pivot_translate{0.0, 0.0, 0.0};
    data::PcgVec3 pivot_rotate_deg{0.0, 0.0, 0.0};
    std::string pre_transform_order = "srt";
    std::string pre_rotate_order = "xyz";
    data::PcgVec3 pre_translate{0.0, 0.0, 0.0};
    data::PcgVec3 pre_rotation_deg{0.0, 0.0, 0.0};
    data::PcgVec3 pre_scale{1.0, 1.0, 1.0};
    data::PcgVec3 pre_shear{0.0, 0.0, 0.0};
    bool recompute_point_normals = false;
    bool preserve_normal_length = true;
    bool invert_transform = false;
    bool output_transform = false;
    std::string output_attribute = "xform";
    std::string output_multiply_order = "post";
};

TransformMeshOptions parse_transform_mesh_options(const nlohmann::json& data);

data::PcgGeometry transform_geometry(const data::PcgGeometry& geometry,
                                     const TransformMeshOptions& options);

data::PcgMeshData transform_mesh(const data::PcgMeshData& mesh,
                                 const TransformMeshOptions& options);

struct TransformByAttributeOptions {
    std::string group;
    std::string group_type = "guess";
    std::string transform_attribute = "xform";
    bool invert_transform = false;
    std::string attributes = "*";
    bool recompute_affected_normals = true;
    bool preserve_normal_length = true;
    bool delete_transform_attribute = true;
};

bool transform_by_attribute_geometry(const data::PcgGeometry& geometry,
                                     const TransformByAttributeOptions& options,
                                     data::PcgGeometry& output,
                                     std::string& error);

} // namespace pcg::internal::elements
