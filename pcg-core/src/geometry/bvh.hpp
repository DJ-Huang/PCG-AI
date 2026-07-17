#pragma once

// BVH (Bounding Volume Hierarchy) for broad-phase triangle intersection.
// Each operand gets its own BVH; cross-tree queries find candidate pairs.

#include "geometry/bmesh.hpp"

#include <vector>
#include <utility>

namespace pcg::internal::geometry {

struct AABB {
    Vec3 min;
    Vec3 max;

    AABB() : min{1e30, 1e30, 1e30}, max{-1e30, -1e30, -1e30} {}

    void expand(const Vec3& v);
    void expand(const AABB& other);
    bool overlaps(const AABB& other) const;
    Vec3 center() const;
};

struct BVHTriangle {
    int tri_index;
    AABB bounds;
};

struct BVHNode {
    AABB bounds;
    int left = -1;   // child index, -1 if leaf
    int right = -1;
    std::vector<int> tri_indices; // only for leaf nodes

    bool is_leaf() const { return left < 0 && right < 0; }
};

class BVH {
public:
    /// Build BVH from a list of triangles.
    void build(const std::vector<BVHTriangle>& tris);

    /// Find all overlapping triangle pairs between this BVH and another.
    /// Returns pairs of (tri_index_a, tri_index_b).
    std::vector<std::pair<int, int>> find_overlaps(const BVH& other) const;

    /// Get all triangle indices whose AABB overlaps the given box.
    std::vector<int> query_box(const AABB& box) const;

    /// Get all triangle indices whose AABB is hit by a forward ray.
    std::vector<int> query_ray(const Vec3& origin, const Vec3& direction) const;

    bool empty() const { return nodes_.empty(); }
    size_t node_count() const { return nodes_.size(); }

private:
    std::vector<BVHNode> nodes_;
    std::vector<BVHTriangle> tris_;

    int build_recursive(std::vector<int>& tri_indices, int depth);
    void query_recursive(int node_idx, const AABB& box, std::vector<int>& out) const;
    void query_ray_recursive(int node_idx, const Vec3& origin, const Vec3& direction,
                             std::vector<int>& out) const;
    void cross_query_recursive(int node_a, const BVH& other, int node_b,
                               std::vector<std::pair<int, int>>& out) const;
};

} // namespace pcg::internal::geometry
