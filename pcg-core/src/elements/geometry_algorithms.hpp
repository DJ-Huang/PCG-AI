#pragma once

#include "data/pcg_geometry.hpp"

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace pcg::internal::elements {

/// Houdini Group Create–aligned options: sticky identity + Enable-gated filters.
struct GroupCreateOptions {
    std::string output_group = "bevel_edges";
    std::string domain = "edge";
    /// replace | union | intersect | subtract against an existing group of the same name.
    std::string initial_merge = "replace";

    bool enable_base_group = false;
    std::vector<std::string> base_groups;

    bool enable_bounding = false;

    bool enable_normals = false;
    double direction_x = 0.0;
    double direction_y = 1.0;
    double direction_z = 0.0;
    double spread_angle_deg = 30.0;

    /// Default true so C++ callers that only set mode/angle keep prior bevel behavior.
    bool enable_edges = true;
    /// angle | unshared | all — used when enable_edges (edge domain).
    std::string mode = "angle";
    double min_edge_angle_deg = 30.0;
    bool include_unshared = false;
    std::vector<std::string> from_face_groups;
    std::vector<std::string> from_edge_groups;

    bool enable_random = false;
    double random_chance = 1.0;
    int random_seed = 0;
};

struct GroupCombineOptions {
    std::string output_group = "combined";
    std::string domain = "edge";
    std::string operation = "union";
    std::vector<std::string> source_groups;
};

struct FaceGroupByNormalOptions {
    std::string output_group = "material_faces";
    double direction_x = 0.0;
    double direction_y = 1.0;
    double direction_z = 0.0;
    double spread_angle_deg = 30.0;
};

/// Houdini Group Promote SOP–aligned options (Point / Vertex / Edge / Face).
struct GroupPromoteOptions {
    std::string from_domain = "point";
    std::string to_domain = "edge";
    std::string group_name;
    /// Empty keeps the original group name (Houdini New Name default).
    std::string new_name;
    bool keep_original_group = false;
    bool include_only_on_boundary = false;
    bool include_unshared_edges = true;
    bool include_all_unshared_curve_edges = true;
    bool use_connectivity_attribute = false;
    std::string connectivity_attribute = "uv";
    double connectivity_attribute_tolerance = 1e-4;
    bool include_all_primitives_sharing_attribute_boundary_points = false;
    /// When To is Primitives / Edges / Vertices and Boundary is off.
    bool include_only_entirely_contained = true;
    /// When To is Face and Boundary is off.
    bool include_only_primitives_sharing_edge = false;
    bool remove_degenerate_bridges = false;
    /// When To is Point / Face / Vertex: write 0/1 attribute and delete the group.
    bool output_as_integer_attribute = false;
};

data::PcgGeometry group_create(const data::PcgGeometry& input,
                               const GroupCreateOptions& options,
                               const data::PcgGeometry* bounding = nullptr);
data::PcgGeometry group_combine(const data::PcgGeometry& input, const GroupCombineOptions& options);
data::PcgGeometry face_group_by_normal(const data::PcgGeometry& input,
                                       const FaceGroupByNormalOptions& options);
data::PcgGeometry group_promote(const data::PcgGeometry& input, const GroupPromoteOptions& options);

/// Houdini Group Delete SOP: remove named groups without deleting geometry elements.
struct GroupDeleteRule {
    bool enabled = true;
    /// any | points | primitives | edges | vertices
    std::string group_type = "any";
    /// Space-separated group names; supports * wildcards.
    std::string group_names;
};

struct GroupDeleteOptions {
    std::vector<GroupDeleteRule> rules;
    bool delete_unused_groups = false;
};

std::vector<GroupDeleteRule> parse_group_delete_rules(const nlohmann::json& data);
data::PcgGeometry group_delete(const data::PcgGeometry& input, const GroupDeleteOptions& options);

} // namespace pcg::internal::elements
