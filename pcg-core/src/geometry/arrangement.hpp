#pragma once

// Arrangement + CSG classification for boolean mesh operations.
// After splitting triangles by intersection segments, we build adjacency,
// propagate winding numbers via BFS, and classify sub-triangles per CSG rules.

#include "geometry/imesh.hpp"

#include <string>
#include <vector>

namespace pcg::internal::geometry {

/// Boolean operation type.
enum class BooleanOp {
    Union,
    Intersect,
    Subtract,
    Shatter,
};

/// Input treatment (solid vs surface).
enum class MeshTreatment {
    Solid,
    Surface,
};

/// Detriangulation mode (Houdini-aligned).
enum class DetriangulateMode {
    All,       // Merge all coplanar triangles
    Unchanged, // Only merge non-seam-split triangles
    None,      // Keep all triangles
};

/// Error types for boolean operations.
enum class BooleanErrorType {
    Ok,
    NonManifold,
    PrecisionOverflow,
    TriangleBudgetExceeded,
    SelfIntersectionUnresolved,
    InvalidInput,
};

/// Result of a boolean operation.
struct BooleanResult {
    data::PcgGeometry geometry;
    BooleanErrorType error = BooleanErrorType::Ok;
    std::string message;
    std::vector<int> bad_operand_indices;
};

/// Options for boolean operations.
struct BooleanOptions {
    BooleanOp operation = BooleanOp::Subtract;
    MeshTreatment treat_a_as = MeshTreatment::Solid;
    MeshTreatment treat_b_as = MeshTreatment::Solid;
    bool use_self = false;
    DetriangulateMode detriangulate = DetriangulateMode::All;
    double weld_epsilon = 0.0001;
    int triangle_budget = 500000;
};

/// Output group names (Houdini-aligned).
namespace BooleanGroups {
    constexpr const char* A_INSIDE_B  = "a_inside_b";
    constexpr const char* A_OUTSIDE_B = "a_outside_b";
    constexpr const char* B_INSIDE_A  = "b_inside_a";
    constexpr const char* B_OUTSIDE_A = "b_outside_a";
    constexpr const char* AB_SEAMS    = "ab_seams";
}

/// Per-triangle winding info after arrangement.
struct TriangleWinding {
    int w_a = 0; ///< Winding number w.r.t. operand A
    int w_b = 0; ///< Winding number w.r.t. operand B
    int source = 0; ///< 0=A, 1=B
    bool keep = false; ///< Whether to keep this triangle
    bool is_seam = false; ///< Whether this triangle is on the A-B boundary
    bool is_coplanar = false; ///< True if produced by coplanar partition
    bool coplanar_inside = false; ///< If is_coplanar: true=inside other operand
};

/// Arrangement: adjacency + winding numbers for a set of split triangles.
struct Arrangement {
    /// For each triangle, the winding numbers and classification.
    std::vector<TriangleWinding> triangles;

    /// Edge adjacency: maps edge_key → list of (tri_index, direction).
    /// direction = +1 if the edge goes v0→v1 in the triangle, -1 if v1→v0.
    std::unordered_map<int64_t, std::vector<std::pair<int, int>>> edge_adjacency;

    /// Build the arrangement from two IMeshes (already split by intersections).
    /// Computes adjacency, winding numbers, and CSG classification.
    void build(const IMesh& mesh_a, const IMesh& mesh_b, const BooleanOptions& opts);

    /// Classify triangles based on winding numbers and CSG operation.
    void classify(const BooleanOptions& opts);

    /// Check manifold validity (PWN condition).
    /// Returns true if the mesh satisfies the piecewise winding number condition.
    bool check_manifold(const IMesh& combined) const;
};

/// Execute the full boolean operation on two geometries.
/// This is the high-level entry point called by the BooleanMeshElement.
BooleanResult execute_boolean(const data::PcgGeometry& a,
                              const data::PcgGeometry& b,
                              const BooleanOptions& opts);

} // namespace pcg::internal::geometry
