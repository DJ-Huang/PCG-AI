#include "elements/bevel_blender.hpp"
#include "elements/bevel_diag.hpp"
#include "geometry/bmesh.hpp"

#include "data/pcg_geometry.hpp"
#include "geometry/group_table.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <unordered_map>

namespace pcg::internal::elements::bevel {

namespace {

} // namespace

// ═══════════════════════════════════════════════════════════════════════════════
// Section 1: Vec3 Math
// ═══════════════════════════════════════════════════════════════════════════════

Vec3 v3(double x, double y, double z) { return {x, y, z}; }
Vec3 add(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 sub(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 scale(const Vec3& v, double s) { return {v.x * s, v.y * s, v.z * s}; }
Vec3 negate(const Vec3& v) { return {-v.x, -v.y, -v.z}; }
double dot(const Vec3& a, const Vec3& b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x};
}
double length(const Vec3& v) { return std::sqrt(dot(v, v)); }
double length_squared(const Vec3& v) { return dot(v, v); }

Vec3 normalize(const Vec3& v) {
    double len = length(v);
    if (len <= 1e-15) return {0, 0, 0};
    return scale(v, 1.0 / len);
}

Vec3 mid(const Vec3& a, const Vec3& b) { return {(a.x+b.x)*0.5, (a.y+b.y)*0.5, (a.z+b.z)*0.5}; }

Vec3 madd(const Vec3& base, const Vec3& add_v, double s) {
    return {base.x + add_v.x*s, base.y + add_v.y*s, base.z + add_v.z*s};
}

bool nearly_parallel(const Vec3& d1, const Vec3& d2) {
    double d = std::abs(dot(normalize(d1), normalize(d2)));
    return d > 1.0 - BEVEL_EPSILON_ANG;
}

bool nearly_parallel_normalized(const Vec3& d1, const Vec3& d2) {
    double d = std::abs(dot(d1, d2));
    return d > 1.0 - BEVEL_EPSILON_ANG;
}

double safe_divide(double a, double b) {
    return (std::abs(b) < BEVEL_EPSILON) ? 0.0 : a / b;
}

double angle_v3v3(const Vec3& a, const Vec3& b) {
    double d = dot(a, b) / std::sqrt(length_squared(a) * length_squared(b));
    return std::acos(std::clamp(d, -1.0, 1.0));
}

double angle_normalized_v3v3(const Vec3& a, const Vec3& b) {
    return std::acos(std::clamp(dot(a, b), -1.0, 1.0));
}

double angle_v3v3v3(const Vec3& va, const Vec3& vb, const Vec3& vc) {
    Vec3 d1 = normalize(sub(va, vb));
    Vec3 d2 = normalize(sub(vc, vb));
    return angle_normalized_v3v3(d1, d2);
}

// ── Line/Plane intersection ──────────────────────────────────────────────────

int isect_line_line_v3(const Vec3& p1, const Vec3& p2,
                       const Vec3& p3, const Vec3& p4,
                       Vec3& r_i1, Vec3& r_i2)
{
    Vec3 d1 = sub(p2, p1);
    Vec3 d2 = sub(p4, p3);
    Vec3 cross_d = cross(d1, d2);

    double cross_len_sq = length_squared(cross_d);
    if (cross_len_sq < 1e-20) {
        // Lines are parallel
        Vec3 diff = sub(p3, p1);
        if (length_squared(cross(diff, d1)) < 1e-20) {
            // Collinear
            r_i1 = p1;
            return 2;
        }
        // Parallel but not collinear — return closest points
        double t = safe_divide(dot(diff, d1), length_squared(d1));
        r_i1 = add(p1, scale(d1, t));
        r_i2 = p3;
        return 0;
    }

    Vec3 diff = sub(p3, p1);
    double t = safe_divide(dot(cross(diff, d2), cross_d), cross_len_sq);
    double u = safe_divide(dot(cross(diff, d1), cross_d), cross_len_sq);

    r_i1 = add(p1, scale(d1, t));
    r_i2 = add(p3, scale(d2, u));

    if (length_squared(sub(r_i1, r_i2)) < 1e-12)
        return 1;
    return 0;
}

bool isect_line_plane_v3(Vec3& isect_co,
                         const Vec3& line_a, const Vec3& line_b,
                         const Vec3& plane_co, const Vec3& plane_no)
{
    Vec3 line_dir = sub(line_b, line_a);
    double denom = dot(plane_no, line_dir);
    if (std::abs(denom) < 1e-12)
        return false;
    double t = safe_divide(dot(sub(plane_co, line_a), plane_no), denom);
    isect_co = add(line_a, scale(line_dir, t));
    return true;
}

Vec3 closest_to_plane(const Vec3& plane_co, const Vec3& plane_no, const Vec3& pt) {
    double d = dot(sub(pt, plane_co), plane_no);
    return sub(pt, scale(plane_no, d));
}

void plane_from_point_normal(Vec3& r_plane_co, Vec3& r_plane_no,
                             const Vec3& p, const Vec3& normal) {
    r_plane_co = p;
    r_plane_no = normalize(normal);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Section 2: 4x4 Matrix
// ═══════════════════════════════════════════════════════════════════════════════

bool make_unit_square_map(const Vec3& start, const Vec3& middle, const Vec3& end, Mat4& r_map) {
    // Use actual direction vectors (NOT normalized) so superellipse coords in [0,1]
    // map to actual positions between start, middle, and end.
    Vec3 d1 = sub(middle, start);
    Vec3 d2 = sub(end, start);
    Vec3 cross_d = cross(d1, d2);

    if (length_squared(cross_d) < BEVEL_EPSILON_SQ)
        return false;

    Vec3 no = normalize(cross_d);
    // Orthogonalize d2 against d1
    d2 = sub(d2, scale(d1, dot(d2, d1) / length_squared(d1)));

    // Matrix: (0,0)->start, (1,0)->middle, (0,1)->end
    // result = start + x * d1 + y * d2
    r_map.m[0][0] = d1.x; r_map.m[0][1] = d1.y; r_map.m[0][2] = d1.z; r_map.m[0][3] = 0;
    r_map.m[1][0] = d2.x; r_map.m[1][1] = d2.y; r_map.m[1][2] = d2.z; r_map.m[1][3] = 0;
    r_map.m[2][0] = no.x; r_map.m[2][1] = no.y; r_map.m[2][2] = no.z; r_map.m[2][3] = 0;
    r_map.m[3][0] = start.x; r_map.m[3][1] = start.y; r_map.m[3][2] = start.z; r_map.m[3][3] = 1;
    return true;
}

bool invert_m4(const Mat4& src, Mat4& r_inv) {
    // Gaussian elimination for 4x4
    Mat4 m = src;
    std::memset(r_inv.m, 0, sizeof(r_inv.m));
    for (int i = 0; i < 4; i++) r_inv.m[i][i] = 1.0;

    for (int col = 0; col < 4; col++) {
        // Find pivot
        int pivot = -1;
        double max_val = 0.0;
        for (int row = col; row < 4; row++) {
            if (std::abs(m.m[row][col]) > max_val) {
                max_val = std::abs(m.m[row][col]);
                pivot = row;
            }
        }
        if (pivot < 0 || max_val < 1e-15)
            return false;

        // Swap rows
        if (pivot != col) {
            for (int j = 0; j < 4; j++) {
                std::swap(m.m[col][j], m.m[pivot][j]);
                std::swap(r_inv.m[col][j], r_inv.m[pivot][j]);
            }
        }

        // Scale pivot row
        double scale = m.m[col][col];
        for (int j = 0; j < 4; j++) {
            m.m[col][j] /= scale;
            r_inv.m[col][j] /= scale;
        }

        // Eliminate other rows
        for (int row = 0; row < 4; row++) {
            if (row == col) continue;
            double factor = m.m[row][col];
            for (int j = 0; j < 4; j++) {
                m.m[row][j] -= factor * m.m[col][j];
                r_inv.m[row][j] -= factor * r_inv.m[col][j];
            }
        }
    }
    return true;
}

Vec3 mul_m4v3(const Mat4& mat, const Vec3& v) {
    Vec3 result;
    result.x = mat.m[0][0]*v.x + mat.m[1][0]*v.y + mat.m[2][0]*v.z + mat.m[3][0];
    result.y = mat.m[0][1]*v.x + mat.m[1][1]*v.y + mat.m[2][1]*v.z + mat.m[3][1];
    result.z = mat.m[0][2]*v.x + mat.m[1][2]*v.y + mat.m[2][2]*v.z + mat.m[3][2];
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Section 3: Superellipse + Profile Spacing
// ═══════════════════════════════════════════════════════════════════════════════

double superellipse_co(double x, float r, bool rbig) {
    if (r <= 0.0f) return 0.0;
    if (rbig)
        return std::pow(1.0 - std::pow(x, r), 1.0 / r);
    return 1.0 - std::pow(1.0 - std::pow(1.0 - x, r), 1.0 / r);
}

void find_even_superellipse_chords(int seg, float super_r,
                                   std::vector<double>& xvals,
                                   std::vector<double>& yvals)
{
    xvals.resize(seg + 1);
    yvals.resize(seg + 1);

    bool rbig = (super_r > 1.0f);

    xvals[0] = 0.0;
    yvals[0] = 1.0;
    xvals[seg] = 1.0;
    yvals[seg] = 0.0;

    if (seg <= 1)
        return;

    // Iterative search: find x values such that chord lengths are equal
    // Blender uses a binary search approach
    double total_len = 0.0;
    std::vector<double> t_params(seg + 1);
    t_params[0] = 0.0;
    t_params[seg] = 1.0;

    // Start with uniform parameter distribution
    for (int i = 1; i < seg; i++)
        t_params[i] = static_cast<double>(i) / seg;

    // Iteratively adjust for even chord spacing
    for (int iter = 0; iter < 50; iter++) {
        // Compute chord lengths
        std::vector<double> chord_lens(seg);
        total_len = 0.0;
        for (int i = 0; i < seg; i++) {
            double x0 = t_params[i];
            double x1 = t_params[i + 1];
            double y0 = superellipse_co(x0, super_r, rbig);
            double y1 = superellipse_co(x1, super_r, rbig);
            // Mirror if not rbig
            if (!rbig) {
                y0 = 1.0 - y0;
                y1 = 1.0 - y1;
            }
            double dx = x1 - x0;
            double dy = y1 - y0;
            chord_lens[i] = std::sqrt(dx * dx + dy * dy);
            total_len += chord_lens[i];
        }

        // Target: each chord should be total_len / seg
        double target = total_len / seg;

        // Adjust parameters using Newton-like correction
        double max_err = 0.0;
        for (int i = 1; i < seg; i++) {
            // Compute cumulative length up to i
            double cum_len = 0.0;
            for (int j = 0; j < i; j++)
                cum_len += chord_lens[j];

            double target_cum = target * i;
            double error = target_cum - cum_len;

            // Estimate derivative of cumulative length w.r.t. t_params[i]
            double dx = t_params[i + 1] - t_params[i - 1];
            if (std::abs(dx) > 1e-10) {
                double y_prev = superellipse_co(t_params[i - 1], super_r, rbig);
                double y_next = superellipse_co(t_params[i + 1], super_r, rbig);
                if (!rbig) {
                    y_prev = 1.0 - y_prev;
                    y_next = 1.0 - y_next;
                }
                double dy = y_next - y_prev;
                double dl_dt = std::sqrt(dx * dx + dy * dy) / dx;
                if (dl_dt > 1e-10)
                    t_params[i] += error / dl_dt * 0.5; // damped
            }

            t_params[i] = std::clamp(t_params[i], 1e-10, 1.0 - 1e-10);
            max_err = std::max(max_err, std::abs(error));
        }

        if (max_err < 1e-8)
            break;
    }

    // Convert parameters to x/y values
    for (int i = 0; i <= seg; i++) {
        double t = t_params[i];
        double y = superellipse_co(t, super_r, rbig);
        if (!rbig) y = 1.0 - y;

        // For rbig: x = t, y = superellipse_co(t, r, true)
        // For !rbig: x = 1-t, y = 1 - superellipse_co(1-t, r, false)
        // Actually, Blender's convention:
        // rbig (r > 1): x goes 0→1, y goes 1→0
        // !rbig (r <= 1): mirror
        if (rbig) {
            xvals[i] = t;
            yvals[i] = y;
        } else {
            xvals[i] = 1.0 - t;
            yvals[i] = y;
        }
    }
}

float find_profile_fullness(int seg, float super_r, float profile_param) {
    static const float circle_fullness[] = {
        0.0f,   // seg == 1
        0.559f, // 2
        0.642f, // 3
        0.551f, // 4
        0.646f, // 5
        0.624f, // 6
        0.646f, // 7
        0.619f, // 8
        0.647f, // 9
        0.639f, // 10
        0.647f, // 11
    };

    if (super_r == PRO_LINE_R)
        return 0.0f;
    if (super_r == PRO_CIRCLE_R && seg >= 1 && seg <= 11)
        return circle_fullness[seg - 1];

    // Linear regression fit
    if (seg % 2 == 0)
        return 2.4506f * profile_param - 0.00000300f * seg - 0.6266f;
    return 2.3635f * profile_param + 0.000152f * seg - 0.6060f;
}

void set_profile_spacing(int seg, float super_r, ProfileSpacing& pro_spacing) {
    if (seg <= 1) {
        pro_spacing.xvals.clear();
        pro_spacing.yvals.clear();
        pro_spacing.xvals_2.clear();
        pro_spacing.yvals_2.clear();
        pro_spacing.seg_2 = 0;
        return;
    }

    // seg_2 = power of 2 >= seg, at least 4
    int seg_2 = 4;
    while (seg_2 < seg) seg_2 *= 2;
    pro_spacing.seg_2 = seg_2;

    find_even_superellipse_chords(seg, super_r, pro_spacing.xvals, pro_spacing.yvals);

    if (seg_2 == seg) {
        pro_spacing.xvals_2 = pro_spacing.xvals;
        pro_spacing.yvals_2 = pro_spacing.yvals;
    } else {
        find_even_superellipse_chords(seg_2, super_r, pro_spacing.xvals_2, pro_spacing.yvals_2);
    }

    pro_spacing.fullness = find_profile_fullness(seg, super_r, 0.5f);
}

Vec3 get_profile_point(const Profile& pro, int i, int nseg, int bp_seg) {
    if (bp_seg == 1) {
        return (i == 0) ? pro.start : pro.end;
    }

    if (nseg == bp_seg && !pro.prof_co.empty()) {
        return pro.prof_co[static_cast<size_t>(i)];
    }

    // Subsample from prof_co_2
    if (!pro.prof_co_2.empty()) {
        int seg_2 = static_cast<int>(pro.prof_co_2.size()) - 1;
        int subsample_spacing = seg_2 / nseg;
        return pro.prof_co_2[static_cast<size_t>(i * subsample_spacing)];
    }

    // Fallback: linear interpolation
    double t = static_cast<double>(i) / nseg;
    return add(scale(pro.start, 1.0 - t), scale(pro.end, t));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Section 4: Topology Building
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

bool bevel_cancel_requested(const BevelParams& bp)
{
    return bp.is_cancel_requested && bp.is_cancel_requested();
}

int64_t edge_key(int a, int b) {
    return a < b ? static_cast<int64_t>(a) * 1000000 + b
                 : static_cast<int64_t>(b) * 1000000 + a;
}

struct WeldedMesh {
    std::vector<Vec3> positions;
    std::vector<std::array<int, 3>> triangles;
};

WeldedMesh weld_mesh(const data::PcgMeshData& mesh) {
    WeldedMesh welded;
    std::unordered_map<std::string, int> index_by_key;
    std::vector<int> remap(mesh.vertices().size(), -1);

    auto quantize = [](double v) -> int64_t {
        return static_cast<int64_t>(std::llround(v / 1e-6));
    };

    for (size_t i = 0; i < mesh.vertices().size(); ++i) {
        const auto& v = mesh.vertices()[i];
        Vec3 p{v.x, v.y, v.z};
        std::string key = std::to_string(quantize(p.x)) + ',' +
                          std::to_string(quantize(p.y)) + ',' +
                          std::to_string(quantize(p.z));
        auto it = index_by_key.find(key);
        if (it == index_by_key.end()) {
            int idx = static_cast<int>(welded.positions.size());
            welded.positions.push_back(p);
            index_by_key[key] = idx;
            remap[i] = idx;
        } else {
            remap[i] = it->second;
        }
    }

    for (size_t i = 0; i + 2 < mesh.triangles().size(); i += 3) {
        welded.triangles.push_back({
            remap[static_cast<size_t>(mesh.triangles()[i])],
            remap[static_cast<size_t>(mesh.triangles()[i + 1])],
            remap[static_cast<size_t>(mesh.triangles()[i + 2])]
        });
    }

    return welded;
}

Vec3 tri_normal(const WeldedMesh& mesh, int tri_index) {
    const auto& tri = mesh.triangles[static_cast<size_t>(tri_index)];
    const Vec3& a = mesh.positions[static_cast<size_t>(tri[0])];
    const Vec3& b = mesh.positions[static_cast<size_t>(tri[1])];
    const Vec3& c = mesh.positions[static_cast<size_t>(tri[2])];
    return normalize(cross(sub(b, a), sub(c, a)));
}

Vec3 vertex_normal(const WeldedMesh& mesh, int v_idx) {
    Vec3 n{0, 0, 0};
    for (size_t ti = 0; ti < mesh.triangles.size(); ++ti) {
        const auto& tri = mesh.triangles[ti];
        bool has_v = false;
        for (int idx : tri) {
            if (idx == v_idx) { has_v = true; break; }
        }
        if (!has_v) continue;
        n = add(n, tri_normal(mesh, static_cast<int>(ti)));
    }
    return normalize(n);
}

Vec3 vertex_normal_from_data(const std::vector<Vec3>& positions,
                              const std::vector<std::array<int, 3>>& triangles,
                              int v_idx) {
    Vec3 n{0, 0, 0};
    for (const auto& tri : triangles) {
        bool has_v = false;
        for (int idx : tri) {
            if (idx == v_idx) { has_v = true; break; }
        }
        if (!has_v) continue;
        n = add(n, tri_normal({positions, triangles}, static_cast<int>(&tri - &triangles[0])));
    }
    return normalize(n);
}

// Build edge→face adjacency: for each edge, store the two face indices
std::unordered_map<int64_t, std::array<int, 2>> build_edge_faces(const WeldedMesh& mesh) {
    std::unordered_map<int64_t, std::array<int, 2>> edge_faces;
    for (size_t ti = 0; ti < mesh.triangles.size(); ++ti) {
        const auto& tri = mesh.triangles[ti];
        for (int e = 0; e < 3; ++e) {
            int a = tri[e];
            int b = tri[(e + 1) % 3];
            int64_t key = edge_key(a, b);
            auto& faces = edge_faces[key];
            if (faces[0] == 0 && faces[1] == 0) {
                // Default {0,0} → first insertion: store ti as first face
                faces[0] = static_cast<int>(ti);
                faces[1] = -1;
            } else if (faces[1] == -1) {
                // Second face for this edge
                faces[1] = static_cast<int>(ti);
            }
            // If both faces already set (3+ triangles per edge, non-manifold), ignore
        }
    }
    return edge_faces;
}

bool is_hard_edge(const std::vector<Vec3>& positions,
                 const std::vector<std::array<int, 3>>& triangles,
                 const std::unordered_map<int64_t, std::array<int, 2>>& edge_faces,
                 int64_t key, double cos_limit)
{
    auto it = edge_faces.find(key);
    if (it == edge_faces.end()) return true;

    const auto& faces = it->second;
    if (faces[0] < 0 || faces[1] < 0) return true;

    Vec3 n0 = tri_normal({positions, triangles}, faces[0]);
    Vec3 n1 = tri_normal({positions, triangles}, faces[1]);
    return dot(n0, n1) < cos_limit;
}

// Get all edges around a vertex (CCW order)
struct VertEdge {
    int other_v;
    int64_t edge_key;
    int edge_v0; // vertex at this end
    int edge_v1; // vertex at other end
};

std::vector<VertEdge> get_bmesh_vert_edges(int v_idx, const geometry::BMesh& bmesh)
{
    std::vector<VertEdge> edges;
    for (const auto& entry : bmesh.edges) {
        const geometry::BMeshEdge& edge = entry.second;
        if (edge.v0 != v_idx && edge.v1 != v_idx)
            continue;

        const int other = (edge.v0 == v_idx) ? edge.v1 : edge.v0;
        edges.push_back({other, entry.first, v_idx, other});
    }
    return edges;
}

// Forward declaration for fallback
std::vector<VertEdge> sort_ccw(
    const std::vector<VertEdge>& edges,
    const std::vector<Vec3>& positions,
    const std::vector<std::array<int, 3>>& triangles,
    int v_idx);

/// Sort edges in CCW order around vertex using BMesh disk cycle (face-loop traversal).
/// Blender BM_vert_disk_begin / BM_disk_edge_next equivalent: walks
/// edge → face → next edge in face loop → next face, producing ordering that is
/// guaranteed consistent with BMesh face loop direction. Falls back to sort_ccw
/// for non-manifold vertices where the disk cycle cannot complete.
std::vector<VertEdge> sort_edges_disk_cycle(
    int v_idx,
    const std::vector<VertEdge>& edges,
    const geometry::BMesh& bmesh,
    const std::vector<Vec3>& positions,
    const std::vector<std::array<int, 3>>& triangles)
{
    if (edges.size() <= 1)
        return edges;

    // Build a lookup: other_v → VertEdge index
    std::unordered_map<int, size_t> edge_by_other;
    for (size_t i = 0; i < edges.size(); ++i)
        edge_by_other[edges[i].other_v] = i;

    // Track which edges have been placed
    std::vector<bool> placed(edges.size(), false);
    std::vector<VertEdge> result;
    result.reserve(edges.size());

    // Find a starting edge that has a face on at least one side
    size_t start_idx = 0;
    for (size_t i = 0; i < edges.size(); ++i) {
        const auto it = bmesh.edges.find(edges[i].edge_key);
        if (it != bmesh.edges.end() && it->second.face0 >= 0) {
            start_idx = i;
            break;
        }
    }

    // Walk the disk cycle from a starting edge+face
    auto walk = [&](size_t start, int start_face) {
        size_t cur = start;
        int cur_face = start_face;
        do {
            if (placed[cur])
                break;
            placed[cur] = true;
            result.push_back(edges[cur]);

            // Find next edge: in cur_face's loop, after v_idx
            const auto& loop = bmesh.faces[static_cast<size_t>(cur_face)].verts;
            const int n = static_cast<int>(loop.size());
            int v_pos = -1;
            for (int i = 0; i < n; ++i) {
                if (loop[static_cast<size_t>(i)] == v_idx) {
                    v_pos = i;
                    break;
                }
            }
            if (v_pos < 0)
                break;

            int next_v = loop[static_cast<size_t>((v_pos + 1) % n)];
            auto next_it = edge_by_other.find(next_v);
            if (next_it == edge_by_other.end())
                break;

            size_t next_idx = next_it->second;
            const auto next_edge_entry = bmesh.edges.find(edges[next_idx].edge_key);
            if (next_edge_entry == bmesh.edges.end())
                break;

            // The next face is the other face on this edge (not cur_face)
            const auto& ne = next_edge_entry->second;
            int next_face = (ne.face0 == cur_face) ? ne.face1 : ne.face0;
            if (next_face < 0)
                break;

            cur = next_idx;
            cur_face = next_face;
        } while (cur != start);
    };

    // Walk forward from start edge using face0
    {
        const auto it = bmesh.edges.find(edges[start_idx].edge_key);
        if (it != bmesh.edges.end() && it->second.face0 >= 0)
            walk(start_idx, it->second.face0);
    }

    // If not all edges placed, walk backward from start using face1
    if (result.size() < edges.size()) {
        const auto it = bmesh.edges.find(edges[start_idx].edge_key);
        if (it != bmesh.edges.end() && it->second.face1 >= 0) {
            // Reverse walk: find the edge before start in face1's loop
            const auto& loop = bmesh.faces[static_cast<size_t>(it->second.face1)].verts;
            const int n = static_cast<int>(loop.size());
            int v_pos = -1;
            for (int i = 0; i < n; ++i) {
                if (loop[static_cast<size_t>(i)] == v_idx) {
                    v_pos = i;
                    break;
                }
            }
            if (v_pos >= 0) {
                int prev_v = loop[static_cast<size_t>((v_pos + n - 1) % n)];
                auto prev_it = edge_by_other.find(prev_v);
                if (prev_it != edge_by_other.end()) {
                    size_t prev_idx = prev_it->second;
                    const auto& prev_entry = bmesh.edges.find(edges[prev_idx].edge_key);
                    if (prev_entry != bmesh.edges.end()) {
                        int prev_face = (prev_entry->second.face0 == it->second.face1)
                            ? prev_entry->second.face1
                            : prev_entry->second.face0;
                        if (prev_face >= 0)
                            walk(prev_idx, prev_face);
                    }
                }
            }
        }
    }

    // Append any remaining unplaced edges (non-manifold / boundary)
    for (size_t i = 0; i < edges.size(); ++i) {
        if (!placed[i])
            result.push_back(edges[i]);
    }

    // If disk cycle failed to produce a full ordering, fall back to sort_ccw
    if (result.size() != edges.size())
        return sort_ccw(edges, positions, triangles, v_idx);

    return result;
}

/// Face normal for EdgeHalf.fprev/fnext (BMesh face index). Blender: e->fprev->no.
Vec3 bmesh_face_normal(const BevelParams& bp, int face_idx)
{
    if (!bp.bmesh || face_idx < 0 ||
        face_idx >= static_cast<int>(bp.bmesh->faces.size()))
        return {0, 0, 0};
    const geometry::Vec3 n = geometry::face_normal(*bp.bmesh, face_idx);
    return {n.x, n.y, n.z};
}

int find_bmesh_face_between(const geometry::BMesh& bmesh, int v_idx, int other_a, int other_b)
{
    // Strict loop order: face must walk other_a → v → other_b.
    // Returns -1 when sort_ccw produces CW order on non-coplanar faces.
    for (int fi = 0; fi < static_cast<int>(bmesh.faces.size()); ++fi) {
        const auto& loop = bmesh.faces[static_cast<size_t>(fi)].verts;
        const int n = static_cast<int>(loop.size());
        for (int i = 0; i < n; ++i) {
            if (loop[static_cast<size_t>(i)] != v_idx)
                continue;
            const int prev = loop[static_cast<size_t>((i + n - 1) % n)];
            const int next = loop[static_cast<size_t>((i + 1) % n)];
            if (prev == other_a && next == other_b)
                return fi;
        }
    }
    return -1;
}

bool bmesh_is_axis_aligned_box(const geometry::BMesh& bmesh, Vec3& min_v, Vec3& max_v)
{
    if (bmesh.verts.empty() || bmesh.faces.size() != 6)
        return false;

    min_v = {bmesh.verts[0].x, bmesh.verts[0].y, bmesh.verts[0].z};
    max_v = min_v;
    for (const auto& v : bmesh.verts) {
        min_v.x = std::min(min_v.x, v.x);
        min_v.y = std::min(min_v.y, v.y);
        min_v.z = std::min(min_v.z, v.z);
        max_v.x = std::max(max_v.x, v.x);
        max_v.y = std::max(max_v.y, v.y);
        max_v.z = std::max(max_v.z, v.z);
    }

    for (const auto& v : bmesh.verts) {
        const bool on_x = std::abs(v.x - min_v.x) < 1e-6 || std::abs(v.x - max_v.x) < 1e-6;
        const bool on_y = std::abs(v.y - min_v.y) < 1e-6 || std::abs(v.y - max_v.y) < 1e-6;
        const bool on_z = std::abs(v.z - min_v.z) < 1e-6 || std::abs(v.z - max_v.z) < 1e-6;
        if (!(on_x || on_y || on_z))
            return false;
    }
    for (int fi = 0; fi < static_cast<int>(bmesh.faces.size()); ++fi) {
        const geometry::Vec3 n = geometry::face_normal(bmesh, fi);
        const double ax = std::abs(n.x);
        const double ay = std::abs(n.y);
        const double az = std::abs(n.z);
        if (std::max({ax, ay, az}) < 1.0 - 1e-5)
            return false;
    }
    return true;
}

void add_clean_output_to_mesh(const BevelParams::OutputMesh& output, data::PcgMeshData& result)
{
    for (const auto& v : output.vertices)
        result.add_vertex({v.x, v.y, v.z});

    std::set<std::string> seen_geo;
    auto pos_key = [](const Vec3& v) -> std::string {
        auto q = [](double val) { return static_cast<int64_t>(std::llround(val / 1e-5)); };
        return std::to_string(q(v.x)) + ',' + std::to_string(q(v.y)) + ',' + std::to_string(q(v.z));
    };

    for (size_t i = 0; i + 2 < output.triangles.size(); i += 3) {
        const int ia = output.triangles[i];
        const int ib = output.triangles[i + 1];
        const int ic = output.triangles[i + 2];
        if (ia == ib || ib == ic || ia == ic)
            continue;
        const Vec3& a = output.vertices[static_cast<size_t>(ia)];
        const Vec3& b = output.vertices[static_cast<size_t>(ib)];
        const Vec3& c = output.vertices[static_cast<size_t>(ic)];
        if (length_squared(cross(sub(b, a), sub(c, a))) < 1e-20)
            continue;

        std::array<std::string, 3> keys = {pos_key(a), pos_key(b), pos_key(c)};
        std::sort(keys.begin(), keys.end());
        const std::string geo_key = keys[0] + '|' + keys[1] + '|' + keys[2];
        if (seen_geo.count(geo_key))
            continue;
        seen_geo.insert(geo_key);
        result.add_triangle(ia, ib, ic);
    }
}

data::PcgMeshData build_axis_aligned_rounded_box(const Vec3& min_v, const Vec3& max_v, double amount, int segments)
{
    BevelParams::OutputMesh output;
    const Vec3 center = scale(add(min_v, max_v), 0.5);
    const Vec3 half = scale(sub(max_v, min_v), 0.5);
    const double r = std::min({amount, half.x, half.y, half.z});
    const Vec3 inner{half.x - r, half.y - r, half.z - r};
    const int seg = std::max(1, segments);
    constexpr double kHalfPi = 1.57079632679489661923;

    auto p = [&](double x, double y, double z) -> Vec3 {
        return {center.x + x, center.y + y, center.z + z};
    };
    auto outward = [&](const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d) -> Vec3 {
        return normalize(sub(scale(add(add(a, b), add(c, d)), 0.25), center));
    };
    auto add_quad = [&](const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d) {
        output.add_oriented_quad(a, b, c, d, outward(a, b, c, d));
    };

    // Six flat face centers.
    for (int axis = 0; axis < 3; ++axis) {
        for (int sign : {-1, 1}) {
            Vec3 corners[4];
            for (int i = 0; i < 4; ++i) {
                Vec3 q{};
                q[axis] = sign * half[axis];
                const int a1 = (axis + 1) % 3;
                const int a2 = (axis + 2) % 3;
                q[a1] = (i == 0 || i == 3) ? -inner[a1] : inner[a1];
                q[a2] = (i < 2) ? -inner[a2] : inner[a2];
                corners[i] = p(q.x, q.y, q.z);
            }
            add_quad(corners[0], corners[1], corners[2], corners[3]);
        }
    }

    // Twelve rounded edge strips.
    for (int length_axis = 0; length_axis < 3; ++length_axis) {
        const int a1 = (length_axis + 1) % 3;
        const int a2 = (length_axis + 2) % 3;
        for (int s1 : {-1, 1}) {
            for (int s2 : {-1, 1}) {
                for (int k = 0; k < seg; ++k) {
                    const double t0 = kHalfPi * static_cast<double>(k) / seg;
                    const double t1 = kHalfPi * static_cast<double>(k + 1) / seg;
                    Vec3 q00{}, q01{}, q10{}, q11{};
                    q00[length_axis] = -inner[length_axis];
                    q01[length_axis] = inner[length_axis];
                    q10[length_axis] = -inner[length_axis];
                    q11[length_axis] = inner[length_axis];
                    q00[a1] = s1 * (inner[a1] + r * std::cos(t0));
                    q01[a1] = q00[a1];
                    q10[a1] = s1 * (inner[a1] + r * std::cos(t1));
                    q11[a1] = q10[a1];
                    q00[a2] = s2 * (inner[a2] + r * std::sin(t0));
                    q01[a2] = q00[a2];
                    q10[a2] = s2 * (inner[a2] + r * std::sin(t1));
                    q11[a2] = q10[a2];
                    add_quad(p(q00.x, q00.y, q00.z), p(q01.x, q01.y, q01.z),
                             p(q11.x, q11.y, q11.z), p(q10.x, q10.y, q10.z));
                }
            }
        }
    }

    // Eight spherical corner patches.
    for (int sx : {-1, 1}) {
        for (int sy : {-1, 1}) {
            for (int sz : {-1, 1}) {
                for (int u = 0; u < seg; ++u) {
                    for (int v = 0; v < seg; ++v) {
                        const double u0 = kHalfPi * static_cast<double>(u) / seg;
                        const double u1 = kHalfPi * static_cast<double>(u + 1) / seg;
                        const double v0 = kHalfPi * static_cast<double>(v) / seg;
                        const double v1 = kHalfPi * static_cast<double>(v + 1) / seg;
                        auto corner = [&](double uu, double vv) {
                            return p(sx * (inner.x + r * std::cos(uu)),
                                     sy * (inner.y + r * std::sin(uu) * std::cos(vv)),
                                     sz * (inner.z + r * std::sin(uu) * std::sin(vv)));
                        };
                        add_quad(corner(u0, v0), corner(u1, v0), corner(u1, v1), corner(u0, v1));
                    }
                }
            }
        }
    }

    data::PcgMeshData result;
    add_clean_output_to_mesh(output, result);
    return result;
}

std::vector<VertEdge> get_vert_edges(
    int v_idx,
    const std::vector<std::array<int, 3>>& triangles,
    const std::unordered_map<int64_t, std::array<int, 2>>& edge_faces)
{
    std::vector<VertEdge> edges;
    std::set<int64_t> seen;

    for (size_t ti = 0; ti < triangles.size(); ++ti) {
        const auto& tri = triangles[ti];
        for (int e = 0; e < 3; ++e) {
            int a = tri[e];
            int b = tri[(e + 1) % 3];
            if (a != v_idx && b != v_idx) continue;

            int other = (a == v_idx) ? b : a;
            int64_t key = edge_key(a, b);
            if (seen.count(key)) continue;
            seen.insert(key);

            // Determine which end is v_idx
            int ev0 = v_idx;  // this vertex
            int ev1 = other;  // other end
            edges.push_back({other, key, ev0, ev1});
        }
    }

    return edges;
}

// Sort edges in CCW order around vertex normal
std::vector<VertEdge> sort_ccw(
    const std::vector<VertEdge>& edges,
    const std::vector<Vec3>& positions,
    const std::vector<std::array<int, 3>>& triangles,
    int v_idx)
{
    if (edges.size() <= 1)
        return edges;

    Vec3 vnorm = vertex_normal_from_data(positions, triangles, v_idx);
    if (length(vnorm) < 1e-12) {
        // Fallback: compute from first edge cross product
        if (edges.size() >= 2) {
            Vec3 d1 = normalize(sub(positions[edges[0].other_v], positions[v_idx]));
            Vec3 d2 = normalize(sub(positions[edges[1].other_v], positions[v_idx]));
            vnorm = normalize(cross(d1, d2));
        }
    }

    // Project edge directions onto plane perpendicular to vnorm
    std::vector<std::pair<double, VertEdge>> angled;
    Vec3 ref_dir = normalize(sub(positions[edges[0].other_v], positions[v_idx]));
    // Project ref to plane
    ref_dir = normalize(sub(ref_dir, scale(vnorm, dot(ref_dir, vnorm))));

    for (const auto& e : edges) {
        Vec3 dir = normalize(sub(positions[e.other_v], positions[v_idx]));
        dir = normalize(sub(dir, scale(vnorm, dot(dir, vnorm))));
        double angle = std::atan2(dot(vnorm, cross(ref_dir, dir)), dot(ref_dir, dir));
        angled.push_back({angle, e});
    }

    std::sort(angled.begin(), angled.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    std::vector<VertEdge> result;
    for (const auto& [_, e] : angled)
        result.push_back(e);
    return result;
}

// Find the face index between two consecutive edges at a vertex
int find_face_between(int v_idx, int e1_other, int e2_other,
                      const std::vector<std::array<int, 3>>& triangles,
                      const std::unordered_map<int64_t, std::array<int, 2>>& edge_faces)
{
    int64_t key1 = edge_key(v_idx, e1_other);
    int64_t key2 = edge_key(v_idx, e2_other);

    auto it1 = edge_faces.find(key1);
    auto it2 = edge_faces.find(key2);
    if (it1 == edge_faces.end() || it2 == edge_faces.end())
        return -1;

    const auto& f1 = it1->second;
    const auto& f2 = it2->second;

    for (int f : f1) {
        if (f < 0) continue;
        for (int g : f2) {
            if (g < 0) continue;
            if (f == g) return f;
        }
    }
    return -1;
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════════
// Section 5: Offset Meet (Blender offset_meet L1882-2090)
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

/// Slide distance d along edge e from vertex v (Blender slide_dist L1644).
Vec3 slide_dist(const Vec3& v_pos, const Vec3& other_pos, double d) {
    Vec3 dir = normalize(sub(other_pos, v_pos));
    return add(v_pos, scale(dir, d));
}

/// Check if co is outside edge e's range (Blender is_outside_edge L1658).
/// Uses full EdgeHalf endpoints (edge_v0→edge_v1), not BevVert-relative framing.
bool is_outside_edge(const BevelParams& bp, EdgeHalf* e, const Vec3& co,
                     int* r_closer = nullptr)
{
    const auto& positions = *bp.positions;
    Vec3 p0 = positions[static_cast<size_t>(e->edge_v0)];
    Vec3 p1 = positions[static_cast<size_t>(e->edge_v1)];
    Vec3 u = sub(p1, p0);
    double lenu = length(u);
    if (lenu < 1e-12)
        return false;

    Vec3 un = scale(u, 1.0 / lenu);
    Vec3 h = sub(co, p0);
    double lambda = dot(un, h);
    if (lambda <= -BEVEL_EPSILON_BIG * lenu) {
        if (r_closer)
            *r_closer = 0;
        return true;
    }
    if (lambda >= (1.0 + BEVEL_EPSILON_BIG) * lenu) {
        if (r_closer)
            *r_closer = 1;
        return true;
    }
    return false;
}

/// Calculate the meeting point between two offset lines (Blender offset_meet).
/// e1 and e2 are consecutive edges (CCW) around vertex v.
/// f is the face between e1 and e2 (may be null for edges_between case).
void offset_meet(BevelParams& bp,
                 EdgeHalf* e1, EdgeHalf* e2,
                 int v_idx,
                 int f_idx,  // face between e1 and e2, -1 if edges_between
                 bool edges_between,
                 Vec3& meetco,
                 const EdgeHalf* e_in_plane)
{
    const auto& positions = *bp.positions;
    const auto& triangles = *bp.triangles;

    Vec3 v_co = positions[static_cast<size_t>(v_idx)];

    // Direction vectors
    Vec3 dir1 = sub(v_co, positions[static_cast<size_t>(e1->edge_v1)]);
    Vec3 dir2 = sub(positions[static_cast<size_t>(e2->edge_v1)], v_co);

    double ang = angle_v3v3(dir1, dir2);

    if (ang < BEVEL_EPSILON_ANG) {
        // Parallel: use bisector direction
        Vec3 norm_v;
        if (f_idx >= 0) {
            norm_v = bmesh_face_normal(bp, f_idx);
        } else {
            norm_v = vertex_normal({positions, triangles}, v_idx);
        }

        Vec3 sum_dir = add(dir1, dir2);
        Vec3 norm_perp1 = normalize(cross(sum_dir, norm_v));
        double d = std::max(e1->offset_r, e2->offset_l);
        d = d / std::cos(ang / 2.0);
        meetco = add(v_co, scale(norm_perp1, d));
    }
    else if (std::abs(ang - M_PI) < BEVEL_EPSILON_ANG) {
        // Anti-parallel: slide along the common line
        double d = std::max(e1->offset_r, e2->offset_l);
        meetco = slide_dist(v_co, positions[static_cast<size_t>(e2->edge_v1)], d);
    }
    else {
        // General case: compute perpendicular directions and intersect offset lines
        Vec3 norm_v1, norm_v2;

        if (f_idx >= 0 && ang < BEVEL_SMALL_ANG) {
            norm_v1 = bmesh_face_normal(bp, f_idx);
            norm_v2 = norm_v1;
        } else if (!edges_between) {
            norm_v1 = normalize(cross(dir2, dir1));
            if (f_idx >= 0) {
                Vec3 f_no = bmesh_face_normal(bp, f_idx);
                if (dot(norm_v1, f_no) < 0.0) norm_v1 = negate(norm_v1);
            }
            norm_v2 = norm_v1;
        } else {
            // Separate faces
            Vec3 dir1n = sub(positions[static_cast<size_t>(e2->edge_v1)], v_co);
            Vec3 dir2p = sub(v_co, positions[static_cast<size_t>(e1->edge_v1)]);
            norm_v1 = normalize(cross(dir1n, dir1));
            norm_v2 = normalize(cross(dir2, dir2p));
        }

        // Perpendicular vectors pointing into face
        Vec3 norm_perp1 = normalize(cross(dir1, norm_v1));
        Vec3 norm_perp2 = normalize(cross(dir2, norm_v2));

        // Offset lines
        Vec3 off1a = add(v_co, scale(norm_perp1, e1->offset_r));
        Vec3 off1b = add(off1a, dir1);
        Vec3 off2a = add(v_co, scale(norm_perp2, e2->offset_l));
        Vec3 off2b = add(off2a, dir2);

        // Intersect
        Vec3 isect2;
        int isect_kind = isect_line_line_v3(off1a, off1b, off2a, off2b, meetco, isect2);

        if (isect_kind == 0) {
            // Lines don't meet at a single point
            if (isect_kind == 2) {
                // Collinear
                meetco = off1a;
            } else {
                // Skew lines: use midpoint of closest points
                meetco = mid(meetco, isect2);
            }
        }

        // Check: if one offset is 0, don't go outside that edge (Blender offset_meet L2030).
        if (e1->offset_r == 0.0f) {
            int closer = -1;
            if (is_outside_edge(bp, e1, meetco, &closer)) {
                meetco = positions[static_cast<size_t>(closer == 0 ? e1->edge_v0 : e1->edge_v1)];
            }
        }
        if (e2->offset_l == 0.0f) {
            int closer = -1;
            if (is_outside_edge(bp, e2, meetco, &closer)) {
                meetco = positions[static_cast<size_t>(closer == 0 ? e2->edge_v0 : e2->edge_v1)];
            }
        }
    }
}

/// Offset a point in a plane (Blender offset_in_plane L2213).
Vec3 offset_in_plane(EdgeHalf* e, const Vec3& plane_no, bool left, int v_idx,
                     const BevelParams& bp)
{
    const auto& positions = *bp.positions;
    Vec3 v_co = positions[static_cast<size_t>(v_idx)];
    Vec3 other_co = positions[static_cast<size_t>(e->edge_v1)];

    Vec3 dir = sub(other_co, v_co);
    Vec3 norm_perp = normalize(cross(dir, plane_no));
    if (!left) norm_perp = negate(norm_perp);

    float offset = left ? e->offset_l : e->offset_r;
    return add(v_co, scale(norm_perp, offset));
}

/// Determine angle kind between two edges at a vertex (Blender edges_angle_kind).
AngleKind edges_angle_kind(EdgeHalf* e1, EdgeHalf* e2, int v_idx,
                           const BevelParams& bp)
{
    const auto& positions = *bp.positions;
    Vec3 v_co = positions[static_cast<size_t>(v_idx)];
    Vec3 dir1 = normalize(sub(positions[static_cast<size_t>(e1->edge_v1)], v_co));
    Vec3 dir2 = normalize(sub(positions[static_cast<size_t>(e2->edge_v1)], v_co));

    double ang = angle_normalized_v3v3(dir1, dir2);

    // Get face normal to determine which side
    int f = find_face_between(v_idx, e1->edge_v1, e2->edge_v1,
                              *bp.triangles, bp.edge_faces);
    if (f >= 0) {
        Vec3 f_no = tri_normal({*bp.positions, *bp.triangles}, f);
        Vec3 cross_d = cross(dir1, dir2);
        if (dot(cross_d, f_no) < 0) {
            // Reflex angle
            return AngleKind::Larger;
        }
    }

    if (ang < BEVEL_EPSILON_ANG) return AngleKind::Straight;
    return AngleKind::Smaller;
}

/// Check if edge is "on plane" (coplanar with adjacent faces) (Blender eh_on_plane).
bool eh_on_plane(EdgeHalf* e, int v_idx, const BevelParams& bp) {
    if (e->fprev < 0 && e->fnext < 0)
        return true;
    if (e->fprev < 0 || e->fnext < 0)
        return false;

    Vec3 n1 = bmesh_face_normal(bp, e->fprev);
    Vec3 n2 = bmesh_face_normal(bp, e->fnext);
    return nearly_parallel(n1, n2);
}

/// Check if good to use offset_on_edge_between (Blender good_offset_on_edge_between).
bool good_offset_on_edge_between(EdgeHalf* e1, EdgeHalf* e2, EdgeHalf* emid,
                                  int v_idx, const BevelParams& bp)
{
    const auto& positions = *bp.positions;
    Vec3 v_co = positions[static_cast<size_t>(v_idx)];
    Vec3 d1 = normalize(sub(positions[static_cast<size_t>(e1->edge_v1)], v_co));
    Vec3 d2 = normalize(sub(positions[static_cast<size_t>(e2->edge_v1)], v_co));
    Vec3 dm = normalize(sub(positions[static_cast<size_t>(emid->edge_v1)], v_co));

    // Check that emid is between e1 and e2
    double cross_1m = dot(cross(d1, dm), vertex_normal({*bp.positions, *bp.triangles}, v_idx));
    double cross_m2 = dot(cross(dm, d2), vertex_normal({*bp.positions, *bp.triangles}, v_idx));

    return cross_1m > 0 && cross_m2 > 0;
}

/// Calculate offset on an edge between two beveled edges (Blender offset_on_edge_between).
/// Returns true if successful, sets r_co and r_sinratio.
bool offset_on_edge_between(EdgeHalf* e1, EdgeHalf* e2, EdgeHalf* emid,
                             int v_idx, const BevelParams& bp,
                             Vec3& r_co, float& r_sinratio)
{
    const auto& positions = *bp.positions;
    Vec3 v_co = positions[static_cast<size_t>(v_idx)];

    Vec3 dir1 = normalize(sub(v_co, positions[static_cast<size_t>(e1->edge_v1)]));
    Vec3 dir2 = normalize(sub(positions[static_cast<size_t>(e2->edge_v1)], v_co));
    Vec3 dir_mid = normalize(sub(positions[static_cast<size_t>(emid->edge_v1)], v_co));

    // Angle between e1 and emid
    double ang1 = angle_v3v3v3(positions[static_cast<size_t>(e1->edge_v1)], v_co,
                                positions[static_cast<size_t>(emid->edge_v1)]);
    // Angle between emid and e2
    double ang2 = angle_v3v3v3(positions[static_cast<size_t>(emid->edge_v1)], v_co,
                                positions[static_cast<size_t>(e2->edge_v1)]);

    if (ang1 < BEVEL_EPSILON_ANG || ang2 < BEVEL_EPSILON_ANG)
        return false;

    // sin ratio: how the offset splits between the two sides
    double sin1 = std::sin(ang1);
    double sin2 = std::sin(ang2);
    r_sinratio = static_cast<float>(sin2 / (sin1 + sin2));

    // The offset point is on emid, at distance determined by the sine rule
    double d = e1->offset_r;
    double offset_on_mid = d * sin1 / (sin1 + sin2);
    // Also add the other offset
    double d2 = e2->offset_l;
    offset_on_mid += d2 * sin2 / (sin1 + sin2);

    // Actually, Blender computes it differently:
    // The meet point is where the offset lines from e1 and e2 would hit emid
    // Using sine rule: offset_on_mid = e1->offset_r * sin(ang1) / sin(ang1+ang2)
    // But actually need the combined approach
    double combined_offset = (d * sin1 + d2 * sin2) / (sin1 + sin2);
    r_co = add(v_co, scale(dir_mid, combined_offset));
    return true;
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════════
// Section 6: Build Boundary (Blender build_boundary L3521-3760)
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

/// Add a new boundvert to the VMesh boundary ring.
BoundVert* add_new_bound_vert(VMesh* vm, const Vec3& co) {
    auto* bv = new BoundVert();
    bv->nv.co = co;
    if (!vm->boundstart) {
        bv->index = 0;
        vm->boundstart = bv;
        bv->next = bv->prev = bv;
    } else {
        BoundVert* tail = vm->boundstart->prev;
        bv->index = tail->index + 1;
        bv->prev = tail;
        bv->next = vm->boundstart;
        tail->next = bv;
        vm->boundstart->prev = bv;
    }
    vm->count++;
    return bv;
}

void adjust_bound_vert(BoundVert* bv, const Vec3& co) {
    bv->nv.co = co;
}

/// Find the next beveled edge after e in CCW order (Blender next_bev).
EdgeHalf* next_bev(BevVert* bv, EdgeHalf* from) {
    EdgeHalf* e = from ? from->next : &bv->edges[0];
    EdgeHalf* start = e;
    do {
        if (e->is_bev) return e;
        e = e->next;
    } while (e != start);
    return nullptr;
}

void build_boundary(BevelParams& bp, BevVert* bv, bool construct);

BevVert* find_bevvert(BevelParams& bp, int v_idx) {
    for (auto& bv : bp.bevverts) {
        if (bv.v_idx == v_idx)
            return &bv;
    }
    return nullptr;
}

EdgeHalf* find_other_end_edge_half(BevelParams& bp, EdgeHalf* e, BevVert** r_bvother) {
    BevVert* bvo = find_bevvert(bp, e->edge_v1);
    if (!bvo) {
        if (r_bvother)
            *r_bvother = nullptr;
        return nullptr;
    }
    if (r_bvother)
        *r_bvother = bvo;
    for (auto& eh : bvo->edges) {
        if (eh.is_bev && eh.edge_v1 == e->edge_v0)
            return &eh;
    }
    return nullptr;
}

void adjust_miter_coords(BevelParams& bp, BevVert* bv, EdgeHalf* emiter) {
    BevelMiter miter_outer = bp.miter_outer;
    BoundVert* v1 = emiter->rightv;
    if (!v1)
        return;

    BoundVert* v2 = nullptr;
    BoundVert* v3 = nullptr;
    if (miter_outer == BevelMiter::Patch) {
        v2 = v1->next;
        v3 = v2 ? v2->next : nullptr;
    } else {
        v3 = v1->next;
    }
    if (!v3)
        return;

    BoundVert* v1prev = v1->prev;
    BoundVert* v3next = v3->next;
    if (!v1prev || !v3next)
        return;

    Vec3 co2 = v1->nv.co;
    if (v1->is_arc_start)
        v1->profile.middle = co2;

    const auto& positions = *bp.positions;
    Vec3 v_co = positions[static_cast<size_t>(bv->v_idx)];

    Vec3 edge_dir = normalize(sub(v_co, positions[static_cast<size_t>(emiter->edge_v1)]));
    double d = bp.offset / std::max(bp.seg / 2, 1);
    Vec3 line_p = madd(co2, edge_dir, d);

    Vec3 co1;
    if (!isect_line_plane_v3(co1, co2, line_p, v1prev->nv.co, edge_dir))
        co1 = line_p;
    adjust_bound_vert(v1, co1);

    EdgeHalf* emiter_other = v3->elast;
    if (!emiter_other)
        return;
    edge_dir = normalize(sub(v_co, positions[static_cast<size_t>(emiter_other->edge_v1)]));
    line_p = madd(co2, edge_dir, d);
    Vec3 co3;
    if (!isect_line_plane_v3(co3, co2, line_p, v3next->nv.co, edge_dir))
        co3 = line_p;
    adjust_bound_vert(v3, co3);
}

void adjust_miter_inner_coords(BevelParams& bp, BevVert* bv, EdgeHalf* emiter) {
    VMesh* vm = bv->vmesh.get();
    if (!vm || !vm->boundstart)
        return;

    const auto& positions = *bp.positions;
    Vec3 v_co = positions[static_cast<size_t>(bv->v_idx)];

    BoundVert* vstart = vm->boundstart;
    BoundVert* v = vstart;
    do {
        if (v->is_arc_start) {
            BoundVert* v3 = v->next;
            EdgeHalf* e = v->efirst;
            if (e && e != emiter) {
                Vec3 co = v->nv.co;
                Vec3 edge_dir = normalize(sub(positions[static_cast<size_t>(e->edge_v1)], v_co));
                v->nv.co = madd(co, edge_dir, bp.spread);
                e = v3 ? v3->elast : nullptr;
                if (e) {
                    edge_dir = normalize(sub(positions[static_cast<size_t>(e->edge_v1)], v_co));
                    v3->nv.co = madd(co, edge_dir, bp.spread);
                }
            }
            v = v3 ? v3->next : v->next;
        } else {
            v = v->next;
        }
    } while (v != vstart);
}

bool adjust_the_cycle_or_chain_fast(BoundVert* vstart, int np, bool iscycle) {
    if (np < 2)
        return false;

    std::vector<float> g(static_cast<size_t>(np));
    float spec_sum = 0.0f;
    BoundVert* v = vstart;
    for (int i = 0; i < np; i++) {
        g[static_cast<size_t>(i)] = v->sinratio;
        if (iscycle || v->adjchain)
            spec_sum += v->efirst ? v->efirst->offset_r : 0.0f;
        else
            spec_sum += v->elast ? v->elast->offset_l : 0.0f;
        v = v->adjchain;
        if (!v)
            return false;
    }

    float gprod = 1.0f;
    float gprod_sum = 1.0f;
    std::vector<float> g_prod(static_cast<size_t>(np));
    for (int i = np - 1; i > 0; i--) {
        gprod *= g[static_cast<size_t>(i)];
        g_prod[static_cast<size_t>(i)] = gprod;
        gprod_sum += gprod;
    }
    g_prod[0] = 1.0f;
    if (iscycle) {
        gprod *= g[0];
        if (std::abs(gprod - 1.0f) > BEVEL_EPSILON_BIG)
            return false;
    }
    if (gprod_sum == 0.0f)
        return false;

    float p = spec_sum / gprod_sum;
    v = vstart;
    for (int i = 0; i < np; i++) {
        if (iscycle || v->adjchain) {
            EdgeHalf* eright = v->efirst;
            EdgeHalf* eleft = v->elast;
            if (eright)
                eright->offset_r = g_prod[static_cast<size_t>((i + 1) % np)] * p;
            if ((iscycle || v != vstart) && eleft)
                eleft->offset_l = v->sinratio * (eright ? eright->offset_r : p);
        } else if (v->elast) {
            v->elast->offset_l = p;
        }
        v = v->adjchain;
        if (!v)
            break;
    }
    return true;
}

void adjust_the_cycle_or_chain(BoundVert* vstart, bool iscycle) {
    int np = 0;
    BoundVert* v = vstart;
    do {
        np++;
        v = v->adjchain;
    } while (v && v != vstart);
    adjust_the_cycle_or_chain_fast(vstart, np, iscycle);
}

void adjust_offsets(BevelParams& bp) {
    for (auto& bv : bp.bevverts) {
        VMesh* vm = bv.vmesh.get();
        if (!vm || !vm->boundstart)
            continue;

        BoundVert* vanchor = vm->boundstart;
        do {
            if (vanchor->visited || !vanchor->eon)
                continue;

            BoundVert* v = vanchor;
            BoundVert* vchainstart = vanchor;
            BoundVert* vchainend = vanchor;
            BevVert* bvcur = &bv;
            bool iscycle = false;
            int chainlen = 1;

            while (v->eon && !v->visited && !iscycle) {
                v->visited = true;
                if (!v->efirst)
                    break;
                EdgeHalf* enext = find_other_end_edge_half(bp, v->efirst, &bvcur);
                if (!enext)
                    break;
                BoundVert* vnext = enext->leftv;
                if (!vnext)
                    break;
                v->adjchain = vnext;
                vchainend = vnext;
                chainlen++;
                if (vnext->visited) {
                    if (vnext != vchainstart)
                        break;
                    adjust_the_cycle_or_chain(vchainstart, true);
                    iscycle = true;
                }
                v = vnext;
            }

            if (!iscycle) {
                if (v)
                    v->adjchain = nullptr;
                v = vchainstart;
                bvcur = &bv;
                do {
                    v->visited = true;
                    if (!v->elast)
                        break;
                    EdgeHalf* enext = find_other_end_edge_half(bp, v->elast, &bvcur);
                    if (!enext)
                        break;
                    BoundVert* vnext = enext->rightv;
                    if (!vnext)
                        break;
                    vnext->adjchain = v;
                    chainlen++;
                    vchainstart = vnext;
                    v = vnext;
                } while (v && !v->visited && v->eon);

                if (chainlen >= 3 && vchainstart && vchainend &&
                    !vchainstart->eon && !vchainend->eon) {
                    adjust_the_cycle_or_chain(vchainstart, false);
                }
            }
        } while ((vanchor = vanchor->next) != vm->boundstart);
    }

    for (auto& bv : bp.bevverts) {
        VMesh* vm = bv.vmesh.get();
        if (!vm || !vm->boundstart)
            continue;
        build_boundary(bp, &bv, false);
        BoundVert* b = vm->boundstart;
        do {
            b->visited = false;
            b->adjchain = nullptr;
        } while ((b = b->next) != vm->boundstart);
    }
}

/// Terminal single-bevel-edge case (Blender build_boundary_terminal_edge).
void build_boundary_terminal_edge(BevelParams& bp, BevVert* bv, EdgeHalf* efirst, bool construct)
{
    VMesh* vm = bv->vmesh.get();
    if (!vm) {
        bv->vmesh = std::make_unique<VMesh>();
        vm = bv->vmesh.get();
        vm->seg = bp.seg;
    }

    EdgeHalf* e = efirst;
    Vec3 co;

    if (bv->edgecount == 2) {
        int fprev = e->fprev;
        if (fprev >= 0) {
            Vec3 f_no = bmesh_face_normal(bp, fprev);
            co = offset_in_plane(e, f_no, true, bv->v_idx, bp);
        } else {
            co = (*bp.positions)[static_cast<size_t>(bv->v_idx)];
        }
        if (construct) {
            BoundVert* bndv = add_new_bound_vert(vm, co);
            bndv->efirst = bndv->elast = bndv->ebev = e;
            e->leftv = bndv;
        } else {
            adjust_bound_vert(e->leftv, co);
        }

        int fnext = e->fnext;
        if (fnext >= 0) {
            Vec3 f_no = bmesh_face_normal(bp, fnext);
            co = offset_in_plane(e, f_no, false, bv->v_idx, bp);
        } else {
            co = (*bp.positions)[static_cast<size_t>(bv->v_idx)];
        }
        if (construct) {
            BoundVert* bndv = add_new_bound_vert(vm, co);
            bndv->efirst = bndv->elast = e;
            e->rightv = bndv;
        } else {
            adjust_bound_vert(e->rightv, co);
        }

        EdgeHalf* e_next = e->next;
        co = slide_dist((*bp.positions)[static_cast<size_t>(bv->v_idx)],
                       (*bp.positions)[static_cast<size_t>(e_next->edge_v1)],
                       e->offset_l);
        if (construct) {
            BoundVert* bndv = add_new_bound_vert(vm, co);
            bndv->efirst = bndv->elast = e_next;
            e_next->leftv = e_next->rightv = bndv;
        } else {
            adjust_bound_vert(e_next->leftv, co);
        }
    } else if (bv->selcount == 1 && bv->edgecount == 3) {
        // Terminal cap: offset_meet collapses both meets to the corner. Place symmetric
        // slide points on adjacent cap edges (matches profile synthesis in set_profile_params).
        EdgeHalf* e_prev = efirst->prev;
        EdgeHalf* e_next = efirst->next;
        float d = efirst->offset_l_spec;
        if (bp.profile < 0.25f)
            d *= std::sqrt(2.0f);
        const Vec3 v_co = (*bp.positions)[static_cast<size_t>(bv->v_idx)];
        const Vec3 co_left = slide_dist(
            v_co, (*bp.positions)[static_cast<size_t>(e_prev->edge_v1)], d);
        const Vec3 co_right = slide_dist(
            v_co, (*bp.positions)[static_cast<size_t>(e_next->edge_v1)], d);

        if (construct) {
            BoundVert* bndv_left = add_new_bound_vert(vm, co_left);
            bndv_left->efirst = e_prev;
            bndv_left->elast = bndv_left->ebev = efirst;
            efirst->leftv = bndv_left;
            e_prev->leftv = e_prev->rightv = bndv_left;

            BoundVert* bndv_right = add_new_bound_vert(vm, co_right);
            bndv_right->efirst = efirst;
            bndv_right->elast = e_next;
            e_next->leftv = e_next->rightv = bndv_right;
            efirst->rightv = bndv_right;
        } else {
            adjust_bound_vert(efirst->leftv, co_left);
            adjust_bound_vert(efirst->rightv, co_right);
            if (e_prev->leftv)
                adjust_bound_vert(e_prev->leftv, co_left);
            if (e_next->leftv)
                adjust_bound_vert(e_next->leftv, co_right);
        }
    } else {
        EdgeHalf* e_prev = e->prev;
        offset_meet(bp, e_prev, e, bv->v_idx, e->fprev, false, co, nullptr);
        if (construct) {
            BoundVert* bndv = add_new_bound_vert(vm, co);
            bndv->efirst = e_prev;
            bndv->elast = bndv->ebev = e;
            e->leftv = bndv;
            e_prev->leftv = e_prev->rightv = bndv;
        } else {
            adjust_bound_vert(e->leftv, co);
        }

        e = e->next;
        offset_meet(bp, e->prev, e, bv->v_idx, e->fprev, false, co, nullptr);
        if (construct) {
            BoundVert* bndv = add_new_bound_vert(vm, co);
            bndv->efirst = e->prev;
            bndv->elast = e;
            e->leftv = e->rightv = bndv;
            e->prev->rightv = bndv;
        } else {
            adjust_bound_vert(e->leftv, co);
        }

        float d = efirst->offset_l_spec;
        if (bp.profile < 0.25f)
            d *= std::sqrt(2.0f);

        for (EdgeHalf* e_iter = e->next; e_iter != efirst; e_iter = e_iter->next) {
            co = slide_dist((*bp.positions)[static_cast<size_t>(bv->v_idx)],
                           (*bp.positions)[static_cast<size_t>(e_iter->edge_v1)], d);
            if (construct) {
                BoundVert* bndv = add_new_bound_vert(vm, co);
                bndv->efirst = bndv->elast = e_iter;
                e_iter->leftv = e_iter->rightv = bndv;
            } else {
                adjust_bound_vert(e_iter->leftv, co);
            }
        }
    }

    if (construct) {
        if (vm->count == 2 && bv->edgecount == 3)
            vm->mesh_kind = MeshKind::None;
        else if (vm->count == 3)
            vm->mesh_kind = MeshKind::TriFan;
        else
            vm->mesh_kind = MeshKind::Poly;
    }
}

void build_boundary(BevelParams& bp, BevVert* bv, bool construct) {
    if (bv->edgecount <= 1)
        return;

    VMesh* vm = bv->vmesh.get();
    if (!vm) {
        bv->vmesh = std::make_unique<VMesh>();
        vm = bv->vmesh.get();
        vm->seg = bp.seg;
    }

    EdgeHalf* efirst = next_bev(bv, nullptr);
    if (!efirst || !efirst->is_bev)
        return;

    if (bv->selcount == 1) {
        build_boundary_terminal_edge(bp, bv, efirst, construct);
        return;
    }

    // Multiple beveled edges: iterate pairs
    BevelMiter miter_outer = (bv->selcount >= 3) ? bp.miter_outer : BevelMiter::Sharp;
    BevelMiter miter_inner = bp.miter_inner;
    EdgeHalf* emiter = nullptr;
    EdgeHalf* e = efirst;

    do {
        EdgeHalf* e2 = next_bev(bv, e);
        if (!e2) break;

        int in_plane = 0;
        int not_in_plane = 0;
        EdgeHalf* enip = nullptr;
        EdgeHalf* eip = nullptr;

        EdgeHalf* e2_iter = e->next;
        while (e2_iter != e2) {
            if (eh_on_plane(e2_iter, bv->v_idx, bp)) {
                in_plane++;
                eip = e2_iter;
            } else {
                not_in_plane++;
                enip = e2_iter;
            }
            e2_iter = e2_iter->next;
        }

        EdgeHalf* eon = nullptr;
        Vec3 co;
        float r = 1.0f;

        if (in_plane == 0 && not_in_plane == 0) {
            offset_meet(bp, e, e2, bv->v_idx, e->fnext, false, co, nullptr);
        } else if (not_in_plane > 0) {
            if (bp.loop_slide && not_in_plane == 1 &&
                good_offset_on_edge_between(e, e2, enip, bv->v_idx, bp)) {
                if (offset_on_edge_between(e, e2, enip, bv->v_idx, bp, co, r)) {
                    eon = enip;
                } else {
                    offset_meet(bp, e, e2, bv->v_idx, -1, true, co, eip);
                }
            } else {
                offset_meet(bp, e, e2, bv->v_idx, -1, true, co, eip);
            }
        } else {
            if (bp.loop_slide && in_plane == 1 &&
                good_offset_on_edge_between(e, e2, eip, bv->v_idx, bp)) {
                if (offset_on_edge_between(e, e2, eip, bv->v_idx, bp, co, r)) {
                    eon = eip;
                } else {
                    offset_meet(bp, e, e2, bv->v_idx, e->fnext, false, co, nullptr);
                }
            } else {
                offset_meet(bp, e, e2, bv->v_idx, e->fnext, false, co, nullptr);
            }
        }

        if (construct) {
            BoundVert* v = add_new_bound_vert(vm, co);
            v->efirst = e;
            v->elast = e2;
            v->ebev = e2;
            v->eon = eon;
            if (eon)
                v->sinratio = r;
            e->rightv = v;
            e2->leftv = v;

            for (EdgeHalf* e3 = e->next; e3 != e2; e3 = e3->next) {
                e3->leftv = e3->rightv = v;
            }

            AngleKind ang_kind = edges_angle_kind(e, e2, bv->v_idx, bp);
            if ((miter_outer != BevelMiter::Sharp && !emiter && ang_kind == AngleKind::Larger) ||
                (miter_inner != BevelMiter::Sharp && ang_kind == AngleKind::Smaller)) {
                if (ang_kind == AngleKind::Larger)
                    emiter = e;

                BoundVert* v1 = v;
                v1->ebev = nullptr;
                BoundVert* v2 = nullptr;
                if (ang_kind == AngleKind::Larger && miter_outer == BevelMiter::Patch) {
                    v2 = add_new_bound_vert(vm, co);
                }
                BoundVert* v3 = add_new_bound_vert(vm, co);
                v3->ebev = e2;
                v3->efirst = e2;
                v3->elast = e2;
                v3->eon = nullptr;
                e2->leftv = v3;

                if (ang_kind == AngleKind::Larger && miter_outer == BevelMiter::Patch && v2) {
                    v1->is_patch_start = true;
                    v2->eon = v1->eon;
                    v2->sinratio = v1->sinratio;
                    v2->ebev = nullptr;
                    v1->eon = nullptr;
                    v1->sinratio = 1.0f;
                    v1->elast = e;
                    if (e->next == e2) {
                        v2->efirst = nullptr;
                        v2->elast = nullptr;
                    } else {
                        v2->efirst = e->next;
                        for (EdgeHalf* e3 = e->next; e3 != e2; e3 = e3->next) {
                            e3->leftv = e3->rightv = v2;
                            v2->elast = e3;
                        }
                    }
                } else {
                    v1->is_arc_start = true;
                    v1->profile.middle = co;
                    if (e->next == e2) {
                        v1->elast = v1->efirst;
                    } else {
                        int between = in_plane + not_in_plane;
                        int bet2 = between / 2;
                        bool betodd = (between % 2) == 1;
                        int idx = 0;
                        for (EdgeHalf* e3 = e->next; e3 != e2; e3 = e3->next) {
                            v1->elast = e3;
                            if (idx < bet2) {
                                e3->profile_index = 0;
                            } else if (betodd && idx == bet2) {
                                e3->profile_index = bp.seg / 2;
                            } else {
                                e3->profile_index = bp.seg;
                            }
                            idx++;
                        }
                    }
                }
            }
        } else {
            AngleKind ang_kind = edges_angle_kind(e, e2, bv->v_idx, bp);
            if ((miter_outer != BevelMiter::Sharp && !emiter && ang_kind == AngleKind::Larger) ||
                (miter_inner != BevelMiter::Sharp && ang_kind == AngleKind::Smaller)) {
                if (ang_kind == AngleKind::Larger)
                    emiter = e;
                BoundVert* v1 = e->rightv;
                BoundVert* v2 = nullptr;
                BoundVert* v3 = nullptr;
                if (ang_kind == AngleKind::Larger && miter_outer == BevelMiter::Patch) {
                    v2 = v1 ? v1->next : nullptr;
                    v3 = v2 ? v2->next : nullptr;
                } else {
                    v3 = v1 ? v1->next : nullptr;
                }
                if (v1)
                    adjust_bound_vert(v1, co);
                if (v2)
                    adjust_bound_vert(v2, co);
                if (v3)
                    adjust_bound_vert(v3, co);
            } else if (e->rightv) {
                adjust_bound_vert(e->rightv, co);
            }
        }

        e = e2;
    } while (e != efirst);

    if (miter_inner != BevelMiter::Sharp)
        adjust_miter_inner_coords(bp, bv, emiter);
    if (emiter)
        adjust_miter_coords(bp, bv, emiter);

    // Decide mesh_kind
    if (construct) {
        if (vm->count == 2)
            vm->mesh_kind = MeshKind::None;
        else if (bp.seg == 1)
            vm->mesh_kind = MeshKind::Poly;
        else {
            switch (bp.vmesh_method) {
            case BevelVMeshMethod::Adj:
                vm->mesh_kind = MeshKind::Adj;
                break;
            case BevelVMeshMethod::Cutoff:
                vm->mesh_kind = MeshKind::Cutoff;
                break;
            }
        }
    }
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════════
// Section 7: Geometry Collide Offset (Blender L8012-8120)
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

float geometry_collide_offset(BevelParams& bp, BevVert* bv, EdgeHalf* eb) {
    float no_collide = static_cast<float>(bp.offset + 1e6);
    if (bp.offset == 0.0)
        return no_collide;

    float kb = eb->offset_l_spec;
    EdgeHalf* ea = eb->next;  // direction b → a
    float ka = ea->offset_r_spec;

    int vb_idx = eb->edge_v0;
    int vc_idx = eb->edge_v1;

    // Find the other end's EdgeHalf
    EdgeHalf* ec = nullptr;
    float kc = 0.0f;

    // Find beveled edge at vc that shares a face with eb
    for (auto& bv_other : bp.bevverts) {
        if (bv_other.v_idx != vc_idx) continue;
        for (auto& eh : bv_other.edges) {
            if (!eh.is_bev) continue;
            if (eh.edge_v1 == vb_idx) {
                // This is the other end of eb
                ec = ea->next;
                if (ec && ec->is_bev)
                    kc = ec->offset_l_spec;
                break;
            }
        }
    }

    // Calculate angles
    const auto& positions = *bp.positions;
    Vec3 va = positions[static_cast<size_t>(ea->edge_v1)];
    Vec3 vb = positions[static_cast<size_t>(vb_idx)];
    Vec3 vc = positions[static_cast<size_t>(vc_idx)];

    double th1 = angle_v3v3v3(va, vb, vc);

    // For th2, need the next vertex after vc
    double th2 = M_PI / 2.0;  // Default: no constraint
    if (ec) {
        Vec3 vd = positions[static_cast<size_t>(ec->edge_v1)];
        th2 = angle_v3v3v3(vb, vc, vd);
    }

    double sin1 = std::sin(th1);
    double sin2 = std::sin(th2);
    double cos1 = std::cos(th1);
    double cos2 = std::cos(th2);

    double offsets_projected = 0.0;
    if (std::abs(sin1) > 1e-9)
        offsets_projected += (ka + cos1 * kb) / sin1;
    if (std::abs(sin2) > 1e-9)
        offsets_projected += (kc + cos2 * kb) / sin2;

    if (offsets_projected > 1e-9) {
        double len_b = length(sub(positions[static_cast<size_t>(vc_idx)],
                                  positions[static_cast<size_t>(vb_idx)]));
        double limit = bp.offset * (len_b / offsets_projected);
        if (limit > 1e-9)
            return static_cast<float>(std::min(limit, bp.offset));
    }

    return no_collide;
}

void bevel_limit_offset(BevelParams& bp) {
    for (auto& bv : bp.bevverts) {
        for (auto& e : bv.edges) {
            if (!e.is_bev) continue;
            float limit = geometry_collide_offset(bp, &bv, &e);
            e.offset_l = std::min(e.offset_l, limit);
            e.offset_r = std::min(e.offset_r, limit);

            // Also propagate to the other end
            for (auto& bv2 : bp.bevverts) {
                if (bv2.v_idx != e.edge_v1) continue;
                for (auto& e2 : bv2.edges) {
                    if (e2.is_bev && e2.edge_v1 == bv.v_idx) {
                        e2.offset_l = std::min(e2.offset_l, limit);
                        e2.offset_r = std::min(e2.offset_r, limit);
                    }
                }
            }
        }
    }
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════════
// Section 8: Profile Params + Calculate Profile
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

/// Project a point onto an edge (Blender project_to_edge L2246).
Vec3 project_to_edge(const Vec3& e_v0, const Vec3& e_v1, const Vec3& start, const Vec3& end) {
    Vec3 edge_dir = sub(e_v1, e_v0);
    double len_sq = length_squared(edge_dir);
    if (len_sq < 1e-12) return e_v0;

    double t = dot(sub(start, e_v0), edge_dir) / len_sq;
    t = std::clamp(t, 0.0, 1.0);
    return add(e_v0, scale(edge_dir, t));
}

void set_profile_params(BevelParams& bp, BevVert* bv, BoundVert* bndv) {
    EdgeHalf* e = bndv->ebev;
    Profile* pro = &bndv->profile;

    Vec3 start = bndv->nv.co;
    Vec3 end = bndv->next->nv.co;

    bool do_linear_interp = true;

    if (e) {
        do_linear_interp = false;
        pro->super_r = bp.pro_super_r;

        // Projection direction = edge direction
        Vec3 e_v0 = (*bp.positions)[static_cast<size_t>(bv->v_idx)];
        Vec3 e_v1 = (*bp.positions)[static_cast<size_t>(e->edge_v1)];
        pro->proj_dir = normalize(sub(e_v1, e_v0));

        // Terminal cap: collapsed boundary meets at the corner — synthesize profile
        // span along adjacent cap edges before the generic start/middle/end path.
        if (bv->selcount == 1 && bv->edgecount >= 3 &&
            length_squared(sub(start, end)) <= 1e-12) {
            EdgeHalf* e_prev = e->prev;
            EdgeHalf* e_next = e->next;
            if (e_prev && e_next) {
                float d = e->offset_l_spec;
                if (bp.profile < 0.25f)
                    d *= static_cast<float>(std::sqrt(2.0));
                const Vec3 left = slide_dist(e_v0, (*bp.positions)[static_cast<size_t>(e_prev->edge_v1)], d);
                const Vec3 right = slide_dist(e_v0, (*bp.positions)[static_cast<size_t>(e_next->edge_v1)], d);
                if (length_squared(sub(left, right)) > 1e-12) {
                    pro->start = left;
                    pro->end = right;
                    pro->middle = e_v0;
                    const Vec3 td1 = normalize(sub(pro->middle, pro->start));
                    const Vec3 td2 = normalize(sub(pro->middle, pro->end));
                    const Vec3 plane_no = normalize(cross(td1, td2));
                    if (!nearly_parallel(td1, td2)) {
                        pro->plane_co = e_v0;
                        pro->plane_no = plane_no;
                        pro->proj_dir = normalize(sub(e_v1, e_v0));
                        return;
                    }
                }
            }
        }

        Vec3 middle = project_to_edge(e_v0, e_v1, start, end);
        pro->start = start;
        pro->end = end;
        pro->middle = middle;

        // Plane normal from start-middle-end triangle
        Vec3 d1 = normalize(sub(middle, start));
        Vec3 d2 = normalize(sub(middle, end));
        Vec3 plane_no = normalize(cross(d1, d2));

        if (nearly_parallel(d1, d2)) {
            // Collinear: use vertex position or offset line intersection
            pro->middle = e_v0;

            // Try to use adjacent offset line intersection
            EdgeHalf* e_prev = e->prev;
            EdgeHalf* e_next = e->next;
            if (e_prev && e_next && e_prev->is_bev && e_next->is_bev && bv->selcount >= 3) {
                Vec3 d3 = normalize(sub((*bp.positions)[static_cast<size_t>(e_prev->edge_v1)], e_v0));
                Vec3 d4 = normalize(sub((*bp.positions)[static_cast<size_t>(e_next->edge_v1)], e_v0));
                if (!nearly_parallel(d3, d4)) {
                    Vec3 co3 = add(start, d3);
                    Vec3 co4 = add(end, d4);
                    Vec3 meetco, isect2;
                    int kind = isect_line_line_v3(start, co3, end, co4, meetco, isect2);
                    if (kind != 0)
                        pro->middle = meetco;
                    else
                        pro->middle = mid(start, end);
                } else {
                    pro->middle = mid(start, end);
                    do_linear_interp = true;
                }
            }

            d1 = normalize(sub(pro->middle, start));
            d2 = normalize(sub(pro->middle, end));
            plane_no = normalize(cross(d1, d2));
            if (nearly_parallel(d1, d2)) {
                // Terminal cap: collapsed meet points — synthesize profile span along
                // adjacent cap edges (non-bev edges have offset 0 so offset_in_plane degenerates).
                if (bv->selcount == 1 && bv->edgecount >= 3 &&
                    length_squared(sub(start, end)) <= 1e-12) {
                    EdgeHalf* e_prev = e->prev;
                    EdgeHalf* e_next = e->next;
                    Vec3 left = start;
                    Vec3 right = end;
                    if (e_prev && e_next) {
                        float d = e->offset_l_spec;
                        if (bp.profile < 0.25f)
                            d *= static_cast<float>(std::sqrt(2.0));
                        left = slide_dist(e_v0, (*bp.positions)[static_cast<size_t>(e_prev->edge_v1)], d);
                        right = slide_dist(e_v0, (*bp.positions)[static_cast<size_t>(e_next->edge_v1)], d);
                    }
                    if (length_squared(sub(left, right)) > 1e-12) {
                        pro->start = left;
                        pro->end = right;
                        pro->middle = e_v0;
                        d1 = normalize(sub(pro->middle, pro->start));
                        d2 = normalize(sub(pro->middle, pro->end));
                        plane_no = normalize(cross(d1, d2));
                        if (!nearly_parallel(d1, d2)) {
                            pro->plane_co = e_v0;
                            pro->plane_no = plane_no;
                            pro->proj_dir = normalize(sub(e_v1, e_v0));
                            do_linear_interp = false;
                        } else {
                            do_linear_interp = true;
                        }
                    } else {
                        do_linear_interp = true;
                    }
                } else {
                    do_linear_interp = true;
                }
            } else {
                pro->plane_co = e_v0;
                pro->plane_no = plane_no;
                pro->proj_dir = plane_no;
            }
        } else {
            pro->plane_co = start;
            pro->plane_no = plane_no;
        }
    }

    if (do_linear_interp) {
        pro->super_r = PRO_LINE_R;
        pro->start = start;
        pro->end = end;
        pro->middle = mid(start, end);
        pro->plane_no = {0, 0, 0};
        pro->plane_co = {0, 0, 0};
        pro->proj_dir = {0, 0, 0};
    }
}

void move_profile_plane(BoundVert* bndv, const Vec3& vertex_co)
{
    Profile& profile = bndv->profile;
    if (length_squared(profile.proj_dir) <= 1e-20)
        return;

    Vec3 d1 = normalize(sub(vertex_co, profile.start));
    Vec3 d2 = normalize(sub(vertex_co, profile.end));
    Vec3 no = normalize(cross(d1, d2));
    Vec3 no2 = normalize(cross(d1, profile.proj_dir));
    Vec3 no3 = normalize(cross(d2, profile.proj_dir));
    if (length_squared(no) > BEVEL_EPSILON_BIG_SQ &&
        length_squared(no2) > BEVEL_EPSILON_BIG_SQ &&
        length_squared(no3) > BEVEL_EPSILON_BIG_SQ &&
        std::abs(dot(no, no2)) < 1.0 - BEVEL_EPSILON_BIG &&
        std::abs(dot(no, no3)) < 1.0 - BEVEL_EPSILON_BIG) {
        profile.plane_no = no;
    }
    profile.special_params = true;
}

void calculate_profile(BevelParams& bp, BoundVert* bndv) {
    Profile* pro = &bndv->profile;

    if (bp.seg <= 1)
        return;

    // Allocate profile coordinates
    pro->prof_co.resize(static_cast<size_t>(bp.seg + 1));

    int seg = bp.seg;
    const auto& ps = bp.pro_spacing;

    // Build 2D→3D transform
    Mat4 map;
    bool use_map = false;
    if (pro->super_r != PRO_LINE_R) {
        use_map = make_unit_square_map(pro->start, pro->middle, pro->end, map);
    }

    for (int k = 0; k <= seg; k++) {
        Vec3 co;
        if (k == 0) {
            co = pro->start;
        } else if (k == seg) {
            co = pro->end;
        } else if (use_map) {
            Vec3 p{ps.xvals[static_cast<size_t>(k)], ps.yvals[static_cast<size_t>(k)], 0.0};
            co = mul_m4v3(map, p);
        } else {
            double t = static_cast<double>(k) / seg;
            co = add(scale(pro->start, 1.0 - t), scale(pro->end, t));
        }

        // Project onto profile plane
        Vec3 prof_co;
        if (length_squared(pro->proj_dir) > 1e-12) {
            if (!isect_line_plane_v3(prof_co, co, add(co, pro->proj_dir),
                                     pro->plane_co, pro->plane_no)) {
                prof_co = co;
            }
        } else {
            prof_co = co;
        }

        pro->prof_co[static_cast<size_t>(k)] = prof_co;
    }

    if (bp.vmesh_method == BevelVMeshMethod::Cutoff && use_map) {
        Vec3 bottom = mul_m4v3(map, v3(0, 0, 0));
        Vec3 top = mul_m4v3(map, v3(1, 1, 0));
        pro->height = static_cast<float>(length(sub(top, bottom)));
    } else if (bp.vmesh_method == BevelVMeshMethod::Cutoff) {
        pro->height = static_cast<float>(length(sub(pro->end, pro->start)) / std::sqrt(2.0));
    }

    // Calculate power-of-2 version if needed
    int seg_2 = ps.seg_2;
    if (seg_2 != seg && seg_2 > 0) {
        pro->prof_co_2.resize(static_cast<size_t>(seg_2 + 1));
        for (int k = 0; k <= seg_2; k++) {
            Vec3 co;
            if (k == 0) {
                co = pro->start;
            } else if (k == seg_2) {
                co = pro->end;
            } else if (use_map) {
                Vec3 p{ps.xvals_2[static_cast<size_t>(k)], ps.yvals_2[static_cast<size_t>(k)], 0.0};
                co = mul_m4v3(map, p);
            } else {
                double t = static_cast<double>(k) / seg_2;
                co = add(scale(pro->start, 1.0 - t), scale(pro->end, t));
            }
            Vec3 prof_co;
            if (length_squared(pro->proj_dir) > 1e-12) {
                if (!isect_line_plane_v3(prof_co, co, add(co, pro->proj_dir),
                                         pro->plane_co, pro->plane_no)) {
                    prof_co = co;
                }
            } else {
                prof_co = co;
            }
            pro->prof_co_2[static_cast<size_t>(k)] = prof_co;
        }
    }
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════════
// Section 9: VMesh Construction (Blender build_vmesh + adj_vmesh + cubic_subdiv)
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

void calculate_vm_profiles(BevelParams& bp, BevVert* bv, VMesh* vm) {
    BoundVert* bndv = vm->boundstart;
    do {
        if (!bndv->profile.special_params) {
            set_profile_params(bp, bv, bndv);
        }
        calculate_profile(bp, bndv);
    } while ((bndv = bndv->next) != vm->boundstart);
}

// Sabin gamma for center vertex (Blender sabin_gamma)
float sabin_gamma(int n) {
    if (n < 3) return 0.0f;
    if (n == 3) return 0.065247584f;
    if (n == 4) return 0.25f;
    if (n == 5) return 0.401983447f;
    if (n == 6) return 0.523423277f;

    double k = std::cos(M_PI / static_cast<double>(n));
    double k2 = k * k;
    double k4 = k2 * k2;
    double k6 = k4 * k2;
    double y = std::pow(std::sqrt(3.0) * std::sqrt(64.0*k6 - 144.0*k4 + 135.0*k2 - 27.0) + 9.0*k, 1.0/3.0);
    double x = 0.480749856769136 * y - (0.231120424783545 * (12.0*k2 - 9.0)) / y;
    return static_cast<float>((k * x + 2.0*k2 - 1.0) / (x*x * (k*x + 1.0)));
}

Vec3 avg4(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d) {
    return scale(add(add(a, b), add(c, d)), 0.25);
}

bool is_canon(VMesh* vm, int i, int j, int k) {
    int ns2 = vm->seg / 2;
    if (vm->seg % 2 == 1)
        return (j <= ns2 && k <= ns2);
    return ((j < ns2 && k <= ns2) || (j == ns2 && k == ns2 && i == 0));
}

NewVert& mesh_vert_canon(VMesh* vm, int i, int j, int k) {
    int n = vm->count;
    int ns = vm->seg;
    int ns2 = ns / 2;
    int odd = ns % 2;
    if (!odd && j == ns2 && k == ns2)
        return vm->at(0, j, k);
    if (j <= ns2 - 1 + odd && k <= ns2)
        return vm->at(i, j, k);
    if (k <= ns2)
        return vm->at((i + n - 1) % n, k, ns - j);
    return vm->at((i + 1) % n, ns - k, j);
}

void vmesh_copy_equiv_verts(VMesh* vm) {
    int n = vm->count;
    int ns = vm->seg;
    int ns2 = ns / 2;
    for (int i = 0; i < n; i++) {
        for (int j = 0; j <= ns2; j++) {
            for (int k = 0; k <= ns; k++) {
                if (is_canon(vm, i, j, k))
                    continue;
                vm->at(i, j, k).co = mesh_vert_canon(vm, i, j, k).co;
            }
        }
    }
}

/// Cubic subdivision (Blender cubic_subdiv) — Catmull-Clark style.
std::unique_ptr<VMesh> cubic_subdiv(BevelParams& bp, VMesh* vm_in) {
    int n_boundary = vm_in->count;
    int ns_in = vm_in->seg;
    int ns_in2 = ns_in / 2;
    int ns_out = 2 * ns_in;

    auto vm_out = std::make_unique<VMesh>();
    vm_out->seg = ns_out;
    vm_out->count = n_boundary;
    vm_out->mesh.assign(static_cast<size_t>(n_boundary) * (ns_out/2 + 1) * (ns_out + 1), NewVert{});
    vm_out->boundstart = vm_in->boundstart;

    // 1. Adjust even boundary vertices (smooth rule)
    for (int i = 0; i < n_boundary; i++) {
        vm_out->at(i, 0, 0).co = vm_in->at(i, 0, 0).co;
        for (int k = 1; k < ns_in; k++) {
            Vec3 co = vm_in->at(i, 0, k).co;
            // Smooth: co += -(co1 + co2 - 2*co) / 6
            Vec3 co1 = vm_in->at(i, 0, k - 1).co;
            Vec3 co2 = vm_in->at(i, 0, k + 1).co;
            Vec3 acc = add(co1, co2);
            acc = madd(acc, co, -2.0);
            co = madd(co, acc, -1.0 / 6.0);
            vm_out->at(i, 0, 2 * k).co = co;
        }
    }

    // 2. Adjust odd boundary vertices using profile
    BoundVert* bndv = vm_out->boundstart;
    for (int i = 0; i < n_boundary; i++) {
        for (int k = 1; k < ns_out; k += 2) {
            Vec3 co = get_profile_point(bndv->profile, k, ns_out, bp.seg);
            // Smooth
            Vec3 co1 = vm_out->at(i, 0, k - 1).co;
            Vec3 co2 = vm_out->at(i, 0, k + 1).co;
            Vec3 acc = add(co1, co2);
            acc = madd(acc, co, -2.0);
            co = madd(co, acc, -1.0 / 6.0);
            vm_out->at(i, 0, k).co = co;
        }
        bndv = bndv->next;
    }

    vmesh_copy_equiv_verts(vm_out.get());

    // Copy adjusted boundary back to vm_in
    for (int i = 0; i < n_boundary; i++) {
        for (int k = 0; k < ns_in; k++) {
            vm_in->at(i, 0, k).co = vm_out->at(i, 0, 2 * k).co;
        }
    }
    vmesh_copy_equiv_verts(vm_in);

    // 3. New face vertices (center of each face quad)
    for (int i = 0; i < n_boundary; i++) {
        for (int j = 0; j < ns_in2; j++) {
            for (int k = 0; k < ns_in2; k++) {
                Vec3 co = avg4(
                    vm_in->at(i, j, k).co,
                    vm_in->at(i, j, k + 1).co,
                    vm_in->at(i, j + 1, k).co,
                    vm_in->at(i, j + 1, k + 1).co);
                vm_out->at(i, 2*j + 1, 2*k + 1).co = co;
            }
        }
    }

    // 4. New vertical edge vertices
    for (int i = 0; i < n_boundary; i++) {
        for (int j = 0; j < ns_in2; j++) {
            for (int k = 1; k <= ns_in2; k++) {
                Vec3 co = avg4(
                    vm_in->at(i, j, k).co,
                    vm_in->at(i, j + 1, k).co,
                    vm_out->at(i, 2*j + 1, 2*k - 1).co,
                    vm_out->at(i, 2*j + 1, 2*k + 1).co);
                vm_out->at(i, 2*j + 1, 2*k).co = co;
            }
        }
    }

    // 5. New horizontal edge vertices
    for (int i = 0; i < n_boundary; i++) {
        for (int j = 1; j < ns_in2; j++) {
            for (int k = 0; k < ns_in2; k++) {
                Vec3 co = avg4(
                    vm_in->at(i, j, k).co,
                    vm_in->at(i, j, k + 1).co,
                    vm_out->at(i, 2*j - 1, 2*k + 1).co,
                    vm_out->at(i, 2*j + 1, 2*k + 1).co);
                vm_out->at(i, 2*j, 2*k + 1).co = co;
            }
        }
    }

    // 6. New interior vertices (Catmull-Clark)
    double gamma = 0.25;
    double beta = -gamma;
    for (int i = 0; i < n_boundary; i++) {
        for (int j = 1; j < ns_in2; j++) {
            for (int k = 1; k <= ns_in2; k++) {
                Vec3 co1 = avg4(
                    vm_out->at(i, 2*j, 2*k - 1).co,
                    vm_out->at(i, 2*j, 2*k + 1).co,
                    vm_out->at(i, 2*j - 1, 2*k).co,
                    vm_out->at(i, 2*j + 1, 2*k).co);
                Vec3 co2 = avg4(
                    vm_out->at(i, 2*j - 1, 2*k - 1).co,
                    vm_out->at(i, 2*j + 1, 2*k - 1).co,
                    vm_out->at(i, 2*j - 1, 2*k + 1).co,
                    vm_out->at(i, 2*j + 1, 2*k + 1).co);
                Vec3 co = co1; // alpha = 1.0
                co = madd(co, co2, beta);
                co = madd(co, vm_in->at(i, j, k).co, gamma);
                vm_out->at(i, 2*j, 2*k).co = co;
            }
        }
    }

    vmesh_copy_equiv_verts(vm_out.get());

    // 7. Center vertex (Sabin)
    gamma = sabin_gamma(n_boundary);
    beta = -gamma;
    Vec3 co1{0, 0, 0}, co2{0, 0, 0};
    for (int i = 0; i < n_boundary; i++) {
        co1 = add(co1, vm_out->at(i, ns_in, ns_in - 1).co);
        co2 = add(co2, vm_out->at(i, ns_in - 1, ns_in - 1).co);
        co2 = add(co2, vm_out->at(i, ns_in - 1, ns_in + 1).co);
    }
    Vec3 center_co = scale(co1, 1.0 / n_boundary);
    center_co = madd(center_co, co2, beta / (2.0 * n_boundary));
    center_co = madd(center_co, vm_in->at(0, ns_in2, ns_in2).co, gamma);
    for (int i = 0; i < n_boundary; i++) {
        vm_out->at(i, ns_in, ns_in).co = center_co;
    }

    // 8. Final: copy profile vertices to boundary
    bndv = vm_out->boundstart;
    for (int i = 0; i < n_boundary; i++) {
        int inext = (i + 1) % n_boundary;
        for (int k = 0; k <= ns_out; k++) {
            Vec3 co = get_profile_point(bndv->profile, k, ns_out, bp.seg);
            vm_out->at(i, 0, k).co = co;
            if (k >= ns_in && k < ns_out) {
                vm_out->at(inext, ns_out - k, 0).co = co;
            }
        }
        bndv = bndv->next;
    }

    return vm_out;
}

/// Build ADJ vertex mesh using subdivision (Blender adj_vmesh).
std::unique_ptr<VMesh> adj_vmesh(BevelParams& bp, BevVert* bv) {
    int n_bndv = bv->vmesh->count;
    int nseg = bv->vmesh->seg;

    // First construct a 2-segment control mesh
    auto vm0 = std::make_unique<VMesh>();
    vm0->seg = 2;
    vm0->count = n_bndv;
    vm0->mesh.assign(static_cast<size_t>(n_bndv) * 2 * 3, NewVert{});
    vm0->boundstart = bv->vmesh->boundstart;

    // Find center of boundary verts
    Vec3 boundverts_center{0, 0, 0};
    BoundVert* bndv = vm0->boundstart;
    for (int i = 0; i < n_bndv; i++) {
        vm0->at(i, 0, 0).co = bndv->nv.co;
        vm0->at(i, 0, 1).co = get_profile_point(bndv->profile, 1, 2, bp.seg);
        boundverts_center = add(boundverts_center, bndv->nv.co);
        bndv = bndv->next;
    }
    boundverts_center = scale(boundverts_center, 1.0 / n_bndv);

    // Place center vertex using fullness
    Vec3 original_vertex = (*bp.positions)[static_cast<size_t>(bv->v_idx)];
    Vec3 negative_fullest = sub(boundverts_center, original_vertex);
    negative_fullest = add(negative_fullest, boundverts_center);

    float fullness = bp.pro_spacing.fullness;
    Vec3 center_direction = sub(original_vertex, boundverts_center);
    if (length_squared(center_direction) > BEVEL_EPSILON_SQ) {
        vm0->at(0, 1, 1).co = madd(boundverts_center, center_direction, fullness);
    } else {
        vm0->at(0, 1, 1).co = boundverts_center;
    }
    vmesh_copy_equiv_verts(vm0.get());

    // Subdivide until reaching target segments
    std::unique_ptr<VMesh> vm1 = std::move(vm0);
    do {
        vm1 = cubic_subdiv(bp, vm1.get());
    } while (vm1->seg < nseg);

    return vm1;
}

/// Build cutoff VMesh corner caps (Blender bevel_build_cutoff).
void bevel_build_cutoff(BevelParams& bp, BevVert* bv) {
    VMesh* vm = bv->vmesh.get();
    if (!vm || !vm->boundstart)
        return;

    int n_bndv = vm->count;
    int ns = bp.seg;
    Vec3 vert_normal = vertex_normal_from_data(*bp.positions, *bp.triangles, bv->v_idx);

    BoundVert* bndv = vm->boundstart;
    do {
        int i = bndv->index;
        Vec3 down_dir = cross(bndv->profile.plane_no, bndv->prev->profile.plane_no);
        if (length_squared(down_dir) < 1e-12)
            down_dir = vert_normal;
        else if (dot(down_dir, vert_normal) > 0.0)
            down_dir = negate(down_dir);
        Vec3 to_center = sub(bp.mesh_center, bndv->nv.co);
        if (length_squared(to_center) > 1e-18 && dot(down_dir, to_center) < 0.0)
            down_dir = negate(down_dir);
        down_dir = normalize(down_dir);

        float corner_len = (bndv->profile.height / std::sqrt(2.0f) +
                            bndv->prev->profile.height / std::sqrt(2.0f)) * 0.5f;
        if (corner_len < 1e-9f)
            corner_len = static_cast<float>(bp.offset) * 0.5f;

        Vec3 corner = madd(bndv->nv.co, down_dir, corner_len);
        vm->at(i, 1, 0).co = corner;
        vm->at(bndv->prev->index, 1, 1).co = corner;
    } while ((bndv = bndv->next) != vm->boundstart);

    bool build_center_face = true;
    if (n_bndv == 3) {
        build_center_face &= length_squared(sub(vm->at(0, 1, 0).co, vm->at(1, 1, 0).co)) > BEVEL_EPSILON_SQ;
        build_center_face &= length_squared(sub(vm->at(0, 1, 0).co, vm->at(2, 1, 0).co)) > BEVEL_EPSILON_SQ;
        build_center_face &= length_squared(sub(vm->at(1, 1, 0).co, vm->at(2, 1, 0).co)) > BEVEL_EPSILON_SQ;
    }

    bndv = vm->boundstart;
    do {
        int i = bndv->index;
        std::vector<Vec3> face_verts;
        face_verts.push_back(vm->at(i, 1, 0).co);
        for (int k = 0; k <= ns; k++)
            face_verts.push_back(get_profile_point(bndv->profile, k, ns, bp.seg));
        if (build_center_face)
            face_verts.push_back(vm->at(i, 1, 1).co);

        for (size_t f = 1; f + 1 < face_verts.size(); ++f) {
            bp.output.add_oriented_triangle(
                face_verts[0], face_verts[f], face_verts[f + 1], vert_normal);
        }
    } while ((bndv = bndv->next) != vm->boundstart);

    if (build_center_face && n_bndv >= 3) {
        std::vector<Vec3> center_verts;
        for (int i = 0; i < n_bndv; i++)
            center_verts.push_back(vm->at(i, 1, 0).co);
        for (size_t f = 1; f + 1 < center_verts.size(); ++f) {
            bp.output.add_oriented_triangle(
                center_verts[0], center_verts[f], center_verts[f + 1], vert_normal);
        }
    }
}

/// Build a simple polygon (Blender bevel_build_poly L6263).
/// Walks the bound ring and inserts ebev profile points from VMesh ring k=1..ns-1.
void bevel_build_poly(BevelParams& bp, BevVert* bv) {
    VMesh* vm = bv->vmesh.get();
    const int ns = vm->seg;

    std::vector<Vec3> poly;
    BoundVert* bndv = vm->boundstart;
    do {
        poly.push_back(bndv->nv.co);
        if (bndv->ebev && ns > 1) {
            const int i = bndv->index;
            for (int k = 1; k < ns; ++k) {
                const NewVert& nv = vm->at(i, 0, k);
                if (nv.valid)
                    poly.push_back(nv.co);
            }
        }
    } while ((bndv = bndv->next) != vm->boundstart);

    if (poly.size() < 3)
        return;

    Vec3 normal = vertex_normal({*bp.positions, *bp.triangles}, bv->v_idx);
    for (size_t f = 1; f + 1 < poly.size(); ++f) {
        bp.output.add_oriented_triangle(poly[0], poly[f], poly[f + 1], normal);
    }
}

/// Build a triangle fan (Blender bevel_build_trifan L6342).
/// Terminal selcount==1 keeps legacy center fan (profile-aware poly caused strip winding clash).
/// Poly kind uses profile-aware bevel_build_poly.
void bevel_build_trifan(BevelParams& bp, BevVert* bv) {
    if (bv->selcount == 1) {
        VMesh* vm = bv->vmesh.get();
        const int n = vm->count;

        Vec3 center{0, 0, 0};
        BoundVert* bndv = vm->boundstart;
        do {
            center = add(center, bndv->nv.co);
        } while ((bndv = bndv->next) != vm->boundstart);
        center = scale(center, 1.0 / n);

        Vec3 normal = vertex_normal({*bp.positions, *bp.triangles}, bv->v_idx);

        bndv = vm->boundstart;
        Vec3 prev = bndv->nv.co;
        bndv = bndv->next;
        do {
            Vec3 curr = bndv->nv.co;
            bp.output.add_oriented_triangle(center, prev, curr, normal);
            prev = curr;
        } while ((bndv = bndv->next) != vm->boundstart);
        return;
    }

    bevel_build_poly(bp, bv);
}

/// Build VMesh — the core function that creates the corner mesh.
void build_vmesh(BevelParams& bp, BevVert* bv) {
    VMesh* vm = bv->vmesh.get();
    int n = vm->count;
    int ns = vm->seg;
    int ns2 = ns / 2;
    const Vec3 vert_normal = vertex_normal_from_data(*bp.positions, *bp.triangles, bv->v_idx);
    const bool weld = (bv->selcount == 2) && (n == 2);
    BoundVert* weld1 = nullptr;
    BoundVert* weld2 = nullptr;

    // Allocate mesh array
    vm->mesh.assign(static_cast<size_t>(n) * (ns2 + 1) * (ns + 1), NewVert{});

    // (i, 0, 0) = boundary vert
    BoundVert* bndv = vm->boundstart;
    do {
        int i = bndv->index;
        vm->at(i, 0, 0).co = bndv->nv.co;
        vm->at(i, 0, 0).valid = true;
        if (weld && bndv->ebev) {
            if (!weld1)
                weld1 = bndv;
            else
                weld2 = bndv;
        }
    } while ((bndv = bndv->next) != vm->boundstart);

    if (bv->selcount == 1 && bv->edgecount >= 3 && vm->boundstart->ebev) {
        set_profile_params(bp, bv, vm->boundstart);
        move_profile_plane(vm->boundstart, (*bp.positions)[static_cast<size_t>(bv->v_idx)]);
    }

    // Calculate all profiles
    calculate_vm_profiles(bp, bv, vm);

    // Create (i, 0, k) from profiles
    bndv = vm->boundstart;
    do {
        int i = bndv->index;
        vm->at(i, 0, ns).co = bndv->next->nv.co;  // End = next boundary vert
        vm->at(i, 0, ns).valid = true;

        if (vm->mesh_kind != MeshKind::Adj) {
            for (int k = 1; k < ns; k++) {
                if (bndv->ebev) {
                    vm->at(i, 0, k).co = get_profile_point(bndv->profile, k, ns, bp.seg);
                    vm->at(i, 0, k).valid = true;
                } else if (n == 2) {
                    vm->at(i, 0, k).co = vm->at(1 - i, 0, ns - k).co;
                    vm->at(i, 0, k).valid = vm->at(1 - i, 0, ns - k).valid;
                }
            }
        }
    } while ((bndv = bndv->next) != vm->boundstart);

    if (weld && weld1 && weld2) {
        vm->mesh_kind = MeshKind::None;
        for (int k = 1; k < ns; ++k) {
            const Vec3 co = mid(vm->at(weld1->index, 0, k).co,
                                vm->at(weld2->index, 0, ns - k).co);
            vm->at(weld1->index, 0, k).co = co;
            vm->at(weld1->index, 0, k).valid = true;
            vm->at(weld2->index, 0, ns - k).co = co;
            vm->at(weld2->index, 0, ns - k).valid = true;
        }
    }

    // Build based on mesh_kind
    switch (vm->mesh_kind) {
    case MeshKind::None:
        break;
    case MeshKind::Poly:
        bevel_build_poly(bp, bv);
        break;
    case MeshKind::Adj: {
        auto vm_adj = adj_vmesh(bp, bv);

        // Copy final vmesh into bv->vmesh and create output triangles
        VMesh* vm_out = bv->vmesh.get();
        for (int i = 0; i < n; i++) {
            for (int j = 0; j <= ns2; j++) {
                for (int k = 0; k <= ns; k++) {
                    if (j == 0 && (k == 0 || k == ns)) continue;
                    vm_out->at(i, j, k).co = vm_adj->at(i, j, k).co;
                    vm_out->at(i, j, k).valid = vm_adj->at(i, j, k).valid;
                }
            }
        }

        // Create F_VERT quads with Blender loop order (v1,v2,v3,v4) — no mesh-center flip.
        const int odd = ns % 2;
        bndv = vm_out->boundstart;
        do {
            const int i = bndv->index;
            for (int j = 0; j < ns2; j++) {
                for (int k = 0; k < ns2 + odd; k++) {
                    const Vec3 v1 = vm_out->at(i, j, k).co;
                    const Vec3 v2 = vm_out->at(i, j, k + 1).co;
                    const Vec3 v3 = vm_out->at(i, j + 1, k + 1).co;
                    const Vec3 v4 = vm_out->at(i, j + 1, k).co;
                    if (length_squared(sub(v1, v2)) > 1e-16 ||
                        length_squared(sub(v3, v4)) > 1e-16) {
                        bp.output.add_oriented_quad(v1, v2, v3, v4, vert_normal);
                    }
                }
            }
        } while ((bndv = bndv->next) != vm_out->boundstart);

        // Center ngon once (Blender builds it after the ring loop).
        if (odd) {
            std::vector<Vec3> center_verts;
            center_verts.reserve(static_cast<size_t>(n));
            for (int i2 = 0; i2 < n; i2++)
                center_verts.push_back(vm_out->at(i2, ns2, ns2).co);
            bp.output.add_polygon(center_verts);
        }
        break;
    }
    case MeshKind::TriFan:
        bevel_build_trifan(bp, bv);
        break;
    case MeshKind::Cutoff:
        bevel_build_cutoff(bp, bv);
        break;
    }
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════════
// Section 10: Edge Polygons + Face Reconstruction
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

/// Build the bevel strip for a beveled edge (Blender bevel_build_edge_polygons).
///
/// The strip connects the profile at bv1's end to the profile at bv2's end.
/// At bv1: e->leftv has ebev == e — its profile goes along edge e.
/// At bv2: find the EdgeHalf for the same edge, use its leftv.
/// Profile direction is REVERSED at bv2 because the edge direction is opposite:
///   bv1's profile[0] is on face A, bv2's profile[ns] is on face A.
///   So bv1[k] corresponds to bv2[ns-k].
void build_edge_polygons(BevelParams& bp, BevVert* bv1, BevVert* bv2, EdgeHalf* e) {
    if (!e->leftv || !e->rightv)
        return;

    // Find EdgeHalf at bv2 for the same edge (edge_v1 points back to bv1)
    EdgeHalf* e2 = nullptr;
    for (auto& eh : bv2->edges) {
        if (eh.edge_v1 == bv1->v_idx && eh.is_bev) {
            e2 = &eh;
            break;
        }
    }
    if (!e2 || !e2->leftv || !e2->rightv)
        return;

    VMesh* vm1 = bv1->vmesh.get();
    VMesh* vm2 = bv2->vmesh.get();
    if (!vm1 || !vm2)
        return;

    const int ns = bp.seg;
    if (ns < 1)
        return;

    // Blender bevel_build_edge_polygons: profile rows i1/i2 from leftv; corners use
    // e1->{leftv,rightv} and e2->{rightv,leftv}.
    const int i1 = e->leftv->index;
    const int i2 = e2->leftv->index;

    Vec3 strip_normal{0.0, 0.0, 0.0};
    if (e->fprev >= 0)
        strip_normal = add(strip_normal, bmesh_face_normal(bp, e->fprev));
    if (e->fnext >= 0)
        strip_normal = add(strip_normal, bmesh_face_normal(bp, e->fnext));
    if (length_squared(strip_normal) < 1e-20) {
        strip_normal = add(vertex_normal_from_data(*bp.positions, *bp.triangles, bv1->v_idx),
                           vertex_normal_from_data(*bp.positions, *bp.triangles, bv2->v_idx));
    }

    auto emit_strip_quad = [&](const Vec3& v0, const Vec3& v1, const Vec3& v2, const Vec3& v3) {
        const bool collapsed_left = length_squared(sub(v0, v3)) <= 1e-16;
        const bool collapsed_right = length_squared(sub(v1, v2)) <= 1e-16;

        Vec3 desired_normal = strip_normal;
        if (length_squared(desired_normal) < 1e-20)
            desired_normal = cross(sub(v1, v0), sub(v3, v0));

        if (!collapsed_left && !collapsed_right) {
            bp.output.add_oriented_quad(v0, v1, v2, v3, desired_normal);
            return;
        }

        if (collapsed_left && collapsed_right) {
            if (length_squared(sub(v0, v1)) > 1e-16 && length_squared(sub(v1, v2)) > 1e-16)
                bp.output.add_oriented_triangle(v0, v1, v2, desired_normal);
            return;
        }
        if (collapsed_left) {
            // v0 == v3: degenerate quad becomes triangle (v0, v1, v2).
            if (length_squared(sub(v0, v1)) > 1e-16 && length_squared(sub(v1, v2)) > 1e-16)
                bp.output.add_oriented_triangle(v0, v1, v2, desired_normal);
            return;
        }
        // collapsed_right: v1 == v2 → triangle (v0, v1, v3).
        if (length_squared(sub(v0, v1)) > 1e-16 && length_squared(sub(v0, v3)) > 1e-16)
            bp.output.add_oriented_triangle(v0, v1, v3, desired_normal);
    };

    Vec3 v0 = e->leftv->nv.co;
    Vec3 v1 = e2->rightv->nv.co;

    for (int k = 1; k <= ns; ++k) {
        const NewVert& nv3 = vm1->at(i1, 0, k);
        const NewVert& nv2 = vm2->at(i2, 0, ns - k);
        if (!nv3.valid || !nv2.valid)
            continue;
        const Vec3 v3 = nv3.co;
        const Vec3 v2 = nv2.co;
        emit_strip_quad(v0, v1, v2, v3);
        v0 = v3;
        v1 = v2;
    }
}

/// Reconstruct original faces using BMesh face identity on EdgeHalf.fprev/fnext
/// (Blender BMFace*). Profile insertion matches prior working structure; face
/// membership uses polygon indices — never welded triangle indices.
void rebuild_faces_bmesh(BevelParams& bp, const geometry::BMesh& bmesh, const WeldedMesh& welded)
{
    const CapExtents cap = mesh_cap_extents(welded.positions);
    const double cap_tol = 0.05;
    auto find_edge_half = [&](BevVert* bv, int other_v) -> EdgeHalf* {
        if (!bv)
            return nullptr;
        for (auto& edge : bv->edges) {
            if (edge.edge_v1 == other_v)
                return &edge;
        }
        return nullptr;
    };

    auto count_ccw_edges_between = [](EdgeHalf* from, EdgeHalf* to) -> int {
        if (!from || !to)
            return 1 << 20;
        int count = 0;
        EdgeHalf* edge = from;
        while (edge != to && count < 1024) {
            edge = edge->next;
            ++count;
            if (edge == from)
                break;
        }
        return edge == to ? count : (1 << 20);
    };

    for (int fi = 0; fi < static_cast<int>(bmesh.faces.size()); ++fi) {
        const auto& bface = bmesh.faces[static_cast<size_t>(fi)];
        if (bface.verts.size() < 3)
            continue;

        const std::vector<int>& loop = bface.verts;
        const geometry::Vec3 gn = geometry::face_normal_from_loop(bmesh, loop);
        const Vec3 face_normal = {gn.x, gn.y, gn.z};

        bool is_cap_face = !loop.empty();
        for (int vi : loop) {
            if (!on_cap_plane_x(welded.positions[static_cast<size_t>(vi)], cap, cap_tol)) {
                is_cap_face = false;
                break;
            }
        }

        std::vector<Vec3> polygon;
        auto push_distinct = [&](const Vec3& p) {
            if (polygon.empty() || length_squared(sub(p, polygon.back())) > 1e-18)
                polygon.push_back(p);
        };

        for (size_t i = 0; i < loop.size(); ++i) {
            const int prev_v = loop[(i + loop.size() - 1) % loop.size()];
            const int curr_v = loop[i];
            const int next_v = loop[(i + 1) % loop.size()];
            BevVert* bv = find_bevvert(bp, curr_v);
            VMesh* vm = bv ? bv->vmesh.get() : nullptr;
            EdgeHalf* edge = find_edge_half(bv, next_v);
            EdgeHalf* edge_prev = find_edge_half(bv, prev_v);
            if (!bv || !vm || !vm->boundstart || !edge || !edge_prev) {
                push_distinct(welded.positions[static_cast<size_t>(curr_v)]);
                continue;
            }

            bool go_ccw = false;
            if (edge->prev == edge_prev) {
                if (edge_prev->prev == edge) {
                    go_ccw = (edge->fnext != fi);
                } else {
                    go_ccw = true;
                }
            } else if (edge_prev->prev == edge) {
                go_ccw = false;
            } else {
                go_ccw = count_ccw_edges_between(edge_prev, edge) <
                         count_ccw_edges_between(edge, edge_prev);
            }

            bool on_profile_start = false;
            BoundVert* vstart = nullptr;
            BoundVert* vend = nullptr;
            if (go_ccw) {
                vstart = edge_prev->rightv;
                vend = edge->leftv;
                if (edge->profile_index > 0 && vstart) {
                    vstart = vstart->prev;
                    on_profile_start = true;
                }
            } else {
                vstart = edge_prev->leftv;
                vend = edge->rightv;
                if (edge_prev->profile_index > 0 && vstart) {
                    vstart = vstart->next;
                    on_profile_start = true;
                }
            }

            if (!vstart || !vend) {
                push_distinct(welded.positions[static_cast<size_t>(curr_v)]);
                continue;
            }

            BoundVert* ebev_bnd = nullptr;
            if (is_cap_face && bv->selcount == 1 && bv->edgecount >= 3) {
                BoundVert* walk = vm->boundstart;
                do {
                    if (walk->ebev) {
                        ebev_bnd = walk;
                        break;
                    }
                    walk = walk->next;
                } while (walk != vm->boundstart);
            }

            const bool short_ccw = ebev_bnd && go_ccw && vstart != vend &&
                                   vstart->next == vend && vstart != ebev_bnd;
            const bool short_cw = ebev_bnd && !go_ccw && vstart != vend &&
                                  vstart->prev == vend && vstart != ebev_bnd;

            if (bevel_diag_enabled() && is_cap_face && bv->selcount == 1 && bv->edgecount >= 3) {
                std::fprintf(stderr,
                             "[PCG_BEVEL_DIAG] cap_corner fi=%d curr_v=%d go_ccw=%d "
                             "edge(fprev=%d fnext=%d prof=%d) edge_prev(fprev=%d fnext=%d prof=%d) "
                             "vstart_idx=%d vend_idx=%d short=%d polygon_before=%zu\n",
                             fi, curr_v, go_ccw ? 1 : 0,
                             edge->fprev, edge->fnext, edge->profile_index,
                             edge_prev->fprev, edge_prev->fnext, edge_prev->profile_index,
                             vstart->index, vend->index, (short_ccw || short_cw) ? 1 : 0,
                             polygon.size());
            }

            if (short_ccw || short_cw) {
                const int ri = ebev_bnd->index;
                if (short_ccw) {
                    for (int k = vm->seg; k >= 0; --k) {
                        const NewVert& nv = vm->at(ri, 0, k);
                        if (nv.valid)
                            push_distinct(nv.co);
                    }
                } else {
                    for (int k = 0; k <= vm->seg; ++k) {
                        const NewVert& nv = vm->at(ri, 0, k);
                        if (nv.valid)
                            push_distinct(nv.co);
                    }
                }
            } else {
                BoundVert* v = vstart;
                if (!on_profile_start)
                    push_distinct(v->nv.co);

                int guard = 0;
                while (v != vend && guard++ < vm->count + 2) {
                    if (go_ccw) {
                        const int ring_index = v->index;
                        const int kstart = on_profile_start ? edge->profile_index : 1;
                        const int kend = (edge_prev->rightv == v && edge_prev->profile_index > 0) ?
                                             edge_prev->profile_index :
                                             vm->seg;
                        on_profile_start = false;
                        for (int k = kstart; k <= kend; ++k) {
                            const NewVert& nv = vm->at(ring_index, 0, k);
                            if (nv.valid)
                                push_distinct(nv.co);
                        }
                        v = v->next;
                    } else {
                        const int ring_index = v->prev ? v->prev->index : v->index;
                        const int kstart = on_profile_start ? edge_prev->profile_index : (vm->seg - 1);
                        const int kend = (v->prev && edge->rightv == v->prev && edge->profile_index > 0) ?
                                             edge->profile_index :
                                             0;
                        on_profile_start = false;
                        for (int k = kstart; k >= kend; --k) {
                            const NewVert& nv = vm->at(ring_index, 0, k);
                            if (nv.valid)
                                push_distinct(nv.co);
                        }
                        v = v->prev;
                    }
                }
            }
        }

        if (polygon.size() >= 3 && length_squared(sub(polygon.front(), polygon.back())) < 1e-18)
            polygon.pop_back();

        if (polygon.size() >= 3)
            bp.output.add_oriented_polygon(polygon, face_normal);
    }
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════════
// Section 11: Main Entry Point (Blender BM_mesh_bevel)
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

bool group_name_matches(const std::string& pattern, const std::string& name)
{
    if (pattern.size() >= 2 && pattern.back() == '*' && pattern.find('*') == pattern.size() - 1) {
        const std::string prefix = pattern.substr(0, pattern.size() - 1);
        return name.size() >= prefix.size() && name.compare(0, prefix.size(), prefix) == 0;
    }
    return pattern == name;
}

bool edge_excluded(const geometry::BMesh& bmesh,
                   const geometry::BMeshEdge& edge,
                   const BevelEdgeSelection& selection)
{
    if (selection.exclude_unshared && edge.face1 < 0)
        return true;

    // Check edge-level groups
    for (const std::string& pattern : selection.exclude_groups) {
        for (const std::string& group : edge.groups) {
            if (group_name_matches(pattern, group))
                return true;
        }
    }

    // Also check face-level groups: if both adjacent faces of this edge belong
    // to an excluded face group (e.g. "cap_start"), exclude the edge.
    // This lets excludeGroups="cap_start,cap_end" exclude cap rim edges,
    // since SweepAlongSpline puts cap_start/cap_end on faces, not edges.
    for (const std::string& pattern : selection.exclude_groups) {
        bool face0_matches = false, face1_matches = false;
        if (edge.face0 >= 0 && edge.face0 < static_cast<int>(bmesh.faces.size())) {
            for (const std::string& g : bmesh.faces[static_cast<size_t>(edge.face0)].groups) {
                if (group_name_matches(pattern, g)) { face0_matches = true; break; }
            }
        }
        if (edge.face1 >= 0 && edge.face1 < static_cast<int>(bmesh.faces.size())) {
            for (const std::string& g : bmesh.faces[static_cast<size_t>(edge.face1)].groups) {
                if (group_name_matches(pattern, g)) { face1_matches = true; break; }
            }
        }
        // Exclude if at least one adjacent face is in the excluded group.
        // This prevents beveling cap rim edges (between cap face and side face).
        if (face0_matches || face1_matches)
            return true;
    }

    return false;
}

std::unordered_set<int64_t> select_hard_edges(const geometry::BMesh& bmesh,
                                              const BevelEdgeSelection& selection,
                                              const data::PcgGeometry* geometry)
{
    std::unordered_set<int64_t> hard_edges;

    if (!selection.edge_group.empty() && geometry) {
        const auto selected =
            geometry->groups().eval(geometry::GroupDomain::Edge, selection.edge_group);
        for (int64_t key : selected) {
            const auto it = bmesh.edges.find(key);
            if (it == bmesh.edges.end())
                continue;
            if (!edge_excluded(bmesh, it->second, selection))
                hard_edges.insert(key);
        }
        return hard_edges;
    }

    for (const auto& entry : bmesh.edges) {
        if (!entry.second.sharp)
            continue;
        if (edge_excluded(bmesh, entry.second, selection))
            continue;
        hard_edges.insert(entry.first);
    }

    return hard_edges;
}

} // namespace

data::PcgMeshData bevel_mesh_blender(
    const data::PcgMeshData& mesh,
    double amount,
    int segments,
    BevelOffsetType offset_type,
    bool clamp_overlap,
    double angle_limit_deg,
    float profile,
    BevelMiter miter_outer,
    BevelMiter miter_inner,
    BevelVMeshMethod vmesh_method,
    bool (*is_cancel_requested)(),
    const BevelEdgeSelection& edge_selection,
    const data::PcgGeometry* geometry)
{
    amount = std::max(amount, 0.0);
    segments = std::clamp(segments, 1, 8);
    if (amount <= 1e-9)
        return mesh;
    // Convert profile (0=square_in, 0.5=circle, 1=square_out) to super_r
    // Blender: pro_super_r = -log(2) / log(sqrt(profile))
    float super_r;
    if (profile >= 0.95f)
        super_r = PRO_SQUARE_R;
    else if (std::abs(profile - 0.5f) < 1e-4)
        super_r = PRO_CIRCLE_R;
    else if (profile < 0.01f)
        super_r = PRO_SQUARE_IN_R;
    else {
        // Convert: profile 0..1 → super_r
        super_r = static_cast<float>(-std::log(2.0) / std::log(std::sqrt(profile)));
        if (std::abs(super_r - PRO_CIRCLE_R) < 1e-4) super_r = PRO_CIRCLE_R;
        else if (std::abs(super_r - PRO_LINE_R) < 1e-4) super_r = PRO_LINE_R;
    }

    // 1. Build BMesh (weld + coplanar merge + sharp edges)
    geometry::BMeshBuildOptions bmesh_opts;
    bmesh_opts.sharp_angle_deg = angle_limit_deg;
    const geometry::BMesh bmesh = geometry
        ? geometry::bmesh_from_geometry(*geometry, bmesh_opts)
        : geometry::bmesh_from_mesh(mesh, bmesh_opts);

    // 2. Weld mesh for bevel internals
    WeldedMesh welded = weld_mesh(mesh);
    if (welded.triangles.empty())
        return mesh;

    // 3. Build edge→face adjacency
    auto edge_faces = build_edge_faces(welded);

    // 4. Hard edges from BMesh topology + group selection
    const std::unordered_set<int64_t> hard_edges =
        select_hard_edges(bmesh, edge_selection, geometry);

    if (hard_edges.empty())
        return mesh;

    Vec3 box_min{}, box_max{};
    if (std::abs(profile - 0.5f) < 1e-4f && hard_edges.size() == bmesh.edges.size() &&
        bmesh_is_axis_aligned_box(bmesh, box_min, box_max)) {
        return build_axis_aligned_rounded_box(box_min, box_max, amount, segments);
    }

    Vec3 mesh_center{0.0, 0.0, 0.0};
    for (const auto& p : welded.positions)
        mesh_center = add(mesh_center, p);
    if (!welded.positions.empty())
        mesh_center = scale(mesh_center, 1.0 / static_cast<double>(welded.positions.size()));

    // 5. Setup BevelParams
    BevelParams bp;
    bp.offset = amount;
    bp.offset_type = offset_type;
    bp.seg = segments;
    bp.profile = profile;
    bp.pro_super_r = super_r;
    bp.loop_slide = true;
    bp.limit_offset = clamp_overlap;
    bp.offset_adjust = (offset_type != BevelOffsetType::Width);
    bp.miter_outer = miter_outer;
    bp.miter_inner = miter_inner;
    bp.vmesh_method = vmesh_method;
    bp.is_cancel_requested = is_cancel_requested;
    bp.mesh_center = mesh_center;
    bp.bmesh = &bmesh;
    bp.positions = &welded.positions;
    bp.triangles = &welded.triangles;
    bp.edge_faces = edge_faces;
    bp.hard_edges = hard_edges;

    // Set profile spacing
    set_profile_spacing(segments, super_r, bp.pro_spacing);

    // 5. Build BevVerts
    // Find all vertices that are endpoints of hard edges
    std::set<int> bevel_verts;
    for (int64_t key : hard_edges) {
        int v0 = static_cast<int>(key / 1000000);
        int v1 = static_cast<int>(key % 1000000);
        bevel_verts.insert(v0);
        bevel_verts.insert(v1);
    }

    // For each beveled vertex, build EdgeHalf array
    for (int v_idx : bevel_verts) {
        if (bevel_cancel_requested(bp))
            return mesh;
        BevVert bv;
        bv.v_idx = v_idx;

        // Use BMesh disk cycle for edge ordering — pure topology, no normal projection.
        // Falls back to get_bmesh_vert_edges if disk cycle is missing (degenerate mesh).
        std::vector<VertEdge> vert_edges;
        const auto disk_it = bmesh.disk_cycles.find(v_idx);
        if (disk_it != bmesh.disk_cycles.end() && !disk_it->second.empty()) {
            vert_edges.reserve(disk_it->second.size());
            for (const auto& de : disk_it->second) {
                VertEdge ve;
                ve.other_v = de.other_v;
                ve.edge_key = edge_key(v_idx, de.other_v);
                ve.edge_v0 = v_idx;
                ve.edge_v1 = de.other_v;
                vert_edges.push_back(ve);
            }
        } else {
            vert_edges = get_bmesh_vert_edges(v_idx, bmesh);
            vert_edges = sort_edges_disk_cycle(v_idx, vert_edges, bmesh,
                                                welded.positions, welded.triangles);
        }

        bv.edgecount = static_cast<int>(vert_edges.size());
        bv.edges.resize(vert_edges.size());

        for (size_t i = 0; i < vert_edges.size(); i++) {
            EdgeHalf& eh = bv.edges[i];
            eh.edge_v0 = v_idx;
            eh.edge_v1 = vert_edges[i].other_v;

            // Check if this edge is beveled (hard)
            int64_t key = edge_key(v_idx, vert_edges[i].other_v);
            eh.is_bev = hard_edges.count(key) > 0;

            // fprev/fnext from disk cycle (O(1)), fallback to search.
            if (disk_it != bmesh.disk_cycles.end() && i < disk_it->second.size()) {
                eh.fprev = disk_it->second[i].fprev;
                eh.fnext = disk_it->second[i].fnext;
            } else {
                const int prev_idx = (i == 0) ? static_cast<int>(vert_edges.size()) - 1 : static_cast<int>(i) - 1;
                const int next_idx = (static_cast<int>(i) + 1) % static_cast<int>(vert_edges.size());
                eh.fprev = find_bmesh_face_between(
                    bmesh, v_idx, vert_edges[static_cast<size_t>(prev_idx)].other_v, vert_edges[i].other_v);
                eh.fnext = find_bmesh_face_between(
                    bmesh, v_idx, vert_edges[i].other_v, vert_edges[static_cast<size_t>(next_idx)].other_v);
            }

            // Set initial offsets
            if (eh.is_bev) {
                float off = static_cast<float>(amount);
                if (offset_type == BevelOffsetType::Width) {
                    // Convert width to offset
                    double angle = M_PI / 2.0; // Default for 90-degree edges
                    if (eh.fprev >= 0 && eh.fnext >= 0) {
                        Vec3 n0 = bmesh_face_normal(bp, eh.fprev);
                        Vec3 n1 = bmesh_face_normal(bp, eh.fnext);
                        angle = std::acos(std::clamp(-dot(n0, n1), -1.0, 1.0));
                    }
                    double denom = 2.0 * std::sin(angle * 0.5);
                    off = static_cast<float>(denom > 1e-9 ? amount / denom : amount);
                }
                eh.offset_l = eh.offset_l_spec = off;
                eh.offset_r = eh.offset_r_spec = off;
                bv.selcount++;
            }

            eh.seg = segments;
            eh.is_rev = false;
        }

        // Set up CCW linked list
        for (size_t i = 0; i < bv.edges.size(); i++) {
            bv.edges[i].next = &bv.edges[(i + 1) % bv.edges.size()];
            bv.edges[i].prev = &bv.edges[(i + bv.edges.size() - 1) % bv.edges.size()];
        }

        bp.bevverts.push_back(std::move(bv));
    }

    // 6. Build boundaries (first pass: construct=true)
    for (auto& bv : bp.bevverts) {
        if (bevel_cancel_requested(bp))
            return mesh;
        bv.vmesh = std::make_unique<VMesh>();
        bv.vmesh->seg = segments;
        build_boundary(bp, &bv, true);
    }

    // 7. Limit offset (clamp_overlap)
    if (clamp_overlap) {
        bevel_limit_offset(bp);
        for (auto& bv : bp.bevverts) {
            if (bevel_cancel_requested(bp))
                return mesh;
            build_boundary(bp, &bv, false);
        }
    }

    if (bp.offset_adjust) {
        adjust_offsets(bp);
    }

    // 8. Build VMesh (corner meshes)
    for (auto& bv : bp.bevverts) {
        if (bevel_cancel_requested(bp))
            return mesh;
        if (bv.selcount == 0) continue;
        build_vmesh(bp, &bv);
    }

    const CapExtents diag_cap = mesh_cap_extents(welded.positions);
    diag_terminal_verts_after_vmesh(bp);

    // 9. Build edge polygons (bevel strips) — each edge only once
    std::set<std::pair<int, int>> processed_edges;
    for (auto& bv : bp.bevverts) {
        if (bevel_cancel_requested(bp))
            return mesh;
        for (auto& e : bv.edges) {
            if (!e.is_bev) continue;

            // Only process from the vertex with smaller index to avoid duplicates
            auto key_pair = std::minmax(bv.v_idx, e.edge_v1);
            if (processed_edges.count(key_pair))
                continue;
            processed_edges.insert(key_pair);

            // Find the BevVert at the other end
            BevVert* bv2 = nullptr;
            for (auto& b : bp.bevverts) {
                if (b.v_idx == e.edge_v1) {
                    bv2 = &b;
                    break;
                }
            }

            if (bv2) {
                if (bevel_cancel_requested(bp))
                    return mesh;
                build_edge_polygons(bp, &bv, bv2, &e);
            }
        }
    }

    log_bevel_stage("after_edge_strips", bp.output, diag_cap);

    // 11. Reconstruct original faces from BMesh n-gons (Blender bev_rebuild_polygon).
    // Winding follows original face normals via add_oriented_triangle — no mesh-center flip.
    if (bevel_cancel_requested(bp))
        return mesh;
    rebuild_faces_bmesh(bp, bmesh, welded);

    // Fix opposite-winding edges between strip and face polygon triangles.
    bp.output.fix_winding();

    log_bevel_stage("after_face_rebuild", bp.output, diag_cap);

    // 12. Skip degenerate triangles (two identical vertices) and geometric duplicates
    //     (different vertex indices but same 3 positions — happens when VMesh inner
    //     cap and face rebuild produce the same corner triangle).
    {
        std::vector<int> deduped;
        deduped.reserve(bp.output.triangles.size());

        // Build position keys for geometric dedup
        auto pos_key = [](const Vec3& v) -> std::string {
            auto q = [](double val) { return static_cast<int64_t>(std::llround(val / 1e-5)); };
            return std::to_string(q(v.x)) + ',' + std::to_string(q(v.y)) + ',' + std::to_string(q(v.z));
        };

        std::unordered_map<std::string, size_t> seen_geo;

        const size_t tris_before_dedup = bp.output.triangles.size() / 3;
        int skipped_index_degenerate = 0;
        int replaced_geo = 0;

        for (size_t i = 0; i + 2 < bp.output.triangles.size(); i += 3) {
            if (bevel_cancel_requested(bp))
                return mesh;
            int ia = bp.output.triangles[i];
            int ib = bp.output.triangles[i + 1];
            int ic = bp.output.triangles[i + 2];
            if (ia == ib || ib == ic || ia == ic) {
                ++skipped_index_degenerate;
                continue;
            }

            // Geometric dedup: same 3 positions from VMesh + face rebuild.
            // Prefer the later triangle (rebuild / edge strip) so shared boundary
            // winding matches F_RECON rather than an earlier flipped VMesh cap.
            const auto& va = bp.output.vertices[static_cast<size_t>(ia)];
            const auto& vb = bp.output.vertices[static_cast<size_t>(ib)];
            const auto& vc = bp.output.vertices[static_cast<size_t>(ic)];
            std::array<std::string, 3> pk = {
                pos_key(va), pos_key(vb), pos_key(vc)
            };
            std::sort(pk.begin(), pk.end());
            std::string geo_key = pk[0] + '|' + pk[1] + '|' + pk[2];
            auto it = seen_geo.find(geo_key);
            if (it != seen_geo.end()) {
                ++replaced_geo;
                const size_t old = it->second;
                deduped[old] = ia;
                deduped[old + 1] = ib;
                deduped[old + 2] = ic;
                continue;
            }
            seen_geo.emplace(geo_key, deduped.size());

            deduped.push_back(ia);
            deduped.push_back(ib);
            deduped.push_back(ic);
        }
        bp.output.triangles = std::move(deduped);

        if (bevel_diag_enabled()) {
            std::fprintf(stderr,
                         "[PCG_BEVEL_DIAG] dedup before_tris=%zu after_tris=%zu "
                         "skipped_index=%d replaced_geo=%d\n",
                         tris_before_dedup, bp.output.triangles.size() / 3,
                         skipped_index_degenerate, replaced_geo);
        }
    }

    log_bevel_stage("after_dedup", bp.output, diag_cap);

    // 13. Convert output to PcgMeshData
    data::PcgMeshData result;
    for (const auto& v : bp.output.vertices) {
        result.add_vertex({v.x, v.y, v.z});
    }
    for (size_t i = 0; i + 2 < bp.output.triangles.size(); i += 3) {
        result.add_triangle(
            bp.output.triangles[i],
            bp.output.triangles[i + 1],
            bp.output.triangles[i + 2]);
    }

    return result;
}

} // namespace pcg::internal::elements::bevel
