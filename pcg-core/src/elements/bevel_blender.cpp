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
#include <limits>
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
    return 2;
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
    // Blender make_unit_square_map: builds a bilinear map where
    // (1,0)->end, (0,1)->start, (1,1)->middle, (0,0)->start+end-middle.
    Vec3 va_vmid = sub(middle, start);
    Vec3 vb_vmid = sub(middle, end);

    if (length_squared(va_vmid) < BEVEL_EPSILON_SQ ||
        length_squared(vb_vmid) < BEVEL_EPSILON_SQ)
        return false;

    if (std::fabs(angle_normalized_v3v3(normalize(va_vmid), normalize(vb_vmid)) - M_PI) <= BEVEL_EPSILON_ANG)
        return false;

    Vec3 vo = sub(start, vb_vmid);
    Vec3 vddir = normalize(cross(vb_vmid, va_vmid));
    Vec3 vd = add(vo, vddir);

    // Columns: vmid-va, vmid-vb, vmid+vd-va-vb, va+vb-vmid
    Vec3 col0 = sub(middle, start);
    Vec3 col1 = sub(middle, end);
    Vec3 col2 = sub(sub(add(middle, vd), start), end);
    Vec3 col3 = sub(add(start, end), middle);

    r_map.m[0][0] = col0.x; r_map.m[0][1] = col0.y; r_map.m[0][2] = col0.z; r_map.m[0][3] = 0;
    r_map.m[1][0] = col1.x; r_map.m[1][1] = col1.y; r_map.m[1][2] = col1.z; r_map.m[1][3] = 0;
    r_map.m[2][0] = col2.x; r_map.m[2][1] = col2.y; r_map.m[2][2] = col2.z; r_map.m[2][3] = 0;
    r_map.m[3][0] = col3.x; r_map.m[3][1] = col3.y; r_map.m[3][2] = col3.z; r_map.m[3][3] = 1;
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

/// Find xnew > x0 so that distance((x0,y0), (xnew, ynew)) = dtarget.
/// False position Illinois method (Blender find_superellipse_chord_endpoint).
static double find_superellipse_chord_endpoint(double x0, double dtarget, float r, bool rbig)
{
    double y0 = superellipse_co(x0, r, rbig);
    const double tol = 1e-13;
    const int maxiter = 10;

    double xmin = x0 + M_SQRT2 / 2.0 * dtarget;
    xmin = std::min(xmin, 1.0);
    double xmax = x0 + dtarget;
    xmax = std::min(xmax, 1.0);
    double ymin = superellipse_co(xmin, r, rbig);
    double ymax = superellipse_co(xmax, r, rbig);

    double dmaxerr = std::sqrt(std::pow((xmax - x0), 2) + std::pow((ymax - y0), 2)) - dtarget;
    double dminerr = std::sqrt(std::pow((xmin - x0), 2) + std::pow((ymin - y0), 2)) - dtarget;

    double xnew = xmax - dmaxerr * (xmax - xmin) / (dmaxerr - dminerr);
    bool lastupdated_upper = true;

    for (int iter = 0; iter < maxiter; iter++) {
        double ynew = superellipse_co(xnew, r, rbig);
        double dnewerr = std::sqrt(std::pow((xnew - x0), 2) + std::pow((ynew - y0), 2)) - dtarget;
        if (std::fabs(dnewerr) < tol) {
            break;
        }
        if (dnewerr < 0) {
            xmin = xnew;
            ymin = ynew;
            dminerr = dnewerr;
            if (!lastupdated_upper) {
                xnew = (dmaxerr / 2 * xmin - dminerr * xmax) / (dmaxerr / 2 - dminerr);
            }
            else {
                xnew = xmax - dmaxerr * (xmax - xmin) / (dmaxerr - dminerr);
            }
            lastupdated_upper = false;
        }
        else {
            xmax = xnew;
            ymax = ynew;
            dmaxerr = dnewerr;
            if (lastupdated_upper) {
                xnew = (dmaxerr * xmin - dminerr / 2 * xmax) / (dmaxerr - dminerr / 2);
            }
            else {
                xnew = xmax - dmaxerr * (xmax - xmin) / (dmaxerr - dminerr);
            }
            lastupdated_upper = true;
        }
    }
    return xnew;
}

/// General-case even chord spacing (Blender find_even_superellipse_chords_general).
/// Works on first half only, then mirrors along x=y diagonal.
static void find_even_superellipse_chords_general(int seg, float r,
                                                   std::vector<double>& xvals,
                                                   std::vector<double>& yvals)
{
    const int smoothitermax = 10;
    const double error_tol = 1e-7;
    int imax = (seg + 1) / 2 - 1;

    bool seg_odd = seg % 2;

    bool rbig;
    double mx;
    if (r > 1.0f) {
        rbig = true;
        mx = std::pow(0.5, 1.0 / r);
    }
    else {
        rbig = false;
        mx = 1 - std::pow(0.5, 1.0 / r);
    }

    for (int i = 0; i <= imax; i++) {
        xvals[i] = i * mx / seg * 2;
        yvals[i] = superellipse_co(xvals[i], r, rbig);
    }
    yvals[0] = 1;

    for (int iter = 0; iter < smoothitermax; iter++) {
        double sum = 0.0;
        double dmin = 2.0;
        double dmax = 0.0;
        for (int i = 0; i < imax; i++) {
            double d = std::sqrt(std::pow((xvals[i + 1] - xvals[i]), 2) +
                                  std::pow((yvals[i + 1] - yvals[i]), 2));
            sum += d;
            dmax = std::max(d, dmax);
            dmin = std::min(d, dmin);
        }
        double davg;
        if (seg_odd) {
            sum += M_SQRT2 / 2 * (yvals[imax] - xvals[imax]);
            davg = sum / (imax + 0.5);
        }
        else {
            sum += std::sqrt(std::pow((xvals[imax] - mx), 2) +
                              std::pow((yvals[imax] - mx), 2));
            davg = sum / (imax + 1.0);
        }

        bool precision_reached = true;
        if (dmax - davg > error_tol)
            precision_reached = false;
        if (dmin - davg < error_tol)
            precision_reached = false;
        if (precision_reached)
            break;

        for (int i = 1; i <= imax; i++) {
            xvals[i] = find_superellipse_chord_endpoint(xvals[i - 1], davg, r, rbig);
            yvals[i] = superellipse_co(xvals[i], r, rbig);
        }
    }

    if (!seg_odd) {
        xvals[imax + 1] = mx;
        yvals[imax + 1] = mx;
    }
    for (int i = imax + 1; i <= seg; i++) {
        yvals[i] = xvals[seg - i];
        xvals[i] = yvals[seg - i];
    }

    if (!rbig) {
        for (int i = 0; i <= seg; i++) {
            double temp = xvals[i];
            xvals[i] = 1.0 - yvals[i];
            yvals[i] = 1.0 - temp;
        }
    }
}

