// BVH implementation: bounding volume hierarchy for broad-phase collision.

#include "geometry/bvh.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace pcg::internal::geometry {

// ── AABB ───────────────────────────────────────────────────

void AABB::expand(const Vec3& v)
{
    min.x = std::min(min.x, v.x);
    min.y = std::min(min.y, v.y);
    min.z = std::min(min.z, v.z);
    max.x = std::max(max.x, v.x);
    max.y = std::max(max.y, v.y);
    max.z = std::max(max.z, v.z);
}

void AABB::expand(const AABB& other)
{
    expand(other.min);
    expand(other.max);
}

bool AABB::overlaps(const AABB& other) const
{
    return (min.x <= other.max.x && max.x >= other.min.x) &&
           (min.y <= other.max.y && max.y >= other.min.y) &&
           (min.z <= other.max.z && max.z >= other.min.z);
}

Vec3 AABB::center() const
{
    return {(min.x + max.x) * 0.5, (min.y + max.y) * 0.5, (min.z + max.z) * 0.5};
}

// ── BVH ────────────────────────────────────────────────────

namespace {

constexpr int kMaxLeafSize = 8;
constexpr int kMaxDepth = 32;

} // anonymous namespace

void BVH::build(const std::vector<BVHTriangle>& tris)
{
    tris_ = tris;
    nodes_.clear();

    if (tris_.empty()) return;

    std::vector<int> tri_indices(tris_.size());
    for (size_t i = 0; i < tris_.size(); ++i) {
        tri_indices[i] = static_cast<int>(i);
    }

    build_recursive(tri_indices, 0);
}

int BVH::build_recursive(std::vector<int>& tri_indices, int depth)
{
    if (tri_indices.empty()) return -1;

    int node_idx = static_cast<int>(nodes_.size());
    nodes_.push_back(BVHNode{});

    BVHNode& node = nodes_[node_idx];

    // Compute bounds for this node
    for (int idx : tri_indices) {
        node.bounds.expand(tris_[idx].bounds);
    }

    // Leaf node
    if (static_cast<int>(tri_indices.size()) <= kMaxLeafSize || depth >= kMaxDepth) {
        node.tri_indices = std::move(tri_indices);
        return node_idx;
    }

    // Choose split axis (longest extent)
    Vec3 extent{
        node.bounds.max.x - node.bounds.min.x,
        node.bounds.max.y - node.bounds.min.y,
        node.bounds.max.z - node.bounds.min.z,
    };
    int axis = 0;
    if (extent.y > extent.x) axis = 1;
    if (extent.z > extent.y && extent.z > extent.x) axis = 2;

    // Sort by centroid along split axis
    std::sort(tri_indices.begin(), tri_indices.end(), [&](int a, int b) {
        Vec3 ca = tris_[a].bounds.center();
        Vec3 cb = tris_[b].bounds.center();
        if (axis == 0) return ca.x < cb.x;
        if (axis == 1) return ca.y < cb.y;
        return ca.z < cb.z;
    });

    // Split at median
    int mid = static_cast<int>(tri_indices.size()) / 2;
    std::vector<int> left_tris(tri_indices.begin(), tri_indices.begin() + mid);
    std::vector<int> right_tris(tri_indices.begin() + mid, tri_indices.end());

    int left_idx = build_recursive(left_tris, depth + 1);
    int right_idx = build_recursive(right_tris, depth + 1);

    // Re-fetch node reference (vector may have reallocated)
    nodes_[node_idx].left = left_idx;
    nodes_[node_idx].right = right_idx;

    return node_idx;
}

std::vector<int> BVH::query_box(const AABB& box) const
{
    std::vector<int> result;
    if (nodes_.empty()) return result;
    query_recursive(0, box, result);
    return result;
}

void BVH::query_recursive(int node_idx, const AABB& box, std::vector<int>& out) const
{
    const BVHNode& node = nodes_[node_idx];
    if (!node.bounds.overlaps(box)) return;

    if (node.is_leaf()) {
        for (int idx : node.tri_indices) {
            if (tris_[idx].bounds.overlaps(box)) {
                out.push_back(tris_[idx].tri_index);
            }
        }
    } else {
        if (node.left >= 0) query_recursive(node.left, box, out);
        if (node.right >= 0) query_recursive(node.right, box, out);
    }
}

