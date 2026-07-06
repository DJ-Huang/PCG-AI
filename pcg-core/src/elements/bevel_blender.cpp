#include "elements/bevel_blender.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

namespace pcg::internal::elements::bevel {

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
bool is_outside_edge(const Vec3& v_pos, const Vec3& other_pos, const Vec3& co,
                     int* r_closer = nullptr)
{
    Vec3 dir = sub(other_pos, v_pos);
    double len_sq = length_squared(dir);
    if (len_sq < 1e-12) return false;

    double t = dot(sub(co, v_pos), dir) / len_sq;
    if (t < -1e-6 || t > 1.0 + 1e-6) {
        if (r_closer) *r_closer = (t < 0) ? 0 : 1;
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
            norm_v = tri_normal({positions, triangles}, f_idx);
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
            norm_v1 = tri_normal({positions, triangles}, f_idx);
            norm_v2 = norm_v1;
        } else if (!edges_between) {
            norm_v1 = normalize(cross(dir2, dir1));
            if (f_idx >= 0) {
                Vec3 f_no = tri_normal({positions, triangles}, f_idx);
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

        // Check: if one offset is 0, don't go outside that edge
        if (e1->offset_r == 0.0f) {
            int closer = -1;
            if (is_outside_edge(v_co, positions[static_cast<size_t>(e1->edge_v1)], meetco, &closer)) {
                meetco = (closer == 0) ? v_co : positions[static_cast<size_t>(e1->edge_v1)];
            }
        }
        if (e2->offset_l == 0.0f) {
            int closer = -1;
            if (is_outside_edge(v_co, positions[static_cast<size_t>(e2->edge_v1)], meetco, &closer)) {
                meetco = (closer == 0) ? v_co : positions[static_cast<size_t>(e2->edge_v1)];
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

    Vec3 n1 = tri_normal({*bp.positions, *bp.triangles}, e->fprev);
    Vec3 n2 = tri_normal({*bp.positions, *bp.triangles}, e->fnext);
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
    } while (e != start && e != &bv->edges[0]);
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
        // Terminal edge case: only one beveled edge
        EdgeHalf* e = efirst;
        Vec3 co;

        if (bv->edgecount == 2) {
            // Only 2 edges: offset in plane on each side
            int fprev = e->fprev;
            if (fprev >= 0) {
                Vec3 f_no = tri_normal({*bp.positions, *bp.triangles}, fprev);
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
                Vec3 f_no = tri_normal({*bp.positions, *bp.triangles}, fnext);
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

            // Artificial extra point along unbeveled edge
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
        } else {
            // More than 2 edges: use offset_meet
            EdgeHalf* e_prev = e->prev;
            Vec3 co_meet;
            offset_meet(bp, e_prev, e, bv->v_idx, e->fprev, false, co_meet, nullptr);
            if (construct) {
                BoundVert* bndv = add_new_bound_vert(vm, co_meet);
                bndv->efirst = e_prev;
                bndv->elast = bndv->ebev = e;
                e->leftv = bndv;
                e_prev->leftv = e_prev->rightv = bndv;
            } else {
                adjust_bound_vert(e->leftv, co_meet);
            }

            EdgeHalf* e_next = e->next;
            offset_meet(bp, e, e_next, bv->v_idx, e->fprev, false, co_meet, nullptr);
            if (construct) {
                BoundVert* bndv = add_new_bound_vert(vm, co_meet);
                bndv->efirst = e;
                bndv->elast = e_next;
                e->rightv = e_next->leftv = bndv;
                e_next->rightv = bndv;
            } else {
                adjust_bound_vert(e->rightv, co_meet);
            }

            // Remaining edges: slide along
            float d = efirst->offset_l_spec;
            if (bp.profile < 0.25f)
                d *= std::sqrt(2.0f);

            EdgeHalf* e_iter = e_next->next;
            while (e_iter != efirst) {
                co = slide_dist((*bp.positions)[static_cast<size_t>(bv->v_idx)],
                               (*bp.positions)[static_cast<size_t>(e_iter->edge_v1)], d);
                if (construct) {
                    BoundVert* bndv = add_new_bound_vert(vm, co);
                    bndv->efirst = bndv->elast = e_iter;
                    e_iter->leftv = e_iter->rightv = bndv;
                } else {
                    adjust_bound_vert(e_iter->leftv, co);
                }
                e_iter = e_iter->next;
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
                do_linear_interp = true;
            } else {
                pro->plane_co = e_v0;
                pro->plane_no = plane_no;
                pro->proj_dir = plane_no;
            }
        }
        pro->plane_co = start;
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

/// Build a simple polygon (Blender bevel_build_poly).
void bevel_build_poly(BevelParams& bp, BevVert* bv) {
    VMesh* vm = bv->vmesh.get();
    int n = vm->count;

    // Collect boundary verts
    std::vector<Vec3> poly;
    BoundVert* bndv = vm->boundstart;
    do {
        poly.push_back(bndv->nv.co);
    } while ((bndv = bndv->next) != vm->boundstart);

    // Fan triangulate
    Vec3 normal = vertex_normal({*bp.positions, *bp.triangles}, bv->v_idx);
    for (int i = 1; i + 1 < static_cast<int>(poly.size()); i++) {
        bp.output.add_oriented_triangle(poly[0], poly[i], poly[i + 1], normal);
    }
}

/// Build a triangle fan (Blender bevel_build_trifan).
void bevel_build_trifan(BevelParams& bp, BevVert* bv) {
    VMesh* vm = bv->vmesh.get();
    int n = vm->count;

    // Center is average of all boundary verts
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
}

/// Build VMesh — the core function that creates the corner mesh.
void build_vmesh(BevelParams& bp, BevVert* bv) {
    VMesh* vm = bv->vmesh.get();
    int n = vm->count;
    int ns = vm->seg;
    int ns2 = ns / 2;

    // Allocate mesh array
    vm->mesh.assign(static_cast<size_t>(n) * (ns2 + 1) * (ns + 1), NewVert{});

    // (i, 0, 0) = boundary vert
    BoundVert* bndv = vm->boundstart;
    do {
        int i = bndv->index;
        vm->at(i, 0, 0).co = bndv->nv.co;
    } while ((bndv = bndv->next) != vm->boundstart);

    // Calculate all profiles
    calculate_vm_profiles(bp, bv, vm);

    // Create (i, 0, k) from profiles
    bndv = vm->boundstart;
    do {
        int i = bndv->index;
        vm->at(i, 0, ns).co = bndv->next->nv.co;  // End = next boundary vert

        if (vm->mesh_kind != MeshKind::Adj) {
            for (int k = 1; k < ns; k++) {
                if (bndv->ebev) {
                    vm->at(i, 0, k).co = get_profile_point(bndv->profile, k, ns, bp.seg);
                }
            }
        }
    } while ((bndv = bndv->next) != vm->boundstart);

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
                }
            }
        }

        // Create quad faces
        int odd = ns % 2;
        Vec3 vert_normal = vertex_normal_from_data(*bp.positions, *bp.triangles, bv->v_idx);
        bndv = vm_out->boundstart;
        do {
            int i = bndv->index;

            for (int j = 0; j < ns2; j++) {
                for (int k = 0; k < ns2 + odd; k++) {
                    Vec3 v1 = vm_out->at(i, j, k).co;
                    Vec3 v2 = vm_out->at(i, j, k + 1).co;
                    Vec3 v3 = vm_out->at(i, j + 1, k + 1).co;
                    Vec3 v4 = vm_out->at(i, j + 1, k).co;

                    if (length_squared(sub(v1, v2)) > 1e-16 ||
                        length_squared(sub(v3, v4)) > 1e-16) {
                        // Desired outward direction: from mesh center to quad center
                        Vec3 quad_center = scale(add(add(v1, v2), add(v3, v4)), 0.25);
                        Vec3 face_normal = normalize(quad_center);
                        if (length_squared(face_normal) < 1e-12)
                            face_normal = vert_normal;
                        bp.output.add_oriented_quad(v1, v2, v3, v4, face_normal);
                    }
                }
            }

            // Center ngon (odd segments)
            if (odd) {
                std::vector<Vec3> center_verts;
                for (int i2 = 0; i2 < n; i2++) {
                    center_verts.push_back(vm_out->at(i2, ns2, ns2).co);
                }
                // Normal: from mesh center to center of ngon
                Vec3 ngon_center{0, 0, 0};
                for (const auto& cv : center_verts)
                    ngon_center = add(ngon_center, cv);
                ngon_center = scale(ngon_center, 1.0 / center_verts.size());
                Vec3 normal = normalize(ngon_center);
                if (length_squared(normal) < 1e-12)
                    normal = vert_normal;
                for (size_t i2 = 1; i2 + 1 < center_verts.size(); i2++) {
                    bp.output.add_oriented_triangle(
                        center_verts[0], center_verts[i2], center_verts[i2 + 1], normal);
                }
            }
        } while ((bndv = bndv->next) != vm_out->boundstart);
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
    // At bv1: the BoundVert whose ebev == e (profile goes along this edge)
    BoundVert* bndv1 = e->leftv;
    if (!bndv1) return;

    // Find EdgeHalf at bv2 for the same edge (edge_v1 points back to bv1)
    EdgeHalf* e2 = nullptr;
    for (auto& eh : bv2->edges) {
        if (eh.edge_v1 == bv1->v_idx && eh.is_bev) {
            e2 = &eh;
            break;
        }
    }
    if (!e2) return;

    // At bv2: same — leftv has ebev == e2, profile goes along this edge
    BoundVert* bndv2 = e2->leftv;
    if (!bndv2) return;

    int ns = bp.seg;
    if (ns < 1) return;

    // Chamfer normal = bisector of adjacent face normals
    Vec3 n0, n1;
    if (e->fprev >= 0) n0 = tri_normal({*bp.positions, *bp.triangles}, e->fprev);
    if (e->fnext >= 0) n1 = tri_normal({*bp.positions, *bp.triangles}, e->fnext);
    Vec3 chamfer_dir = normalize(add(n0, n1));
    if (length_squared(chamfer_dir) < 1e-12) {
        Vec3 d = sub((*bp.positions)[static_cast<size_t>(e->edge_v1)],
                     (*bp.positions)[static_cast<size_t>(e->edge_v0)]);
        chamfer_dir = normalize(cross(d, cross(n0, n1)));
    }

    // Create quad strip: bv1's profile → bv2's profile (reversed)
    // Quad: (bv1[s], bv1[s+1], bv2[ns-s-1], bv2[ns-s])
    for (int s = 0; s < ns; s++) {
        Vec3 a = get_profile_point(bndv1->profile, s, ns, bp.seg);
        Vec3 b = get_profile_point(bndv1->profile, s + 1, ns, bp.seg);
        Vec3 c = get_profile_point(bndv2->profile, ns - (s + 1), ns, bp.seg);
        Vec3 d = get_profile_point(bndv2->profile, ns - s, ns, bp.seg);

        if (length_squared(sub(a, b)) > 1e-16 &&
            length_squared(sub(c, d)) > 1e-16) {
            bp.output.add_oriented_quad(a, b, c, d, chamfer_dir);
        }
    }
}

/// Reconstruct original faces with new boundary vertices.
void rebuild_faces(BevelParams& bp, const WeldedMesh& welded) {
    // Group triangles by face normal
    std::unordered_map<std::string, std::vector<int>> face_groups;
    for (size_t ti = 0; ti < welded.triangles.size(); ++ti) {
        Vec3 n = tri_normal(welded, static_cast<int>(ti));
        auto quantize = [](double v) -> int64_t {
            return static_cast<int64_t>(std::llround(v / 1e-4));
        };
        std::string key = std::to_string(quantize(n.x)) + ',' +
                          std::to_string(quantize(n.y)) + ',' +
                          std::to_string(quantize(n.z));
        face_groups[key].push_back(static_cast<int>(ti));
    }

    for (const auto& [group_key, tri_indices] : face_groups) {
        if (tri_indices.empty()) continue;

        // Get boundary loop
        std::unordered_map<int64_t, int> edge_use;
        for (int ti : tri_indices) {
            const auto& tri = welded.triangles[static_cast<size_t>(ti)];
            for (int e = 0; e < 3; e++) {
                int a = tri[e];
                int b = tri[(e + 1) % 3];
                edge_use[edge_key(a, b)]++;
            }
        }

        // Find boundary edges (used by only 1 triangle)
        std::unordered_map<int, std::vector<int>> adjacency;
        for (int ti : tri_indices) {
            const auto& tri = welded.triangles[static_cast<size_t>(ti)];
            for (int e = 0; e < 3; e++) {
                int a = tri[e];
                int b = tri[(e + 1) % 3];
                if (edge_use[edge_key(a, b)] == 1) {
                    adjacency[a].push_back(b);
                    adjacency[b].push_back(a);
                }
            }
        }

        if (adjacency.empty()) continue;

        // Trace boundary loop
        int start = adjacency.begin()->first;
        std::vector<int> loop = {start};
        int prev = -1, current = start;
        while (true) {
            const auto& neighbors = adjacency[current];
            int next = -1;
            for (int cand : neighbors) {
                if (cand != prev) { next = cand; break; }
            }
            if (next < 0 || next == start) break;
            loop.push_back(next);
            prev = current;
            current = next;
            if (loop.size() > welded.positions.size() + 1) break;
        }

        if (loop.size() < 3) continue;

        // Build offset polygon: replace vertices that are on beveled edges
        Vec3 face_normal = tri_normal(welded, tri_indices[0]);
        Vec3 centroid{0, 0, 0};
        int count = 0;
        for (int ti : tri_indices) {
            const auto& tri = welded.triangles[static_cast<size_t>(ti)];
            for (int idx : tri) {
                centroid = add(centroid, welded.positions[static_cast<size_t>(idx)]);
                count++;
            }
        }
        centroid = scale(centroid, 1.0 / count);

        // Build a set of triangle indices in this face group for fast lookup
        std::set<int> face_tri_set(tri_indices.begin(), tri_indices.end());

        // For each vertex in loop, check if it's a beveled edge endpoint.
        // Match BoundVerts by FACE INDEX (not edge keys) — this is robust against
        // face boundary loop direction ≠ CCW edge ordering.
        //
        // KEY: For each hard edge in the face boundary, insert ALL profile points
        // (profile[0]..profile[ns]) so the face polygon edges match the edge strip
        // edges. Without this, the face polygon has a single edge (meet_A→meet_B)
        // while the edge strip has ns edges, creating boundary edges.
        std::vector<Vec3> polygon;
        auto push_distinct = [&](const Vec3& p) {
            if (polygon.empty() || length_squared(sub(p, polygon.back())) > 1e-18)
                polygon.push_back(p);
        };
        for (size_t i = 0; i < loop.size(); i++) {
            int prev_v = loop[(i + loop.size() - 1) % loop.size()];
            int curr_v = loop[i];
            int next_v = loop[(i + 1) % loop.size()];

            int64_t edge_in = edge_key(prev_v, curr_v);
            int64_t edge_out = edge_key(curr_v, next_v);

            bool hard_in = bp.hard_edges.count(edge_in) > 0;
            bool hard_out = bp.hard_edges.count(edge_out) > 0;

            if (hard_in && hard_out) {
                // Corner: find BoundVert whose adjacent face (efirst->fnext) is in
                // this face group. Push meet point only — the edge strip and VMesh
                // handle the profile curve between meet points.
                bool found = false;
                for (auto& bv : bp.bevverts) {
                    if (bv.v_idx != curr_v) continue;
                    if (!bv.vmesh) continue;
                    BoundVert* bndv = bv.vmesh->boundstart;
                    do {
                        if (!bndv->efirst || !bndv->elast) {
                            bndv = bndv->next;
                            continue;
                        }
                        int face = bndv->efirst->fnext;
                        if (face >= 0 && face_tri_set.count(face)) {
                            push_distinct(bndv->nv.co);
                            found = true;
                            break;
                        }
                    } while ((bndv = bndv->next) != bv.vmesh->boundstart);
                }
                if (!found)
                    push_distinct(welded.positions[static_cast<size_t>(curr_v)]);
            } else if (hard_out) {
                // Start of beveled edge: insert ALL profile points so face polygon
                // edges match edge strip edges.
                bool found = false;
                for (auto& bv : bp.bevverts) {
                    if (bv.v_idx != curr_v) continue;
                    if (!bv.vmesh) continue;
                    BoundVert* bndv = bv.vmesh->boundstart;
                    do {
                        if (!bndv->ebev || bndv->ebev->edge_v1 != next_v) {
                            bndv = bndv->next;
                            continue;
                        }
                        // Determine direction: fprev → forward (0..ns), fnext → reverse (ns..0)
                        bool forward = (bndv->ebev->fprev >= 0 && face_tri_set.count(bndv->ebev->fprev));
                        bool reverse = (bndv->ebev->fnext >= 0 && face_tri_set.count(bndv->ebev->fnext));
                        if (forward || reverse) {
                            for (int k = 0; k <= bp.seg; k++) {
                                int idx = forward ? k : (bp.seg - k);
                                Vec3 pt = get_profile_point(bndv->profile, idx, bp.seg, bp.seg);
                                // Don't push the last point — it belongs to the next vertex
                                if (k < bp.seg)
                                    push_distinct(pt);
                            }
                            found = true;
                            break;
                        }
                    } while ((bndv = bndv->next) != bv.vmesh->boundstart);
                }
                if (!found)
                    push_distinct(welded.positions[static_cast<size_t>(curr_v)]);
            } else if (hard_in) {
                // End of beveled edge: profile points were already inserted by the
                // start vertex (hard_out case). Skip this vertex entirely.
                // But we need the profile endpoint — it was the last point pushed.
                // If nothing was pushed yet (start vertex failed), push original.
                if (polygon.empty())
                    push_distinct(welded.positions[static_cast<size_t>(curr_v)]);
            } else {
                push_distinct(welded.positions[static_cast<size_t>(curr_v)]);
            }
        }
        // Close the loop: ensure last point connects to first
        if (polygon.size() >= 3 && length_squared(sub(polygon.front(), polygon.back())) < 1e-18)
            polygon.pop_back();

        // Triangulate polygon
        if (polygon.size() >= 3) {
            for (size_t i = 1; i + 1 < polygon.size(); i++) {
                bp.output.add_oriented_triangle(polygon[0], polygon[i], polygon[i + 1], face_normal);
            }
        }
    }
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════════
// Section 11: Main Entry Point (Blender BM_mesh_bevel)
// ═══════════════════════════════════════════════════════════════════════════════

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
    BevelVMeshMethod vmesh_method)
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

    // 1. Weld mesh
    WeldedMesh welded = weld_mesh(mesh);
    if (welded.triangles.empty())
        return mesh;

    // 2. Build edge→face adjacency
    auto edge_faces = build_edge_faces(welded);

    // 3. Find hard edges
    double cos_limit = std::cos(angle_limit_deg * M_PI / 180.0);
    std::unordered_set<int64_t> hard_edges;
    for (const auto& [key, faces] : edge_faces) {
        if (faces[0] < 0 || faces[1] < 0) {
            hard_edges.insert(key);
            continue;
        }
        Vec3 n0 = tri_normal(welded, faces[0]);
        Vec3 n1 = tri_normal(welded, faces[1]);
        if (dot(n0, n1) < cos_limit)
            hard_edges.insert(key);
    }

    if (hard_edges.empty())
        return mesh;

    // 4. Setup BevelParams
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
        BevVert bv;
        bv.v_idx = v_idx;

        // Get all edges at this vertex
        auto vert_edges = get_vert_edges(v_idx, welded.triangles, edge_faces);
        vert_edges = sort_ccw(vert_edges, welded.positions, welded.triangles, v_idx);

        bv.edgecount = static_cast<int>(vert_edges.size());
        bv.edges.resize(vert_edges.size());

        for (size_t i = 0; i < vert_edges.size(); i++) {
            EdgeHalf& eh = bv.edges[i];
            eh.edge_v0 = v_idx;
            eh.edge_v1 = vert_edges[i].other_v;

            // Check if this edge is beveled (hard)
            int64_t key = edge_key(v_idx, vert_edges[i].other_v);
            eh.is_bev = hard_edges.count(key) > 0;

            // Set face adjacency
            auto face_it = edge_faces.find(key);
            if (face_it != edge_faces.end()) {
                // Determine which face is fprev and which is fnext based on CCW order
                int prev_idx = (i == 0) ? vert_edges.size() - 1 : i - 1;
                int next_idx = (i + 1) % vert_edges.size();

                int prev_other = vert_edges[prev_idx].other_v;
                int next_other = vert_edges[next_idx].other_v;

                // Find face shared with previous edge
                int64_t prev_key = edge_key(v_idx, prev_other);
                auto prev_face_it = edge_faces.find(prev_key);
                int fprev = -1, fnext = -1;
                if (prev_face_it != edge_faces.end()) {
                    const auto& pf = prev_face_it->second;
                    // Find face shared between this edge and previous edge
                    for (int f1 : face_it->second) {
                        if (f1 < 0) continue;
                        for (int f2 : pf) {
                            if (f2 < 0) continue;
                            if (f1 == f2) { fprev = f1; break; }
                        }
                        if (fprev >= 0) break;
                    }
                }

                // Find face shared with next edge
                int64_t next_key = edge_key(v_idx, next_other);
                auto next_face_it = edge_faces.find(next_key);
                if (next_face_it != edge_faces.end()) {
                    const auto& nf = next_face_it->second;
                    for (int f1 : face_it->second) {
                        if (f1 < 0) continue;
                        for (int f2 : nf) {
                            if (f2 < 0) continue;
                            if (f1 == f2) { fnext = f1; break; }
                        }
                        if (fnext >= 0) break;
                    }
                }

                eh.fprev = fprev;
                eh.fnext = fnext;
            }

            // Set initial offsets
            if (eh.is_bev) {
                float off = static_cast<float>(amount);
                if (offset_type == BevelOffsetType::Width) {
                    // Convert width to offset
                    double angle = M_PI / 2.0; // Default for 90-degree edges
                    if (eh.fprev >= 0 && eh.fnext >= 0) {
                        Vec3 n0 = tri_normal(welded, eh.fprev);
                        Vec3 n1 = tri_normal(welded, eh.fnext);
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
        bv.vmesh = std::make_unique<VMesh>();
        bv.vmesh->seg = segments;
        build_boundary(bp, &bv, true);
    }

    // 7. Limit offset (clamp_overlap)
    if (clamp_overlap) {
        bevel_limit_offset(bp);
        for (auto& bv : bp.bevverts) {
            build_boundary(bp, &bv, false);
        }
    }

    if (bp.offset_adjust) {
        adjust_offsets(bp);
    }

    // 8. Build VMesh (corner meshes)
    for (auto& bv : bp.bevverts) {
        if (bv.selcount == 0) continue;
        build_vmesh(bp, &bv);
    }

    // 9. Build edge polygons (bevel strips) — each edge only once
    std::set<std::pair<int, int>> processed_edges;
    for (auto& bv : bp.bevverts) {
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
                build_edge_polygons(bp, &bv, bv2, &e);
            }
        }
    }

    // 10. Reconstruct original faces
    rebuild_faces(bp, welded);

    // 11. Fix winding: ensure all triangle normals point outward.
    // For a closed mesh centered at origin, the centroid of each triangle
    // should be on the same side as the face normal.
    // Use the original input mesh centroid as the outward reference.
    Vec3 mesh_center{0, 0, 0};
    int mesh_center_count = 0;
    for (const auto& v : welded.positions) {
        mesh_center = add(mesh_center, v);
        mesh_center_count++;
    }
    if (mesh_center_count > 0)
        mesh_center = scale(mesh_center, 1.0 / mesh_center_count);

    for (size_t i = 0; i + 2 < bp.output.triangles.size(); i += 3) {
        const auto& a = bp.output.vertices[static_cast<size_t>(bp.output.triangles[i])];
        const auto& b = bp.output.vertices[static_cast<size_t>(bp.output.triangles[i + 1])];
        const auto& c = bp.output.vertices[static_cast<size_t>(bp.output.triangles[i + 2])];
        Vec3 n = cross(sub({b.x, b.y, b.z}, {a.x, a.y, a.z}),
                       sub({c.x, c.y, c.z}, {a.x, a.y, a.z}));
        if (length_squared(n) < 1e-20)
            continue;
        Vec3 centroid = scale(add(add({a.x, a.y, a.z}, {b.x, b.y, b.z}), {c.x, c.y, c.z}), 1.0 / 3.0);
        Vec3 outward = sub(centroid, mesh_center);
        if (dot(n, outward) < 0.0) {
            std::swap(bp.output.triangles[i + 1], bp.output.triangles[i + 2]);
        }
    }

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

        std::set<std::string> seen_geo;

        for (size_t i = 0; i + 2 < bp.output.triangles.size(); i += 3) {
            int ia = bp.output.triangles[i];
            int ib = bp.output.triangles[i + 1];
            int ic = bp.output.triangles[i + 2];
            if (ia == ib || ib == ic || ia == ic)
                continue;

            // Geometric dedup: sorted position keys
            const auto& va = bp.output.vertices[static_cast<size_t>(ia)];
            const auto& vb = bp.output.vertices[static_cast<size_t>(ib)];
            const auto& vc = bp.output.vertices[static_cast<size_t>(ic)];
            std::array<std::string, 3> pk = {
                pos_key(va), pos_key(vb), pos_key(vc)
            };
            std::sort(pk.begin(), pk.end());
            std::string geo_key = pk[0] + '|' + pk[1] + '|' + pk[2];
            if (seen_geo.count(geo_key))
                continue;
            seen_geo.insert(geo_key);

            deduped.push_back(ia);
            deduped.push_back(ib);
            deduped.push_back(ic);
        }
        bp.output.triangles = std::move(deduped);
    }

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