void find_even_superellipse_chords(int seg, float super_r,
                                   std::vector<double>& xvals,
                                   std::vector<double>& yvals)
{
    xvals.resize(seg + 1);
    yvals.resize(seg + 1);

    bool seg_odd = seg % 2;
    int n2 = seg / 2;

    if (super_r == PRO_LINE_R) {
        for (int i = 0; i <= seg; i++) {
            xvals[i] = static_cast<double>(i) / seg;
            yvals[i] = 1.0 - static_cast<double>(i) / seg;
        }
        return;
    }
    if (super_r == PRO_CIRCLE_R) {
        double temp = M_PI_2 / seg;
        for (int i = 0; i <= seg; i++) {
            xvals[i] = std::sin(i * temp);
            yvals[i] = std::cos(i * temp);
        }
        return;
    }
    if (super_r == PRO_SQUARE_IN_R) {
        if (!seg_odd) {
            for (int i = 0; i <= n2; i++) {
                xvals[i] = 0.0;
                yvals[i] = 1.0 - static_cast<double>(i) / n2;
                xvals[seg - i] = yvals[i];
                yvals[seg - i] = xvals[i];
            }
        }
        else {
            double temp = 1.0 / (n2 + M_SQRT2 / 2.0);
            for (int i = 0; i <= n2; i++) {
                xvals[i] = 0.0;
                yvals[i] = 1.0 - static_cast<double>(i) * temp;
                xvals[seg - i] = yvals[i];
                yvals[seg - i] = xvals[i];
            }
        }
        return;
    }
    if (super_r == PRO_SQUARE_R) {
        if (!seg_odd) {
            for (int i = 0; i <= n2; i++) {
                xvals[i] = static_cast<double>(i) / n2;
                yvals[i] = 1.0;
                xvals[seg - i] = yvals[i];
                yvals[seg - i] = xvals[i];
            }
        }
        else {
            double temp = 1.0 / (n2 + M_SQRT2 / 2);
            for (int i = 0; i <= n2; i++) {
                xvals[i] = static_cast<double>(i) * temp;
                yvals[i] = 1.0;
                xvals[seg - i] = yvals[i];
                yvals[seg - i] = xvals[i];
            }
        }
        return;
    }

    find_even_superellipse_chords_general(seg, super_r, xvals, yvals);
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

void set_profile_spacing(int seg, float super_r, float profile, ProfileSpacing& pro_spacing) {
    if (seg <= 1) {
        pro_spacing.xvals.clear();
        pro_spacing.yvals.clear();
        pro_spacing.xvals_2.clear();
        pro_spacing.yvals_2.clear();
        pro_spacing.seg_2 = 0;
        pro_spacing.fullness = 0.0f;
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

    // Blender: fullness = f(bp->profile, nseg) — NOT a hardcoded 0.5.
    // Wrong fullness places Adj VMesh center off the profile curve → crossed corners.
    pro_spacing.fullness = find_profile_fullness(seg, super_r, profile);
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
    return geometry::edge_group_id(a, b);
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

    // Fan-triangulate from faces (derived cache may be stale).
    for (const auto& face : output.faces) {
        if (face.verts.size() < 3) continue;
        const int i0 = face.verts[0];
        for (size_t i = 1; i + 1 < face.verts.size(); ++i) {
            const int ia = i0;
            const int ib = face.verts[i];
            const int ic = face.verts[i + 1];
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
}

void split_inward_face_fans(BevelParams::OutputMesh& output, const Vec3& center)
{
    auto orientation = [&](int ia, int ib, int ic) {
        const Vec3& a = output.vertices[static_cast<size_t>(ia)];
        const Vec3& b = output.vertices[static_cast<size_t>(ib)];
        const Vec3& c = output.vertices[static_cast<size_t>(ic)];
        const Vec3 normal = cross(sub(b, a), sub(c, a));
        const Vec3 triangle_center = scale(add(add(a, b), c), 1.0 / 3.0);
        return dot(normal, sub(triangle_center, center));
    };

    std::vector<BevelParams::OutputFace> repaired;
    repaired.reserve(output.faces.size());
    for (const auto& face : output.faces) {
        if (face.verts.size() < 4) {
            repaired.push_back(face);
            continue;
        }

        const int a = face.verts[0];
        bool has_inward_triangle = false;
        for (size_t i = 1; i + 1 < face.verts.size(); ++i) {
            if (orientation(a, face.verts[i], face.verts[i + 1]) < -1e-12) {
                has_inward_triangle = true;
                break;
            }
        }
        if (!has_inward_triangle) {
            repaired.push_back(face);
            continue;
        }

        for (size_t i = 1; i + 1 < face.verts.size(); ++i) {
            const int b = face.verts[i];
            const int c = face.verts[i + 1];
            repaired.push_back({orientation(a, b, c) < 0.0
                                    ? std::vector<int>{a, c, b}
                                    : std::vector<int>{a, b, c},
                                face.origin});
        }
    }
    output.faces = std::move(repaired);
}

void orient_faces_outward(BevelParams::OutputMesh& output, const Vec3& center)
{
    for (auto& face : output.faces) {
        if (face.verts.size() < 3)
            continue;
        Vec3 normal{};
        Vec3 face_center{};
        for (size_t i = 0; i < face.verts.size(); ++i) {
            const Vec3& current = output.vertices[static_cast<size_t>(face.verts[i])];
            const Vec3& next =
                output.vertices[static_cast<size_t>(face.verts[(i + 1) % face.verts.size()])];
            normal = add(normal, cross(current, next));
            face_center = add(face_center, current);
        }
        face_center = scale(face_center, 1.0 / static_cast<double>(face.verts.size()));
        if (dot(normal, sub(face_center, center)) < 0.0)
            std::reverse(face.verts.begin(), face.verts.end());
    }
}

std::vector<Vec3> clip_polygon_axis(const std::vector<Vec3>& polygon, int axis,
                                    double coordinate, bool keep_lower)
{
    std::vector<Vec3> clipped;
    if (polygon.empty())
        return clipped;

    constexpr double kPlaneEps = 1e-8;
    auto inside = [&](const Vec3& point) {
        return keep_lower ? point[axis] <= coordinate + kPlaneEps
                          : point[axis] >= coordinate - kPlaneEps;
    };
    auto intersection = [&](const Vec3& a, const Vec3& b) {
        const double denominator = b[axis] - a[axis];
        if (std::abs(denominator) <= 1e-12)
            return a;
        const double t = (coordinate - a[axis]) / denominator;
        return add(a, scale(sub(b, a), t));
    };

    Vec3 previous = polygon.back();
    bool previous_inside = inside(previous);
    for (const Vec3& current : polygon) {
        const bool current_inside = inside(current);
        if (current_inside != previous_inside)
            clipped.push_back(intersection(previous, current));
        if (current_inside)
            clipped.push_back(current);
        previous = current;
        previous_inside = current_inside;
    }
    return clipped;
}

void split_output_at_source_planes(BevelParams::OutputMesh& output,
                                   const data::PcgGeometry& source,
                                   const Vec3& min_v,
                                   const Vec3& max_v)
{
    std::array<std::vector<double>, 3> planes;
    constexpr double kBoundsEps = 1e-6;
    auto source_coordinate = [](const data::PcgVec3& point, int axis) {
        return axis == 0 ? point.x : (axis == 1 ? point.y : point.z);
    };
    for (const auto& point : source.points()) {
        for (int axis = 0; axis < 3; ++axis) {
            const double coordinate = source_coordinate(point, axis);
            if (coordinate <= min_v[axis] + kBoundsEps ||
                coordinate >= max_v[axis] - kBoundsEps) {
                continue;
            }
            auto& axis_planes = planes[static_cast<size_t>(axis)];
            const bool exists = std::any_of(axis_planes.begin(), axis_planes.end(),
                [&](double value) { return std::abs(value - coordinate) <= kBoundsEps; });
            if (!exists)
                axis_planes.push_back(coordinate);
        }
    }
    for (auto& axis_planes : planes)
        std::sort(axis_planes.begin(), axis_planes.end());

    struct FacePiece {
        std::vector<Vec3> points;
        int origin = -1;
    };
    std::vector<FacePiece> pieces;
    pieces.reserve(output.faces.size());
    for (const auto& face : output.faces) {
        FacePiece piece;
        piece.origin = face.origin;
        piece.points.reserve(face.verts.size());
        for (int index : face.verts)
            piece.points.push_back(output.vertices[static_cast<size_t>(index)]);
        pieces.push_back(std::move(piece));
    }

    constexpr double kSplitEps = 1e-8;
    for (int axis = 0; axis < 3; ++axis) {
        for (double coordinate : planes[static_cast<size_t>(axis)]) {
            std::vector<FacePiece> next;
            next.reserve(pieces.size() * 2);
            for (auto& piece : pieces) {
                bool has_lower = false;
                bool has_upper = false;
                for (const Vec3& point : piece.points) {
                    has_lower = has_lower || point[axis] < coordinate - kSplitEps;
                    has_upper = has_upper || point[axis] > coordinate + kSplitEps;
                }
                if (!has_lower || !has_upper) {
                    next.push_back(std::move(piece));
                    continue;
                }

                auto lower = clip_polygon_axis(piece.points, axis, coordinate, true);
                auto upper = clip_polygon_axis(piece.points, axis, coordinate, false);
                if (lower.size() >= 3)
                    next.push_back({std::move(lower), piece.origin});
                if (upper.size() >= 3)
                    next.push_back({std::move(upper), piece.origin});
            }
            pieces = std::move(next);
        }
    }

    auto vertex_key = [](const Vec3& point) {
        auto quantize = [](double value) {
            return static_cast<int64_t>(std::llround(value / 1e-6));
        };
        return std::to_string(quantize(point.x)) + ',' +
               std::to_string(quantize(point.y)) + ',' +
               std::to_string(quantize(point.z));
    };
    output.vertex_cache.clear();
    for (size_t i = 0; i < output.vertices.size(); ++i)
        output.vertex_cache.emplace(vertex_key(output.vertices[i]), static_cast<int>(i));

    output.faces.clear();
    for (const auto& piece : pieces) {
        output.current_face_origin = piece.origin;
        std::vector<int> indices;
        indices.reserve(piece.points.size());
        for (const Vec3& point : piece.points)
            indices.push_back(output.get_vertex(point));
        output.emit_face(indices);
    }
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

        // Perpendicular vectors pointing into the face (Blender disk handedness).
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

        if (bevel_diag_enabled() && bp.positions->size() == 26) {
            std::fprintf(stderr,
                         "[PCG_BEVEL_DIAG] offset_meet v=%d between=%d kind=%d e1=%d e2=%d\n",
                         v_idx, edges_between ? 1 : 0, isect_kind,
                         e1->edge_v1, e2->edge_v1);
        }

        if (isect_kind == 0)
            meetco = off1a;
        else if (isect_kind == 2 && edges_between)
            meetco = mid(meetco, isect2);

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

Vec3 bmesh_vertex_normal(const BevelParams& bp, int vertex_index) {
    if (!bp.bmesh)
        return {0.0, 0.0, 0.0};

    Vec3 normal{};
    for (int face_index = 0;
         face_index < static_cast<int>(bp.bmesh->faces.size());
         ++face_index) {
        const auto& loop = bp.bmesh->faces[static_cast<size_t>(face_index)].verts;
        const int count = static_cast<int>(loop.size());
        for (int i = 0; i < count; ++i) {
            if (loop[static_cast<size_t>(i)] != vertex_index)
                continue;
            const Vec3& previous =
                (*bp.positions)[static_cast<size_t>(loop[static_cast<size_t>((i + count - 1) % count)])];
            const Vec3& current = (*bp.positions)[static_cast<size_t>(vertex_index)];
            const Vec3& next =
                (*bp.positions)[static_cast<size_t>(loop[static_cast<size_t>((i + 1) % count)])];
            const Vec3 incoming = normalize(sub(current, previous));
            const Vec3 outgoing = normalize(sub(next, current));
            const double corner_angle =
                std::acos(std::clamp(-dot(incoming, outgoing), -1.0, 1.0));
            normal = madd(normal, bmesh_face_normal(bp, face_index), corner_angle);
            break;
        }
    }
    return normalize(normal);
}

/// Blender offset_meet_edge. One of the two offsets is expected to be zero.
static bool offset_meet_edge(EdgeHalf* first,
                             EdgeHalf* second,
                             int vertex_index,
                             const BevelParams& bp,
                             Vec3& meeting_point,
                             double* result_angle) {
    const Vec3& vertex = (*bp.positions)[static_cast<size_t>(vertex_index)];
    const Vec3 first_direction = normalize(sub(
        (*bp.positions)[static_cast<size_t>(first->edge_v1)], vertex));
    const Vec3 second_direction = normalize(sub(
        (*bp.positions)[static_cast<size_t>(second->edge_v1)], vertex));

    double angle = angle_normalized_v3v3(first_direction, second_direction);
    constexpr double kGoodAngle = 0.1;
    if (std::abs(angle) < kGoodAngle) {
        if (result_angle)
            *result_angle = 0.0;
        return false;
    }

    if (dot(cross(first_direction, second_direction),
            bmesh_vertex_normal(bp, vertex_index)) < 0.0) {
        angle = 2.0 * M_PI - angle;
        if (result_angle)
            *result_angle = angle;
        return false;
    }
    if (result_angle)
        *result_angle = angle;
    if (std::abs(angle - M_PI) < kGoodAngle)
        return false;

    const double sine = std::sin(angle);
    meeting_point = vertex;
    if (first->offset_r == 0.0f)
        meeting_point = madd(meeting_point, first_direction, second->offset_l / sine);
    else
        meeting_point = madd(meeting_point, second_direction, first->offset_r / sine);
    return true;
}

/// Check if good to use offset_on_edge_between (Blender good_offset_on_edge_between).
bool good_offset_on_edge_between(EdgeHalf* e1, EdgeHalf* e2, EdgeHalf* emid,
                                  int v_idx, const BevelParams& bp)
{
    Vec3 meeting_point;
    double angle = 0.0;
    return offset_meet_edge(e1, emid, v_idx, bp, meeting_point, &angle) &&
           offset_meet_edge(emid, e2, v_idx, bp, meeting_point, &angle);
}

/// Calculate offset on an edge between two beveled edges (Blender offset_on_edge_between).
/// Returns true if successful, sets r_co and r_sinratio.
bool offset_on_edge_between(EdgeHalf* e1, EdgeHalf* e2, EdgeHalf* emid,
                             int v_idx, const BevelParams& bp,
                             Vec3& r_co, float& r_sinratio)
{
    Vec3 first_meeting;
    Vec3 second_meeting;
    double first_angle = 0.0;
    double second_angle = 0.0;
    const bool first_ok =
        offset_meet_edge(e1, emid, v_idx, bp, first_meeting, &first_angle);
    const bool second_ok =
        offset_meet_edge(emid, e2, v_idx, bp, second_meeting, &second_angle);

    if (first_ok && second_ok) {
        r_co = mid(first_meeting, second_meeting);
        r_sinratio = first_angle == 0.0
                         ? 1.0f
                         : static_cast<float>(std::sin(second_angle) /
                                              std::sin(first_angle));
        return true;
    }
    if (first_ok)
        r_co = first_meeting;
    else if (second_ok)
        r_co = second_meeting;
    else
        r_co = slide_dist((*bp.positions)[static_cast<size_t>(v_idx)],
                          (*bp.positions)[static_cast<size_t>(emid->edge_v1)],
                          e1->offset_r);
    return false;
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

bool solve_least_squares(const std::vector<std::vector<double>>& matrix,
                         const std::vector<double>& rhs,
                         std::vector<double>& solution) {
    if (matrix.empty() || matrix[0].empty() || matrix.size() != rhs.size())
        return false;

    const int rows = static_cast<int>(matrix.size());
    const int columns = static_cast<int>(matrix[0].size());
    std::vector<std::vector<double>> normal(
        static_cast<size_t>(columns), std::vector<double>(static_cast<size_t>(columns), 0.0));
    std::vector<double> projected(static_cast<size_t>(columns), 0.0);
    for (int row = 0; row < rows; ++row) {
        for (int i = 0; i < columns; ++i) {
            projected[static_cast<size_t>(i)] +=
                matrix[static_cast<size_t>(row)][static_cast<size_t>(i)] *
                rhs[static_cast<size_t>(row)];
            for (int j = 0; j < columns; ++j) {
                normal[static_cast<size_t>(i)][static_cast<size_t>(j)] +=
                    matrix[static_cast<size_t>(row)][static_cast<size_t>(i)] *
                    matrix[static_cast<size_t>(row)][static_cast<size_t>(j)];
            }
        }
    }

    for (int column = 0; column < columns; ++column) {
        int pivot = column;
        for (int row = column + 1; row < columns; ++row) {
            if (std::abs(normal[static_cast<size_t>(row)][static_cast<size_t>(column)]) >
                std::abs(normal[static_cast<size_t>(pivot)][static_cast<size_t>(column)])) {
                pivot = row;
            }
        }
        if (std::abs(normal[static_cast<size_t>(pivot)][static_cast<size_t>(column)]) < 1e-12)
            return false;
        if (pivot != column) {
            std::swap(normal[static_cast<size_t>(pivot)], normal[static_cast<size_t>(column)]);
            std::swap(projected[static_cast<size_t>(pivot)], projected[static_cast<size_t>(column)]);
        }

        const double diagonal = normal[static_cast<size_t>(column)][static_cast<size_t>(column)];
        for (int j = column; j < columns; ++j)
            normal[static_cast<size_t>(column)][static_cast<size_t>(j)] /= diagonal;
        projected[static_cast<size_t>(column)] /= diagonal;

        for (int row = 0; row < columns; ++row) {
            if (row == column)
                continue;
            const double factor = normal[static_cast<size_t>(row)][static_cast<size_t>(column)];
            for (int j = column; j < columns; ++j) {
                normal[static_cast<size_t>(row)][static_cast<size_t>(j)] -=
                    factor * normal[static_cast<size_t>(column)][static_cast<size_t>(j)];
            }
            projected[static_cast<size_t>(row)] -=
                factor * projected[static_cast<size_t>(column)];
        }
    }

    solution = std::move(projected);
    return true;
}

void adjust_the_cycle_or_chain(BoundVert* vstart, bool iscycle) {
    std::vector<BoundVert*> chain;
    BoundVert* v = vstart;
    do {
        chain.push_back(v);
        v = v->adjchain;
    } while (v && v != vstart);

    const int count = static_cast<int>(chain.size());
    if (count < 2)
        return;

    const int row_count = iscycle ? 3 * count : 3 * count - 3;
    std::vector<std::vector<double>> matrix(
        static_cast<size_t>(row_count), std::vector<double>(static_cast<size_t>(count), 0.0));
    std::vector<double> rhs(static_cast<size_t>(row_count), 0.0);
    constexpr double kMatchSpecWeight = 0.2;

    for (int i = 0; i < count; ++i) {
        v = chain[static_cast<size_t>(i)];
        if (iscycle || i < count - 1) {
            BoundVert* next = v->adjchain;
            if (!v->efirst || !v->elast || !next || !next->elast)
                return;

            matrix[static_cast<size_t>(i)][static_cast<size_t>(i)] += 1.0;
            if (iscycle) {
                const int previous = i > 0 ? i - 1 : count - 1;
                matrix[static_cast<size_t>(previous)][static_cast<size_t>(i)] -= v->sinratio;
            } else if (i > 0) {
                matrix[static_cast<size_t>(i - 1)][static_cast<size_t>(i)] -= v->sinratio;
            }

            int row = iscycle ? count + 2 * i : count - 1 + 2 * i;
            matrix[static_cast<size_t>(row)][static_cast<size_t>(i)] += kMatchSpecWeight;
            rhs[static_cast<size_t>(row)] +=
                kMatchSpecWeight * static_cast<double>(v->efirst->offset_r);

            ++row;
            const int next_parameter = i == count - 1 ? 0 : i + 1;
            matrix[static_cast<size_t>(row)][static_cast<size_t>(next_parameter)] +=
                kMatchSpecWeight * static_cast<double>(next->sinratio);
            rhs[static_cast<size_t>(row)] +=
                kMatchSpecWeight * static_cast<double>(next->elast->offset_l);
        } else {
            if (!v->elast)
                return;
            matrix[static_cast<size_t>(i - 1)][static_cast<size_t>(i)] -= 1.0;
        }
    }

    std::vector<double> solution;
    if (!solve_least_squares(matrix, rhs, solution))
        return;

    for (int i = 0; i < count; ++i) {
        v = chain[static_cast<size_t>(i)];
        const float value = static_cast<float>(solution[static_cast<size_t>(i)]);
        if (iscycle || i < count - 1) {
            v->efirst->offset_r = value;
            if (iscycle || i > 0)
                v->elast->offset_l = v->sinratio * value;
        } else {
            v->elast->offset_l = value;
        }
    }
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

        for (EdgeHalf* e_iter = e->next; e_iter->next != efirst; e_iter = e_iter->next) {
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

    // Find the EdgeHalf at vc that points back to vb (the other end of eb).
        // The reverse half-edge flips disk-cycle orientation at the opposite endpoint.
    for (auto& bv_other : bp.bevverts) {
        if (bv_other.v_idx != vc_idx) continue;
        for (auto& eh : bv_other.edges) {
            if (!eh.is_bev) continue;
            if (eh.edge_v1 == vb_idx) {
                // eh is the reverse EdgeHalf at vc pointing back to vb.
                // Blender uses the previous disk-cycle edge at the opposite
                // endpoint because the half-edge direction is reversed there.
                ec = eh.prev;
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
    const float requested_offset = static_cast<float>(bp.offset);
    float limited_offset = static_cast<float>(bp.offset);
    for (auto& bv : bp.bevverts) {
        for (auto& e : bv.edges) {
            limited_offset = std::min(
                limited_offset, geometry_collide_offset(bp, &bv, &e));
        }
    }

    if (limited_offset >= bp.offset)
        return;

    if (bevel_diag_enabled()) {
        std::fprintf(stderr,
                     "[PCG_BEVEL_DIAG] clamp requested=%.9f limited=%.9f\n",
                     requested_offset, limited_offset);
    }

    const float offset_factor = limited_offset / static_cast<float>(bp.offset);
    for (auto& bv : bp.bevverts) {
        for (auto& e : bv.edges) {
            e.offset_l_spec *= offset_factor;
            e.offset_r_spec *= offset_factor;
            e.offset_l *= offset_factor;
            e.offset_r *= offset_factor;
        }
    }
    bp.offset = limited_offset;
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════════
// Section 8: Profile Params + Calculate Profile
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

/// Project a point onto an edge (Blender project_to_edge L2246).
Vec3 project_to_edge(const Vec3& e_v0, const Vec3& e_v1, const Vec3& start, const Vec3& end) {
    Vec3 projected;
    Vec3 profile_projected;
    if (isect_line_line_v3(e_v0, e_v1, start, end,
                           projected, profile_projected) == 0) {
        return e_v0;
    }
    return projected;
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

/// Move both profile planes to the shared weld plane (Blender
/// move_weld_profile_planes).  The boundary points and original vertex define
/// the plane on which the two projected profiles are most likely to meet.
void move_weld_profile_planes(BoundVert* bndv1,
                              BoundVert* bndv2,
                              const Vec3& vertex_co)
{
    Profile& profile1 = bndv1->profile;
    Profile& profile2 = bndv2->profile;
    if (length_squared(profile1.proj_dir) == 0.0 ||
        length_squared(profile2.proj_dir) == 0.0) {
        return;
    }

    const Vec3 d1 = sub(vertex_co, bndv1->nv.co);
    const Vec3 d2 = sub(vertex_co, bndv2->nv.co);
    const Vec3 no_raw = cross(d1, d2);
    const Vec3 no2_raw = cross(d1, profile1.proj_dir);
    const Vec3 no3_raw = cross(d2, profile2.proj_dir);
    const double l1 = length(no_raw);
    const double l2 = length(no2_raw);
    const double l3 = length(no3_raw);

    if (l1 != 0.0 && (l2 != 0.0 || l3 != 0.0)) {
        const Vec3 no = normalize(no_raw);
        const Vec3 no2 = normalize(no2_raw);
        const Vec3 no3 = normalize(no3_raw);
        const double dot1 = std::abs(dot(no, no2));
        const double dot2 = std::abs(dot(no, no3));
        if (std::abs(dot1 - 1.0) > BEVEL_EPSILON)
            profile1.plane_no = no;
        if (std::abs(dot2 - 1.0) > BEVEL_EPSILON)
            profile2.plane_no = no;
    }

    profile1.special_params = true;
    profile2.special_params = true;
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
                vm->at(i, j, k).valid = mesh_vert_canon(vm, i, j, k).valid;
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
    vm_out->mesh.assign(static_cast<size_t>(n_boundary) * (ns_out / 2 + 1) * (ns_out + 1), NewVert{});
    vm_out->boundstart = vm_in->boundstart;

    // 1. Adjust even boundary vertices (smooth rule)
    for (int i = 0; i < n_boundary; i++) {
        vm_out->at(i, 0, 0).co = vm_in->at(i, 0, 0).co;
        for (int k = 1; k < ns_in; k++) {
            Vec3 co = vm_in->at(i, 0, k).co;
            Vec3 co1 = mesh_vert_canon(vm_in, i, 0, k - 1).co;
            Vec3 co2 = mesh_vert_canon(vm_in, i, 0, k + 1).co;
            Vec3 acc = add(co1, co2);
            acc = madd(acc, co, -2.0);
            co = madd(co, acc, -1.0 / 6.0);
            mesh_vert_canon(vm_out.get(), i, 0, 2 * k).co = co;
        }
    }

    // 2. Adjust odd boundary vertices using profile
    BoundVert* bndv = vm_out->boundstart;
    for (int i = 0; i < n_boundary; i++) {
        for (int k = 1; k < ns_out; k += 2) {
            Vec3 co = get_profile_point(bndv->profile, k, ns_out, bp.seg);
            // Smooth
            Vec3 co1 = mesh_vert_canon(vm_out.get(), i, 0, k - 1).co;
            Vec3 co2 = mesh_vert_canon(vm_out.get(), i, 0, k + 1).co;
            Vec3 acc = add(co1, co2);
            acc = madd(acc, co, -2.0);
            co = madd(co, acc, -1.0 / 6.0);
            mesh_vert_canon(vm_out.get(), i, 0, k).co = co;
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
                    mesh_vert_canon(vm_out.get(), i, 2*j + 1, 2*k - 1).co,
                    mesh_vert_canon(vm_out.get(), i, 2*j + 1, 2*k + 1).co);
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
                    mesh_vert_canon(vm_out.get(), i, 2*j - 1, 2*k + 1).co,
                    mesh_vert_canon(vm_out.get(), i, 2*j + 1, 2*k + 1).co);
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
                    mesh_vert_canon(vm_out.get(), i, 2*j, 2*k - 1).co,
                    mesh_vert_canon(vm_out.get(), i, 2*j, 2*k + 1).co,
                    mesh_vert_canon(vm_out.get(), i, 2*j - 1, 2*k).co,
                    mesh_vert_canon(vm_out.get(), i, 2*j + 1, 2*k).co);
                Vec3 co2 = avg4(
                    mesh_vert_canon(vm_out.get(), i, 2*j - 1, 2*k - 1).co,
                    mesh_vert_canon(vm_out.get(), i, 2*j + 1, 2*k - 1).co,
                    mesh_vert_canon(vm_out.get(), i, 2*j - 1, 2*k + 1).co,
                    mesh_vert_canon(vm_out.get(), i, 2*j + 1, 2*k + 1).co);
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

/// Snap a vertex to the superellipsoid surface (Blender snap_to_superellipsoid).
/// PRO_CIRCLE_R → normalize; PRO_SQUARE_R/PRO_SQUARE_IN_R → snap to axis planes;
/// general r → superellipse formula.
static void snap_to_superellipsoid(Vec3& co, float super_r, bool midline) {
    float r = super_r;
    if (r == PRO_CIRCLE_R) {
        co = normalize(co);
        return;
    }

    double a = std::max(0.0, co.x);
    double b = std::max(0.0, co.y);
    double c = std::max(0.0, co.z);
    double x = a, y = b, z = c;

    if (r == PRO_SQUARE_R || r == PRO_SQUARE_IN_R) {
        z = 0.0;
        x = std::min(1.0, x);
        y = std::min(1.0, y);
        if (r == PRO_SQUARE_R) {
            double dx = 1.0 - x;
            double dy = 1.0 - y;
            if (dx < dy) {
                x = 1.0;
                y = midline ? 1.0 : y;
            } else {
                y = 1.0;
                x = midline ? 1.0 : x;
            }
        } else {
            if (x < y) {
                x = 0.0;
                y = midline ? 0.0 : y;
            } else {
                y = 0.0;
                x = midline ? 0.0 : x;
            }
        }
    } else {
        double rinv = 1.0 / r;
        if (a == 0.0) {
            if (b == 0.0) {
                x = 0.0;
                y = 0.0;
                z = std::pow(c, rinv);
            } else {
                x = 0.0;
                y = std::pow(1.0 / (1.0 + std::pow(c / b, r)), rinv);
                z = c * y / b;
            }
        } else {
            x = std::pow(1.0 / (1.0 + std::pow(b / a, r) + std::pow(c / a, r)), rinv);
            y = b * x / a;
            z = c * x / a;
        }
    }
    co.x = x;
    co.y = y;
    co.z = z;
}

/// Build 4x4 transform from unit-cube corner space to world space (Blender make_unit_cube_map).
/// va, vb, vc are the three boundary vert positions; vd is the original vertex position.
static Mat4 make_unit_cube_map(const Vec3& va, const Vec3& vb, const Vec3& vc, const Vec3& vd) {
    Mat4 mat;
    // Column 0: (va - vb - vc + vd) * 0.5
    Vec3 col0 = scale(sub(add(va, vd), add(vb, vc)), 0.5);
    // Column 1: (vb - va - vc + vd) * 0.5
    Vec3 col1 = scale(sub(add(vb, vd), add(va, vc)), 0.5);
    // Column 2: (vc - va - vb + vd) * 0.5
    Vec3 col2 = scale(sub(add(vc, vd), add(va, vb)), 0.5);
    // Column 3 (translation): (va + vb + vc - vd) * 0.5
    Vec3 col3 = scale(sub(add(va, add(vb, vc)), vd), 0.5);

    mat.m[0][0] = col0.x; mat.m[0][1] = col0.y; mat.m[0][2] = col0.z; mat.m[0][3] = 0;
    mat.m[1][0] = col1.x; mat.m[1][1] = col1.y; mat.m[1][2] = col1.z; mat.m[1][3] = 0;
    mat.m[2][0] = col2.x; mat.m[2][1] = col2.y; mat.m[2][2] = col2.z; mat.m[2][3] = 0;
    mat.m[3][0] = col3.x; mat.m[3][1] = col3.y; mat.m[3][2] = col3.z; mat.m[3][3] = 1;
    return mat;
}

/// Build PRO_SQUARE_R cube corner VMesh (Blender make_cube_corner_square).
static std::unique_ptr<VMesh> make_cube_corner_square(int nseg) {
    int ns2 = nseg / 2;
    auto vm = std::make_unique<VMesh>();
    vm->seg = nseg;
    vm->count = 0;
    vm->mesh.assign(static_cast<size_t>(3) * (ns2 + 1) * (nseg + 1), NewVert{});
    vm->boundstart = nullptr;

    for (int i = 0; i < 3; i++) {
        Vec3 co{0, 0, 0};
        co[i] = 1.0;
        add_new_bound_vert(vm.get(), co);
    }
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j <= ns2; j++) {
            for (int k = 0; k <= ns2; k++) {
                if (!is_canon(vm.get(), i, j, k))
                    continue;
                Vec3 co;
                co[i] = 1.0;
                co[(i + 1) % 3] = static_cast<double>(k) * 2.0 / nseg;
                co[(i + 2) % 3] = static_cast<double>(j) * 2.0 / nseg;
                vm->at(i, j, k).co = co;
            }
        }
    }
    vmesh_copy_equiv_verts(vm.get());
    return vm;
}

/// Build PRO_SQUARE_IN_R cube corner VMesh (Blender make_cube_corner_square_in).
static std::unique_ptr<VMesh> make_cube_corner_square_in(int nseg) {
    int ns2 = nseg / 2;
    int odd = nseg % 2;
    auto vm = std::make_unique<VMesh>();
    vm->seg = nseg;
    vm->count = 0;
    vm->mesh.assign(static_cast<size_t>(3) * (ns2 + 1) * (nseg + 1), NewVert{});
    vm->boundstart = nullptr;

    for (int i = 0; i < 3; i++) {
        Vec3 co{0, 0, 0};
        co[i] = 1.0;
        add_new_bound_vert(vm.get(), co);
    }

    double b;
    if (odd) {
        b = 2.0 / (2.0 * static_cast<double>(ns2) + 1.4142135623730951);
    } else {
        b = 2.0 / static_cast<double>(nseg);
    }
    for (int i = 0; i < 3; i++) {
        for (int k = 0; k <= ns2; k++) {
            Vec3 co;
            co[i] = 1.0 - static_cast<double>(k) * b;
            co[(i + 1) % 3] = 0.0;
            co[(i + 2) % 3] = 0.0;
            vm->at(i, 0, k).co = co;

            co[(i + 1) % 3] = 1.0 - static_cast<double>(k) * b;
            co[(i + 2) % 3] = 0.0;
            co[i] = 0.0;
            vm->at(i, 0, nseg - k).co = co;
        }
    }
    return vm;
}

/// Interpolate VMesh from power-of-2 seg to target seg (Blender interp_vmesh).
/// Extracted from adj_vmesh so make_cube_corner_adj_vmesh can reuse it.
static std::unique_ptr<VMesh> interp_vmesh(BevelParams& bp, VMesh* vm_in, int nseg) {
    int n_bndv = vm_in->count;
    int source_seg = vm_in->seg;
    int target_half = nseg / 2;

    auto target = std::make_unique<VMesh>();
    target->seg = nseg;
    target->count = n_bndv;
    target->boundstart = vm_in->boundstart;
    target->mesh.assign(static_cast<size_t>(n_bndv) * (target_half + 1) * (nseg + 1), NewVert{});

    std::vector<double> previous(static_cast<size_t>(source_seg + 1));
    std::vector<double> current(static_cast<size_t>(source_seg + 1));
    std::vector<double> previous_target(static_cast<size_t>(nseg + 1));
    std::vector<double> target_profile(static_cast<size_t>(nseg + 1));

    auto fill_ring = [](VMesh* mesh, int i, std::vector<double>& fractions) {
        fractions[0] = 0.0;
        double total = 0.0;
        for (int k = 0; k < mesh->seg; ++k) {
            total += length(sub(mesh->at(i, 0, k).co, mesh->at(i, 0, k + 1).co));
            fractions[static_cast<size_t>(k + 1)] = total;
        }
        if (total > 1e-12) {
            for (double& value : fractions) value /= total;
        } else {
            fractions.back() = 1.0;
        }
    };
    auto fill_profile = [&](BoundVert* bound, std::vector<double>& fractions) {
        fractions[0] = 0.0;
        double total = 0.0;
        Vec3 previous_point = bound->nv.co;
        for (int k = 0; k < nseg; ++k) {
            Vec3 next = get_profile_point(bound->profile, k + 1, nseg, bp.seg);
            total += length(sub(previous_point, next));
            fractions[static_cast<size_t>(k + 1)] = total;
            previous_point = next;
        }
        if (total > 1e-12) {
            for (double& value : fractions) value /= total;
        } else {
            fractions.back() = 1.0;
        }
    };
    auto interp_range = [](const std::vector<double>& fractions, double value, double& rest) {
        const int n = static_cast<int>(fractions.size()) - 1;
        for (int index = 0; index < n; ++index) {
            if (value <= fractions[static_cast<size_t>(index + 1)] + 1e-12) {
                const double span = fractions[static_cast<size_t>(index + 1)] - fractions[static_cast<size_t>(index)];
                rest = span > 1e-12 ? (value - fractions[static_cast<size_t>(index)]) / span : 0.0;
                return index;
            }
        }
        rest = 0.0;
        return n;
    };
    auto bilerp = [](const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d,
                     double u, double v) {
        return add(add(scale(a, (1.0 - u) * (1.0 - v)), scale(b, u * (1.0 - v))),
                   add(scale(c, u * v), scale(d, (1.0 - u) * v)));
    };

    BoundVert* bound = vm_in->boundstart;
    fill_ring(vm_in, n_bndv - 1, previous);
    fill_profile(bound->prev, previous_target);
    for (int i = 0; i < n_bndv; ++i) {
        fill_ring(vm_in, i, current);
        fill_profile(bound, target_profile);
        for (int j = 0; j <= target_half - 1 + (nseg % 2); ++j) {
            for (int k = 0; k <= target_half; ++k) {
                double rest_k = 0.0, rest_prev = 0.0;
                const int source_k = interp_range(current, target_profile[static_cast<size_t>(k)], rest_k);
                const int source_k_prev = interp_range(previous, previous_target[static_cast<size_t>(nseg - j)], rest_prev);
                int source_j = source_seg - source_k_prev;
                double rest_j = -rest_prev;
                if (rest_j > -1e-6) rest_j = 0.0;
                else { --source_j; rest_j += 1.0; }
                const int j_inc = (rest_j < 1e-6 || source_j == source_seg) ? 0 : 1;
                const int k_inc = (rest_k < 1e-6 || source_k == source_seg) ? 0 : 1;
                const Vec3& a = mesh_vert_canon(vm_in, i, source_j, source_k).co;
                const Vec3& b = mesh_vert_canon(vm_in, i, source_j, source_k + k_inc).co;
                const Vec3& c = mesh_vert_canon(vm_in, i, source_j + j_inc, source_k + k_inc).co;
                const Vec3& d = mesh_vert_canon(vm_in, i, source_j + j_inc, source_k).co;
                target->at(i, j, k).co = bilerp(a, b, c, d, rest_k, rest_j);
            }
        }
        bound = bound->next;
        previous = current;
        previous_target = target_profile;
    }
    if (nseg % 2 == 0) {
        Vec3 center{};
        for (int i = 0; i < n_bndv; ++i)
            center = add(center, target->at(i, target_half, target_half).co);
        center = scale(center, 1.0 / static_cast<double>(n_bndv));
        target->at(0, target_half, target_half).co = center;
    }
    vmesh_copy_equiv_verts(target.get());
    return target;
}

/// Build unit-sphere-octant VMesh for cube corner (Blender make_cube_corner_adj_vmesh).
/// BoundVerts at (1,0,0), (0,1,0), (0,0,1) in unit cube space.
/// After cubic_subdiv + interp_vmesh, snaps each vertex to the superellipsoid.
static std::unique_ptr<VMesh> make_cube_corner_adj_vmesh(BevelParams& bp) {
    auto dump_vm = [](const char* stage, const VMesh* vm, int nseg) {
        if (!bevel_diag_enabled()) return;
        int ns2 = nseg / 2;
        std::fprintf(stderr, "[PCG_BEVEL_DIAG] cube_corner %s seg=%d\n", stage, nseg);
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j <= ns2; j++) {
                for (int k = 0; k <= nseg; k++) {
                    const auto& v = vm->at(i, j, k).co;
                    std::fprintf(stderr, "[PCG_BEVEL_DIAG]   vm[%d][%d][%d] = (%.10f, %.10f, %.10f)\n",
                        i, j, k, v.x, v.y, v.z);
                }
            }
        }
    };
    int nseg = bp.seg;
    float r = bp.pro_super_r;

    if (r == PRO_SQUARE_R) {
        return make_cube_corner_square(nseg);
    }
    if (r == PRO_SQUARE_IN_R) {
        return make_cube_corner_square_in(nseg);
    }

    // Initial mesh: 3 sides, 2 segments each.
    auto vm0 = std::make_unique<VMesh>();
    vm0->seg = 2;
    vm0->count = 0;
    vm0->mesh.assign(static_cast<size_t>(3) * 2 * 3, NewVert{});
    vm0->boundstart = nullptr;

    for (int i = 0; i < 3; i++) {
        Vec3 co{0, 0, 0};
        co[i] = 1.0;
        add_new_bound_vert(vm0.get(), co);
    }

    BoundVert* bndv = vm0->boundstart;
    for (int i = 0; i < 3; i++) {
        // Point halfway around the arc between this and next boundvert.
        Vec3 coc{0, 0, 0};
        coc[i] = 1.0;
        coc[(i + 1) % 3] = 1.0;
        coc[(i + 2) % 3] = 0.0;

        bndv->profile.super_r = r;
        bndv->profile.start = bndv->nv.co;
        bndv->profile.end = bndv->next->nv.co;
        bndv->profile.middle = coc;
        vm0->at(i, 0, 0).co = bndv->profile.start;
        bndv->profile.plane_co = bndv->profile.start;
        bndv->profile.plane_no = cross(bndv->profile.start, bndv->profile.end);
        bndv->profile.proj_dir = bndv->profile.plane_no;
        calculate_profile(bp, bndv);

        // Sample profile at halfway point.
        vm0->at(i, 0, 1).co = get_profile_point(bndv->profile, 1, 2, bp.seg);

        bndv = bndv->next;
    }

    // Center vertex at (sqrt(1/3), sqrt(1/3), sqrt(1/3)) with fullness adjustment.
    constexpr double kSqrt1_3 = 0.5773502691896258;  // sqrt(1/3)
    Vec3 co{kSqrt1_3, kSqrt1_3, kSqrt1_3};
    if (nseg > 2) {
        if (r > 1.5f) {
            co = scale(co, 1.4);
        } else if (r < 0.75f) {
            co = scale(co, 0.6);
        }
    }
    vm0->at(0, 1, 1).co = co;

    vmesh_copy_equiv_verts(vm0.get());

    dump_vm("initial", vm0.get(), 2);

    std::unique_ptr<VMesh> vm1 = std::move(vm0);
    while (vm1->seg < nseg) {
        vm1 = cubic_subdiv(bp, vm1.get());
    }
    dump_vm("after_cubic_subdiv", vm1.get(), vm1->seg);

    if (vm1->seg != nseg) {
        vm1 = interp_vmesh(bp, vm1.get(), nseg);
    }
    dump_vm("after_interp", vm1.get(), nseg);

    // Snap each vertex to the superellipsoid.
    int ns2 = nseg / 2;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j <= ns2; j++) {
            for (int k = 0; k <= nseg; k++) {
                snap_to_superellipsoid(vm1->at(i, j, k).co, r, false);
            }
        }
    }
    dump_vm("after_snap", vm1.get(), nseg);

    return vm1;
}

/// Compute signed dihedral angle for an edge (Blender BM_edge_calc_face_angle_signed_ex).
static float edge_face_angle_signed(const BevelParams& bp, const EdgeHalf* e) {
    if (e->fprev < 0 || e->fnext < 0) return 0.0f;
    Vec3 n1 = bmesh_face_normal(bp, e->fprev);
    Vec3 n2 = bmesh_face_normal(bp, e->fnext);
    Vec3 edge_dir = sub((*bp.positions)[static_cast<size_t>(e->edge_v1)],
                         (*bp.positions)[static_cast<size_t>(e->edge_v0)]);
    Vec3 cr = cross(n1, n2);
    double ang = angle_normalized_v3v3(n1, n2);
    if (dot(cr, edge_dir) > 0.0)
        ang = -ang;
    return static_cast<float>(ang);
}

/// Test if BevVert is a cube-corner candidate (Blender tri_corner_test).
/// Returns 1 = yes, 0 = not cube corner but OK to continue, -1 = not usable.
static int tri_corner_test(const BevelParams& bp, const BevVert* bv) {
    int in_plane_e = 0;

    if (bv->vmesh->count != 3) {
        return 0;
    }

    // Only use tri-corner if offset is the same for every edge.
    float offset = bv->edges[0].offset_l;

    float totang = 0.0f;
    for (int i = 0; i < bv->edgecount; i++) {
        const EdgeHalf* e = &bv->edges[static_cast<size_t>(i)];
        float ang = edge_face_angle_signed(bp, e);
        float absang = std::fabs(ang);
        if (absang <= static_cast<float>(M_PI_4)) {
            in_plane_e++;
        } else if (absang >= 3.0f * static_cast<float>(M_PI_4)) {
            return -1;
        }

        if (e->is_bev && std::fabs(e->offset_l - offset) > static_cast<float>(BEVEL_EPSILON)) {
            return -1;
        }

        totang += ang;
    }

    if (in_plane_e != bv->edgecount - 3) {
        return -1;
    }

    float angdiff = std::fabs(std::fabs(totang) - 3.0f * static_cast<float>(M_PI_2));
    if ((bp.pro_super_r == PRO_SQUARE_R && angdiff > static_cast<float>(M_PI) / 16.0f) ||
        (angdiff > static_cast<float>(M_PI_4))) {
        return -1;
    }

    if (bv->edgecount != 3 || bv->selcount != 3) {
        return 0;
    }
    return 1;
}

/// Return the boundary vertex that starts a Blender-style pipe VMesh, or null.
/// A pipe has three or four beveled edges with two collinear pipe edges and all
/// incident face planes parallel to the pipe direction.
static BoundVert* pipe_test(const BevelParams& bp, BevVert* bv) {
    VMesh* vm = bv->vmesh.get();
    if (!vm || vm->count < 3 || vm->count > 4 ||
        bv->selcount < 3 || bv->selcount > 4) {
        return nullptr;
    }

    BoundVert* pipe_start = vm->boundstart;
    Vec3 pipe_direction{};
    do {
        BoundVert* middle = pipe_start->next;
        BoundVert* opposite = middle->next;
        if (pipe_start->ebev && middle->ebev && opposite->ebev) {
            const Vec3& vertex = (*bp.positions)[static_cast<size_t>(bv->v_idx)];
            const Vec3& first_other =
                (*bp.positions)[static_cast<size_t>(pipe_start->ebev->edge_v1)];
            const Vec3& opposite_other =
                (*bp.positions)[static_cast<size_t>(opposite->ebev->edge_v1)];
            const Vec3 first_direction = normalize(sub(vertex, first_other));
            const Vec3 opposite_direction = normalize(sub(opposite_other, vertex));
            if (angle_normalized_v3v3(first_direction, opposite_direction) <
                BEVEL_EPSILON_ANG) {
                pipe_direction = first_direction;
                break;
            }
        }
        pipe_start = pipe_start->next;
    } while (pipe_start != vm->boundstart);

    if (length_squared(pipe_direction) <= BEVEL_EPSILON_SQ)
        return nullptr;

    for (const EdgeHalf& edge : bv->edges) {
        const double face_dot = edge.fnext >= 0
                                    ? std::abs(dot(pipe_direction,
                                                   bmesh_face_normal(bp, edge.fnext)))
                                    : 0.0;
        if (edge.fnext >= 0 && face_dot > BEVEL_EPSILON_BIG) {
            if (bevel_diag_enabled()) {
                std::fprintf(stderr,
                             "[PCG_BEVEL_DIAG] pipe_reject v=%d face=%d dot=%.9f\n",
                             bv->v_idx, edge.fnext, face_dot);
            }
            return nullptr;
        }
    }
    if (bevel_diag_enabled())
        std::fprintf(stderr, "[PCG_BEVEL_DIAG] pipe_accept v=%d\n", bv->v_idx);
    return pipe_start;
}

/// Build cube-corner VMesh and transform to world space (Blender tri_corner_adj_vmesh).
static std::unique_ptr<VMesh> tri_corner_adj_vmesh(BevelParams& bp, const BevVert* bv) {
    const BoundVert* bndv = bv->vmesh->boundstart;

    Vec3 co0 = bndv->nv.co;
    bndv = bndv->next;
    Vec3 co1 = bndv->nv.co;
    bndv = bndv->next;
    Vec3 co2 = bndv->nv.co;

    Vec3 original_vertex = (*bp.positions)[static_cast<size_t>(bv->v_idx)];
    Mat4 mat = make_unit_cube_map(co0, co1, co2, original_vertex);

    int ns = bp.seg;
    int ns2 = ns / 2;
    auto vm = make_cube_corner_adj_vmesh(bp);

    for (int i = 0; i < 3; i++) {
        for (int j = 0; j <= ns2; j++) {
            for (int k = 0; k <= ns; k++) {
                vm->at(i, j, k).co = mul_m4v3(mat, vm->at(i, j, k).co);
            }
        }
    }

    return vm;
}

/// Build ADJ vertex mesh using subdivision (Blender adj_vmesh).
std::unique_ptr<VMesh> adj_vmesh(BevelParams& bp, BevVert* bv) {
    int n_bndv = bv->vmesh->count;
    int nseg = bv->vmesh->seg;

    // Cube corner special case: build in unit space + snap to superellipsoid (Blender adj_vmesh:5089).
    if (n_bndv == 3 && tri_corner_test(bp, bv) == 1 && bp.pro_super_r != PRO_SQUARE_IN_R) {
        return tri_corner_adj_vmesh(bp, bv);
    }

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
    while (vm1->seg < nseg) {
        vm1 = cubic_subdiv(bp, vm1.get());
    }

    // For odd seg values, cubic_subdiv overshoots to the next power of two.
    // Blender interpolates the complete control mesh into target-sized storage.
    if (vm1->seg != nseg) {
        vm1 = interp_vmesh(bp, vm1.get(), nseg);
    }

    return vm1;
}

static Vec3 closest_to_line_segment(const Vec3& point,
                                    const Vec3& start,
                                    const Vec3& end) {
    const Vec3 direction = sub(end, start);
    const double length_sq = length_squared(direction);
    if (length_sq <= 1e-20)
        return start;
    const double factor =
        std::clamp(dot(sub(point, start), direction) / length_sq, 0.0, 1.0);
    return madd(start, direction, factor);
}

/// Snap a coordinate to the pipe profile in its perpendicular cross-section.
static void snap_to_pipe_profile(const BevelParams& bp,
                                 BoundVert* pipe_start,
                                 bool midline,
                                 Vec3& coordinate) {
    const Profile& profile = pipe_start->profile;
    if (length_squared(sub(profile.start, profile.end)) <= BEVEL_EPSILON_SQ) {
        coordinate = profile.start;
        return;
    }

    const EdgeHalf* edge = pipe_start->ebev;
    const Vec3 edge_direction = normalize(sub(
        (*bp.positions)[static_cast<size_t>(edge->edge_v0)],
        (*bp.positions)[static_cast<size_t>(edge->edge_v1)]));
    const Vec3 start_plane = closest_to_plane(coordinate, edge_direction, profile.start);
    const Vec3 end_plane = closest_to_plane(coordinate, edge_direction, profile.end);
    const Vec3 middle_plane = closest_to_plane(coordinate, edge_direction, profile.middle);

    Mat4 map;
    Mat4 inverse;
    if (make_unit_square_map(start_plane, middle_plane, end_plane, map) &&
        invert_m4(map, inverse)) {
        Vec3 profile_coordinate = mul_m4v3(inverse, coordinate);
        snap_to_superellipsoid(profile_coordinate, profile.super_r, midline);
        coordinate = mul_m4v3(map, profile_coordinate);
    } else {
        coordinate = closest_to_line_segment(coordinate, start_plane, end_plane);
    }
}

/// Blender pipe_adj_vmesh: build the normal ADJ mesh, then constrain its
/// interior vertices to the cross-section of the pair of collinear pipe edges.
static std::unique_ptr<VMesh> pipe_adj_vmesh(BevelParams& bp,
                                             BevVert* bv,
                                             BoundVert* pipe_start) {
    auto vm = adj_vmesh(bp, bv);
    const int boundary_count = bv->vmesh->count;
    const int segments = bv->vmesh->seg;
    const int half_segments = segments / 2;
    const int first_pipe_index = pipe_start->index;
    const int second_pipe_index = pipe_start->next->next->index;

    for (int i = 0; i < boundary_count; ++i) {
        for (int j = 1; j <= half_segments; ++j) {
            for (int k = 0; k <= half_segments; ++k) {
                if (!is_canon(vm.get(), i, j, k))
                    continue;
                const bool even = (segments % 2) == 0;
                const bool midline =
                    even && k == half_segments &&
                    ((i == 0 && j == half_segments) ||
                     i == first_pipe_index || i == second_pipe_index);
                snap_to_pipe_profile(bp, pipe_start, midline, vm->at(i, j, k).co);
            }
        }
    }
    vmesh_copy_equiv_verts(vm.get());
    return vm;
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

        bp.output.add_oriented_polygon(face_verts, vert_normal);
    } while ((bndv = bndv->next) != vm->boundstart);

    if (build_center_face && n_bndv >= 3) {
        std::vector<Vec3> center_verts;
        for (int i = 0; i < n_bndv; i++)
            center_verts.push_back(vm->at(i, 1, 0).co);
        bp.output.add_oriented_polygon(center_verts, vert_normal);
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
    bp.output.add_oriented_polygon(poly, normal);
}

/// Build a triangle fan (Blender bevel_build_trifan L6342).
/// Blender first builds the same profile-aware polygon as bevel_build_poly,
/// then repeatedly splits it from the vertex preceding the polygon start.
void bevel_build_trifan(BevelParams& bp, BevVert* bv) {
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

    const Vec3 normal = vertex_normal({*bp.positions, *bp.triangles}, bv->v_idx);
    const Vec3& fan_vertex = poly.back();
    for (size_t i = 0; i + 2 < poly.size(); ++i)
        bp.output.add_oriented_triangle(fan_vertex, poly[i], poly[i + 1], normal);
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

    if (bevel_diag_enabled()) {
        std::fprintf(stderr,
                     "[PCG_BEVEL_DIAG] vmesh_input v=%d edges=%d selected=%d bounds=%d kind=%d\n",
                     bv->v_idx, bv->edgecount, bv->selcount, n,
                     static_cast<int>(vm->mesh_kind));
    }

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

    if (weld && weld1 && weld2) {
        set_profile_params(bp, bv, weld1);
        set_profile_params(bp, bv, weld2);
        move_weld_profile_planes(
            weld1, weld2, (*bp.positions)[static_cast<size_t>(bv->v_idx)]);
    }

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
            const Vec3& co1 = vm->at(weld1->index, 0, k).co;
            const Vec3& co2 = vm->at(weld2->index, 0, ns - k).co;
            Vec3 co;
            if (weld1->profile.super_r == PRO_LINE_R &&
                weld2->profile.super_r != PRO_LINE_R) {
                co = co2;
            } else if (weld2->profile.super_r == PRO_LINE_R &&
                       weld1->profile.super_r != PRO_LINE_R) {
                co = co1;
            } else {
                co = mid(co1, co2);
            }
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
        BoundVert* pipe_start = nullptr;
        if ((n == 3 || n == 4) && ns > 1)
            pipe_start = pipe_test(bp, bv);
        auto vm_adj = pipe_start ? pipe_adj_vmesh(bp, bv, pipe_start) : adj_vmesh(bp, bv);

        // Copy final vmesh into bv->vmesh and create output triangles
        VMesh* vm_out = bv->vmesh.get();
        for (int i = 0; i < n; i++) {
            for (int j = 0; j <= ns2; j++) {
                for (int k = 0; k <= ns; k++) {
                    if (j == 0 && (k == 0 || k == ns)) continue;
                    vm_out->at(i, j, k).co = vm_adj->at(i, j, k).co;
                    vm_out->at(i, j, k).valid = true;
                }
            }
        }

        // Create F_VERT quads with Blender loop order (v1,v2,v3,v4) — no mesh-center flip.
        // VMesh quads: generate ALL rings including j=0 (boundary). The face
        // rebuild inserts BoundVert positions only (vstart==vend for box corners),
        // so face polygon edges are the full-span edges shared with edge strips.
        // VMesh j=0 quad boundary ring edges are shared with edge strip side edges.
        const int odd = ns % 2;
        // Encode BevVert index as negative face_origin for VMesh triangles.
        const int vm_origin = -static_cast<int>(bv - &bp.bevverts[0]) - 1;
        bndv = vm_out->boundstart;
        do {
            const int i = bndv->index;
            const Vec3 vert_pos = (*bp.positions)[static_cast<size_t>(bv->v_idx)];
            for (int j = 0; j < ns2; j++) {
                for (int k = 0; k < ns2 + odd; k++) {
                    const Vec3 v1 = vm_out->at(i, j, k).co;
                    const Vec3 v2 = vm_out->at(i, j, k + 1).co;
                    const Vec3 v3 = vm_out->at(i, j + 1, k + 1).co;
                    const Vec3 v4 = vm_out->at(i, j + 1, k).co;
                    if (length_squared(sub(v1, v2)) > 1e-16 ||
                        length_squared(sub(v3, v4)) > 1e-16) {
                        const Vec3 outward = normalize(sub(vert_pos, bp.mesh_center));
                        bp.output.current_face_origin = vm_origin;
                        bp.output.add_oriented_quad(v1, v2, v3, v4, outward);
                        bp.output.current_face_origin = -1;
                    }
                }
            }
        } while ((bndv = bndv->next) != vm_out->boundstart);

        // Center ngon (Blender builds it after the ring loop).
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
        } else if (collapsed_left && collapsed_right) {
            if (length_squared(sub(v0, v1)) > 1e-16 && length_squared(sub(v1, v2)) > 1e-16)
                bp.output.add_oriented_triangle(v0, v1, v2, desired_normal);
        } else if (collapsed_left) {
            if (length_squared(sub(v0, v1)) > 1e-16 && length_squared(sub(v1, v2)) > 1e-16)
                bp.output.add_oriented_triangle(v0, v1, v2, desired_normal);
        } else {
            if (length_squared(sub(v0, v1)) > 1e-16 && length_squared(sub(v0, v3)) > 1e-16)
                bp.output.add_oriented_triangle(v0, v1, v3, desired_normal);
        }
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

    // When the face loop contains a vertex that IS a BevVert but the face's
    // adjacent loop vertex is not directly connected (e.g. the loop vertex is
    // a subdivision midpoint on an edge that was merged in the BMesh), find
    // the edge whose direction is closest to the target vertex.
    auto find_nearest_edge_half = [&](BevVert* bv, int target_v,
                                      const std::vector<Vec3>& positions) -> EdgeHalf* {
        if (!bv) return nullptr;
        const Vec3& vp = positions[static_cast<size_t>(bv->v_idx)];
        const Vec3 target_dir = normalize(sub(positions[static_cast<size_t>(target_v)], vp));
        EdgeHalf* best = nullptr;
        double best_dot = -2.0;
        for (auto& eh : bv->edges) {
            Vec3 dir = normalize(sub(positions[static_cast<size_t>(eh.edge_v1)], vp));
            double d = dot(dir, target_dir);
            if (d > best_dot) {
                best_dot = d;
                best = &eh;
            }
        }
        return best;
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
            // If direct edge lookup fails (face loop vertex not in BevVert edge
            // list — e.g. subdivision midpoint on a merged edge), fall back to
            // nearest edge by direction. This ensures we use the offset-moved
            // BoundVert position instead of the original unmoved position,
            // preventing vertex-index splits that create boundary edges.
            if (bv && vm && vm->boundstart) {
                if (!edge)
                    edge = find_nearest_edge_half(bv, next_v, welded.positions);
                if (!edge_prev)
                    edge_prev = find_nearest_edge_half(bv, prev_v, welded.positions);
            }
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

        if (polygon.size() >= 3) {
            bp.output.current_face_origin = fi;
            bp.output.add_oriented_polygon(polygon, face_normal);
            bp.output.current_face_origin = -1;
        }
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
    // Pipeline: Group candidates → Exclude → Limit Method (Angle/None).
    // Empty edge_group = all BMesh edges. Unknown group must not fall back to all edges.
    BevelLimitMethod limit = selection.limit_method;
    if (!selection.limit_method_explicit)
        limit = selection.edge_group.empty() ? BevelLimitMethod::Angle : BevelLimitMethod::None;

    std::unordered_set<int64_t> candidates;

    if (!selection.edge_group.empty()) {
        if (!geometry)
            return {};
        const auto selected =
            geometry->groups().eval(geometry::GroupDomain::Edge, selection.edge_group);
        for (int64_t key : selected) {
            const auto it = bmesh.edges.find(key);
            if (it == bmesh.edges.end())
                continue;
            if (edge_excluded(bmesh, it->second, selection))
                continue;
            candidates.insert(key);
        }
    } else {
        for (const auto& entry : bmesh.edges) {
            if (edge_excluded(bmesh, entry.second, selection))
                continue;
            candidates.insert(entry.first);
        }
    }

    if (limit == BevelLimitMethod::None)
        return candidates;

    std::unordered_set<int64_t> hard_edges;
    for (int64_t key : candidates) {
        const auto it = bmesh.edges.find(key);
        if (it == bmesh.edges.end())
            continue;
        if (!it->second.sharp)
            continue;
        hard_edges.insert(key);
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
    const data::PcgGeometry* geometry,
    data::PcgGeometry* out_geometry)
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

    // 1. Build BMesh from geometry without coplanar merge — aligns with Blender
    //    where bevel operates on the full mesh topology. The sharp_angle_deg
    //    threshold alone determines which edges to bevel: Simple subdiv edges
    //    (0° dihedral) are not sharp; CC artifacts (~26.75°) are filtered by
    //    angle_limit; original cube edges (90°) are sharp.
    geometry::BMeshBuildOptions bmesh_opts;
    bmesh_opts.merge_coplanar_angle_deg = 0.0;
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
    const bool axis_aligned_rounded_box =
        std::abs(profile - 0.5f) < 1e-4f &&
        hard_edges.size() == bmesh.edges.size() &&
        bmesh_is_axis_aligned_box(bmesh, box_min, box_max);

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
    bp.offset_adjust = axis_aligned_rounded_box || offset_type != BevelOffsetType::Width;
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
    set_profile_spacing(segments, super_r, profile, bp.pro_spacing);

    // 5. Build BevVerts
    // Find all vertices that are endpoints of hard edges
    std::set<int> bevel_verts;
    for (int64_t key : hard_edges) {
        const auto endpoints = geometry::edge_group_points(key);
        bevel_verts.insert(endpoints[0]);
        bevel_verts.insert(endpoints[1]);
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

    // 6. Build boundaries immediately only when overlap limiting is disabled.
    // Blender delays the first boundary build until after bevel_limit_offset when
    // clamp overlap is enabled; building twice appends duplicate BoundVerts.
    for (auto& bv : bp.bevverts) {
        if (bevel_cancel_requested(bp))
            return mesh;
        bv.vmesh = std::make_unique<VMesh>();
        bv.vmesh->seg = segments;
        if (!clamp_overlap)
            build_boundary(bp, &bv, true);
    }

    // 7. Limit offset (clamp_overlap)
    if (clamp_overlap) {
        bevel_limit_offset(bp);
        for (auto& bv : bp.bevverts) {
            if (bevel_cancel_requested(bp))
                return mesh;
            build_boundary(bp, &bv, true);
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

    log_bevel_stage("after_face_rebuild", bp.output, diag_cap);

    // 12. Vertex-level weld + face cleanup + cyclic dedup.
    //     Faces are the single source of truth. After welding vertices and
    //     cleaning face loops, rebuild_triangles_from_faces() derives the
    //     flat triangle buffer for legacy consumers.
    {
        // Step A: weld vertices at 1e-5 precision.
        auto pos_key = [](const Vec3& v) -> std::string {
            auto q = [](double val) { return static_cast<int64_t>(std::llround(val / 1e-5)); };
            return std::to_string(q(v.x)) + ',' + std::to_string(q(v.y)) + ',' + std::to_string(q(v.z));
        };

        std::unordered_map<std::string, int> weld_map;
        std::vector<int> remap(bp.output.vertices.size(), -1);
        std::vector<Vec3> welded_verts;

        for (size_t vi = 0; vi < bp.output.vertices.size(); ++vi) {
            const std::string key = pos_key(bp.output.vertices[vi]);
            auto it = weld_map.find(key);
            if (it != weld_map.end()) {
                remap[vi] = it->second;
            } else {
                int new_idx = static_cast<int>(welded_verts.size());
                welded_verts.push_back(bp.output.vertices[vi]);
                weld_map[key] = new_idx;
                remap[vi] = new_idx;
            }
        }

        // Step B: remap face indices, fold dups, remove degenerate,
        //         cyclic dedup. Then rebuild_triangles_from_faces().
        auto cyclic_canonical = [](const std::vector<int>& verts) -> std::string {
            int n = static_cast<int>(verts.size());
            int min_start = 0;
            for (int i = 1; i < n; ++i) {
                for (int j = 0; j < n; ++j) {
                    if (verts[(i + j) % n] < verts[(min_start + j) % n]) { min_start = i; break; }
                    if (verts[(i + j) % n] > verts[(min_start + j) % n]) break;
                }
            }
            std::string key;
            for (int j = 0; j < n; ++j)
                key += std::to_string(verts[(min_start + j) % n]) + ",";
            return key;
        };

        std::vector<BevelParams::OutputFace> cleaned_faces;
        std::unordered_map<std::string, size_t> seen_face_keys;
        int skipped_degenerate = 0;
        int replaced_geo = 0;

        for (const auto& face : bp.output.faces) {
            if (bevel_cancel_requested(bp))
                return mesh;

            // Remap indices
            std::vector<int> remapped;
            remapped.reserve(face.verts.size());
            for (int v : face.verts)
                remapped.push_back(remap[v]);

            // Fold consecutive dups
            std::vector<int> clean;
            for (int v : remapped) {
                if (!clean.empty() && v == clean.back()) continue;
                clean.push_back(v);
            }
            // Remove last if == first
            if (clean.size() > 1 && clean.front() == clean.back())
                clean.pop_back();
            if (clean.size() < 3) {
                ++skipped_degenerate;
                continue;
            }

            // Check >= 3 distinct vertices
            std::unordered_set<int> distinct(clean.begin(), clean.end());
            if (distinct.size() < 3) {
                ++skipped_degenerate;
                continue;
            }

            // Newell area check (zero-area face)
            Vec3 normal_accum{};
            for (size_t i = 0; i < clean.size(); ++i) {
                const Vec3& curr = welded_verts[clean[i]];
                const Vec3& next = welded_verts[clean[(i + 1) % clean.size()]];
                normal_accum.x += (curr.y - next.y) * (curr.z + next.z);
                normal_accum.y += (curr.z - next.z) * (curr.x + next.x);
                normal_accum.z += (curr.x - next.x) * (curr.y + next.y);
            }
            if (length_squared(normal_accum) < 1e-20) {
                ++skipped_degenerate;
                continue;
            }

            // Cyclic dedup
            std::string canon = cyclic_canonical(clean);
            auto it = seen_face_keys.find(canon);
            if (it != seen_face_keys.end()) {
                ++replaced_geo;
                continue;
            }
            seen_face_keys.emplace(canon, cleaned_faces.size());
            cleaned_faces.push_back({std::move(clean), face.origin});
        }

        bp.output.vertices = std::move(welded_verts);
        bp.output.faces = std::move(cleaned_faces);
        bp.output.vertex_cache.clear();
        bp.output.rebuild_triangles_from_faces();

        if (bevel_diag_enabled()) {
            std::fprintf(stderr,
                         "[PCG_BEVEL_DIAG] weld+dedup faces_before=%zu faces_after=%zu "
                         "skipped_degenerate=%d replaced_geo=%d\n",
                         bp.output.faces.size() + skipped_degenerate + replaced_geo,
                         bp.output.faces.size(),
                         skipped_degenerate, replaced_geo);
        }
    }

    log_bevel_stage("after_dedup", bp.output, diag_cap);

    // Enforce opposite winding on every manifold edge, then flip the whole mesh
    // if signed volume is negative. fix_winding operates on polygon faces;
    // rebuild_triangles_from_faces() syncs the derived triangle cache afterwards.
    bp.output.fix_winding();
    if (axis_aligned_rounded_box) {
        const Vec3 center = scale(add(box_min, box_max), 0.5);
        orient_faces_outward(bp.output, center);
    }
    bp.output.rebuild_triangles_from_faces();

    // 13. Build output PcgGeometry with n-gon faces and propagated face groups.
    if (out_geometry && geometry) {
        data::PcgGeometry& geom = *out_geometry;
        for (const auto& v : bp.output.vertices)
            geom.points_mut().push_back({v.x, v.y, v.z});

        for (const auto& face : bp.output.faces) {
            if (face.verts.size() < 3) continue;
            geom.faces_mut().push_back(face.verts);
            const int origin = face.origin;
            if (origin >= 0 && origin < static_cast<int>(bmesh.faces.size())) {
                const int face_idx = static_cast<int>(geom.faces().size()) - 1;
                for (const std::string& g : bmesh.faces[static_cast<size_t>(origin)].groups)
                    geom.groups().add(geometry::GroupDomain::Face, g, face_idx);
            }
        }

        // Propagate vertex colors: match output vertex positions to source bmesh
        // verts (which correspond 1:1 to geometry points when merge_coplanar=0).
        // Non-beveled vertices keep their original positions → exact match.
        // Beveled vertices have new positions → default to white.
        if (geometry->has_colors()) {
            const auto& src_colors = geometry->colors();
            auto pos_key5 = [](const auto& v) -> std::string {
                auto q = [](double val) { return static_cast<int64_t>(std::llround(val / 1e-5)); };
                return std::to_string(q(v.x)) + ',' + std::to_string(q(v.y)) + ',' + std::to_string(q(v.z));
            };
            std::unordered_map<std::string, data::PcgColor> pos_to_color;
            for (size_t i = 0; i < bmesh.verts.size() && i < src_colors.size(); ++i) {
                const std::string key = pos_key5(bmesh.verts[i]);
                pos_to_color.try_emplace(key, src_colors[i]);
            }
            std::vector<data::PcgColor> out_colors(geom.points().size(), data::PcgColor{1,1,1,1});
            for (size_t i = 0; i < geom.points().size(); ++i) {
                const Vec3 v{geom.points()[i].x, geom.points()[i].y, geom.points()[i].z};
                const std::string key = pos_key5(v);
                auto it = pos_to_color.find(key);
                if (it != pos_to_color.end())
                    out_colors[i] = it->second;
            }
            geom.set_colors(std::move(out_colors));
        }
    }

    // 14. Convert output to PcgMeshData (triangles derived from faces).
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