namespace {

bool ray_hits_box(const Vec3& origin, const Vec3& direction, const AABB& box)
{
    double t_min = 0.0;
    double t_max = std::numeric_limits<double>::infinity();
    const double origins[3] = {origin.x, origin.y, origin.z};
    const double directions[3] = {direction.x, direction.y, direction.z};
    const double mins[3] = {box.min.x, box.min.y, box.min.z};
    const double maxs[3] = {box.max.x, box.max.y, box.max.z};
    for (int axis = 0; axis < 3; ++axis) {
        if (std::fabs(directions[axis]) <= 1e-20) {
            if (origins[axis] < mins[axis] || origins[axis] > maxs[axis])
                return false;
            continue;
        }
        const double inv_direction = 1.0 / directions[axis];
        double near_t = (mins[axis] - origins[axis]) * inv_direction;
        double far_t = (maxs[axis] - origins[axis]) * inv_direction;
        if (near_t > far_t) std::swap(near_t, far_t);
        t_min = std::max(t_min, near_t);
        t_max = std::min(t_max, far_t);
        if (t_max < t_min) return false;
    }
    return t_max >= 0.0;
}

} // anonymous namespace

std::vector<int> BVH::query_ray(const Vec3& origin, const Vec3& direction) const
{
    std::vector<int> result;
    if (!nodes_.empty())
        query_ray_recursive(0, origin, direction, result);
    return result;
}

void BVH::query_ray_recursive(int node_idx, const Vec3& origin, const Vec3& direction,
                              std::vector<int>& out) const
{
    const BVHNode& node = nodes_[node_idx];
    if (!ray_hits_box(origin, direction, node.bounds)) return;

    if (node.is_leaf()) {
        for (int idx : node.tri_indices) {
            if (ray_hits_box(origin, direction, tris_[idx].bounds))
                out.push_back(tris_[idx].tri_index);
        }
        return;
    }
    if (node.left >= 0) query_ray_recursive(node.left, origin, direction, out);
    if (node.right >= 0) query_ray_recursive(node.right, origin, direction, out);
}

std::vector<std::pair<int, int>> BVH::find_overlaps(const BVH& other) const
{
    std::vector<std::pair<int, int>> result;
    if (nodes_.empty() || other.nodes_.empty()) return result;
    cross_query_recursive(0, other, 0, result);
    return result;
}

void BVH::cross_query_recursive(int node_a, const BVH& other, int node_b,
                                 std::vector<std::pair<int, int>>& out) const
{
    const BVHNode& a = nodes_[node_a];
    const BVHNode& b = other.nodes_[node_b];

    if (!a.bounds.overlaps(b.bounds)) return;

    if (a.is_leaf() && b.is_leaf()) {
        for (int ia : a.tri_indices) {
            for (int ib : b.tri_indices) {
                if (tris_[ia].bounds.overlaps(other.tris_[ib].bounds)) {
                    out.emplace_back(tris_[ia].tri_index, other.tris_[ib].tri_index);
                }
            }
        }
    } else if (a.is_leaf()) {
        if (b.left >= 0) cross_query_recursive(node_a, other, b.left, out);
        if (b.right >= 0) cross_query_recursive(node_a, other, b.right, out);
    } else if (b.is_leaf()) {
        if (a.left >= 0) cross_query_recursive(a.left, other, node_b, out);
        if (a.right >= 0) cross_query_recursive(a.right, other, node_b, out);
    } else {
        // Descend the larger node
        Vec3 ext_a{
            a.bounds.max.x - a.bounds.min.x,
            a.bounds.max.y - a.bounds.min.y,
            a.bounds.max.z - a.bounds.min.z,
        };
        Vec3 ext_b{
            b.bounds.max.x - b.bounds.min.x,
            b.bounds.max.y - b.bounds.min.y,
            b.bounds.max.z - b.bounds.min.z,
        };
        double vol_a = ext_a.x * ext_a.y * ext_a.z;
        double vol_b = ext_b.x * ext_b.y * ext_b.z;

        if (vol_a >= vol_b) {
            if (a.left >= 0) cross_query_recursive(a.left, other, node_b, out);
            if (a.right >= 0) cross_query_recursive(a.right, other, node_b, out);
        } else {
            if (b.left >= 0) cross_query_recursive(node_a, other, b.left, out);
            if (b.right >= 0) cross_query_recursive(node_a, other, b.right, out);
        }
    }
}

} // namespace pcg::internal::geometry
