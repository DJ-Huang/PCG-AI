#include "elements/lot_subdivision_algorithms.hpp"

#include "elements/element_utils.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace pcg::internal::elements {
namespace {

using data::PcgGeometry;
using data::PcgVec3;

constexpr double kEps = 1.0e-9;

struct Vec2 {
    double x = 0.0;
    double y = 0.0;
};

struct FacePlane {
    PcgVec3 origin{};
    PcgVec3 axis_u{};
    PcgVec3 axis_v{};
    PcgVec3 normal{};
};

uint32_t next_rng(uint32_t& state)
{
    state = state * 1664525u + 1013904223u;
    return state;
}

double rand01(uint32_t& state)
{
    return (next_rng(state) & 0xffffffu) / static_cast<double>(0xffffffu);
}

PcgVec3 sub(const PcgVec3& a, const PcgVec3& b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

PcgVec3 add(const PcgVec3& a, const PcgVec3& b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

PcgVec3 scale(const PcgVec3& v, double s)
{
    return {v.x * s, v.y * s, v.z * s};
}

double dot(const PcgVec3& a, const PcgVec3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

PcgVec3 cross(const PcgVec3& a, const PcgVec3& b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

double length(const PcgVec3& v)
{
    return std::sqrt(dot(v, v));
}

PcgVec3 normalize(const PcgVec3& v)
{
    const double len = length(v);
    if (len <= kEps)
        return {1.0, 0.0, 0.0};
    return scale(v, 1.0 / len);
}

double dist2(const Vec2& a, const Vec2& b)
{
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    return dx * dx + dy * dy;
}

bool build_face_plane(const std::vector<PcgVec3>& points, FacePlane& plane)
{
    if (points.size() < 3)
        return false;

    plane.origin = points[0];
    PcgVec3 normal{0.0, 0.0, 0.0};
    for (size_t i = 0; i < points.size(); ++i) {
        const PcgVec3& a = points[i];
        const PcgVec3& b = points[(i + 1) % points.size()];
        normal.x += (a.y - b.y) * (a.z + b.z);
        normal.y += (a.z - b.z) * (a.x + b.x);
        normal.z += (a.x - b.x) * (a.y + b.y);
    }
    if (length(normal) <= kEps) {
        const PcgVec3 e0 = sub(points[1], points[0]);
        PcgVec3 e1 = sub(points[2], points[0]);
        for (size_t i = 2; i < points.size() && length(cross(e0, e1)) <= kEps; ++i)
            e1 = sub(points[i], points[0]);
        normal = cross(e0, e1);
    }
    if (length(normal) <= kEps)
        return false;

    plane.normal = normalize(normal);
    PcgVec3 axis_u = sub(points[1], points[0]);
    if (length(axis_u) <= kEps) {
        axis_u = {1.0, 0.0, 0.0};
        if (std::abs(dot(axis_u, plane.normal)) > 0.9)
            axis_u = {0.0, 0.0, 1.0};
    }
    plane.axis_u = normalize(sub(axis_u, scale(plane.normal, dot(axis_u, plane.normal))));
    plane.axis_v = normalize(cross(plane.normal, plane.axis_u));
    return true;
}

Vec2 to_2d(const FacePlane& plane, const PcgVec3& p)
{
    const PcgVec3 d = sub(p, plane.origin);
    return {dot(d, plane.axis_u), dot(d, plane.axis_v)};
}

PcgVec3 to_3d(const FacePlane& plane, const Vec2& p)
{
    return add(plane.origin, add(scale(plane.axis_u, p.x), scale(plane.axis_v, p.y)));
}

double polygon_size(const std::vector<Vec2>& poly)
{
    if (poly.size() < 2)
        return 0.0;
    double min_x = poly[0].x;
    double max_x = poly[0].x;
    double min_y = poly[0].y;
    double max_y = poly[0].y;
    for (const auto& p : poly) {
        min_x = std::min(min_x, p.x);
        max_x = std::max(max_x, p.x);
        min_y = std::min(min_y, p.y);
        max_y = std::max(max_y, p.y);
    }
    // Shortest planar AABB extent — thin box sides stay below minSize;
    // square lots keep cutting until both axes shrink past the threshold.
    return std::min(max_x - min_x, max_y - min_y);
}

double cut_fraction(double irregularity, uint32_t& rng)
{
    const double clamped = std::clamp(irregularity, 0.0, 1.0);
    const double jitter = (rand01(rng) * 2.0 - 1.0) * 0.5 * clamped;
    return std::clamp(0.5 + jitter, 0.15, 0.85);
}

bool clip_polygon(const std::vector<Vec2>& poly, const Vec2& n, double c, std::vector<Vec2>& out)
{
    out.clear();
    if (poly.size() < 3)
        return false;

    auto side = [&](const Vec2& p) { return n.x * p.x + n.y * p.y - c; };

    for (size_t i = 0; i < poly.size(); ++i) {
        const Vec2& cur = poly[i];
        const Vec2& nxt = poly[(i + 1) % poly.size()];
        const double sc = side(cur);
        const double sn = side(nxt);
        const bool cur_in = sc >= -kEps;
        const bool nxt_in = sn >= -kEps;
        if (cur_in)
            out.push_back(cur);
        if (cur_in != nxt_in) {
            const double den = sc - sn;
            if (std::abs(den) > kEps) {
                const double t = sc / den;
                out.push_back({cur.x + (nxt.x - cur.x) * t, cur.y + (nxt.y - cur.y) * t});
            }
        }
    }

    // Drop near-duplicates.
    std::vector<Vec2> cleaned;
    cleaned.reserve(out.size());
    for (const auto& p : out) {
        if (cleaned.empty() || dist2(cleaned.back(), p) > 1.0e-14)
            cleaned.push_back(p);
    }
    if (cleaned.size() >= 2 && dist2(cleaned.front(), cleaned.back()) <= 1.0e-14)
        cleaned.pop_back();
    out.swap(cleaned);
    return out.size() >= 3;
}

bool bipartition_polygon(const std::vector<Vec2>& poly,
                         const LotSubdivisionOptions& options,
                         uint32_t& rng,
                         std::vector<Vec2>& left,
                         std::vector<Vec2>& right)
{
    left.clear();
    right.clear();
    if (poly.size() < 3)
        return false;

    double min_x = poly[0].x;
    double max_x = poly[0].x;
    double min_y = poly[0].y;
    double max_y = poly[0].y;
    for (const auto& p : poly) {
        min_x = std::min(min_x, p.x);
        max_x = std::max(max_x, p.x);
        min_y = std::min(min_y, p.y);
        max_y = std::max(max_y, p.y);
    }
    const double width = max_x - min_x;
    const double height = max_y - min_y;
    if (width <= kEps && height <= kEps)
        return false;

    const double t = cut_fraction(options.irregularity, rng);
    Vec2 cut_n{};
    double cut_c = 0.0;

    if (options.alignment == "boundingBox") {
        if (width >= height) {
            cut_n = {1.0, 0.0};
            cut_c = min_x + width * t;
        } else {
            cut_n = {0.0, 1.0};
            cut_c = min_y + height * t;
        }
    } else {
        // longestEdge: cut across the polygon with a line perpendicular to the
        // longest boundary edge. The 2D clip normal must be parallel to that
        // edge (so the clip line is perpendicular to it). Using a normal
        // perpendicular to the edge makes the clip line coincide with the
        // boundary and bipartition always fails (1 lot forever).
        size_t longest = 0;
        double longest_len2 = -1.0;
        for (size_t i = 0; i < poly.size(); ++i) {
            const double len2 = dist2(poly[i], poly[(i + 1) % poly.size()]);
            if (len2 > longest_len2) {
                longest_len2 = len2;
                longest = i;
            }
        }
        const Vec2& a = poly[longest];
        const Vec2& b = poly[(longest + 1) % poly.size()];
        const Vec2 edge{b.x - a.x, b.y - a.y};
        const double edge_len = std::sqrt(std::max(edge.x * edge.x + edge.y * edge.y, 0.0));
        if (edge_len <= kEps)
            return false;
        const Vec2 mid{a.x + edge.x * t, a.y + edge.y * t};
        cut_n = {edge.x / edge_len, edge.y / edge_len};
        cut_c = cut_n.x * mid.x + cut_n.y * mid.y;
    }

    if (!clip_polygon(poly, cut_n, cut_c, left))
        return false;
    if (!clip_polygon(poly, {-cut_n.x, -cut_n.y}, -cut_c, right))
        return false;
    return true;
}

void append_face(PcgGeometry& geo,
                 const FacePlane& plane,
                 const std::vector<Vec2>& poly2d,
                 std::vector<int64_t>& lot_ids,
                 int lot_id)
{
    std::vector<int> face;
    face.reserve(poly2d.size());
    for (const auto& p : poly2d) {
        face.push_back(static_cast<int>(geo.points().size()));
        geo.points_mut().push_back(to_3d(plane, p));
    }
    geo.faces_mut().push_back(std::move(face));
    lot_ids.push_back(lot_id);
}

} // namespace

PcgGeometry lot_subdivide_geometry(const PcgGeometry& input, const LotSubdivisionOptions& options)
{
    PcgGeometry output;
    std::vector<int64_t> lot_ids;

    LotSubdivisionOptions opts = options;
    opts.min_size = std::max(0.0, opts.min_size);
    opts.iterations = std::max(0, opts.iterations);
    opts.irregularity = std::clamp(opts.irregularity, 0.0, 1.0);
    if (opts.alignment != "boundingBox" && opts.alignment != "longestEdge")
        opts.alignment = "longestEdge";

    uint32_t rng = rng_state_from_seed(opts.seed, opts.graph_seed);

    int next_lot_id = 0;
    for (const auto& face : input.faces()) {
        if (face.size() < 3)
            continue;

        std::vector<PcgVec3> ring;
        ring.reserve(face.size());
        for (int idx : face) {
            if (idx < 0 || static_cast<size_t>(idx) >= input.points().size())
                continue;
            ring.push_back(input.points()[static_cast<size_t>(idx)]);
        }
        if (ring.size() < 3)
            continue;

        FacePlane plane;
        if (!build_face_plane(ring, plane))
            continue;

        std::vector<Vec2> poly;
        poly.reserve(ring.size());
        for (const auto& p : ring)
            poly.push_back(to_2d(plane, p));

        struct Lot {
            std::vector<Vec2> poly;
            FacePlane plane;
        };
        std::vector<Lot> current;
        current.push_back({std::move(poly), plane});

        for (int iter = 0; iter < opts.iterations; ++iter) {
            std::vector<Lot> next;
            next.reserve(current.size() * 2);
            for (auto& lot : current) {
                const double size = polygon_size(lot.poly);
                if (size + kEps < opts.min_size) {
                    next.push_back(std::move(lot));
                    continue;
                }
                std::vector<Vec2> left;
                std::vector<Vec2> right;
                if (!bipartition_polygon(lot.poly, opts, rng, left, right)) {
                    next.push_back(std::move(lot));
                    continue;
                }
                next.push_back({std::move(left), lot.plane});
                next.push_back({std::move(right), lot.plane});
            }
            current.swap(next);
        }

        for (auto& lot : current) {
            append_face(output, lot.plane, lot.poly, lot_ids, next_lot_id++);
        }
    }

    if (!lot_ids.empty()) {
        auto& attr = output.attributes().create_int(
            data::AttributeOwner::Primitive, "lotid", 1, {0});
        attr.int_values_mut() = lot_ids;
        for (int i = 0; i < static_cast<int>(lot_ids.size()); ++i)
            output.groups().add(geometry::GroupDomain::Face, "lots", i);
    }

    return output;
}

} // namespace pcg::internal::elements
