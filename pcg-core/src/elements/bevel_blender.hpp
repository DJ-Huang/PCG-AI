#pragma once

// Blender-aligned bevel implementation.
// Ports architecture from bmesh_bevel.cc. EdgeHalf.fprev/fnext are BMesh face
// indices (Blender BMFace*), not welded triangle indices.

#include "data/pcg_mesh_data.hpp"

#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace pcg::internal::data {
class PcgGeometry;
}

namespace pcg::internal::geometry {
struct BMesh;
}

namespace pcg::internal::elements::bevel {

// ── Constants ───────────────────────────────────────────────────────────────

constexpr double BEVEL_EPSILON = 1e-6;
constexpr double BEVEL_EPSILON_SQ = 1e-12;
constexpr double BEVEL_EPSILON_BIG = 1e-4;
constexpr double BEVEL_EPSILON_BIG_SQ = 1e-8;
constexpr double BEVEL_EPSILON_ANG = 0.0349066;  // ~2 degrees in radians
constexpr double BEVEL_SMALL_ANG = 0.174533;     // ~10 degrees in radians

constexpr float PRO_SQUARE_R = 1e4f;
constexpr float PRO_CIRCLE_R = 2.0f;
constexpr float PRO_LINE_R = 1.0f;
constexpr float PRO_SQUARE_IN_R = 0.0f;

// ── Vec3 Math ───────────────────────────────────────────────────────────────

struct Vec3 {
    double x = 0.0, y = 0.0, z = 0.0;
    double& operator[](int i) { return (&x)[i]; }
    const double& operator[](int i) const { return (&x)[i]; }
};

Vec3 v3(double x, double y, double z);
Vec3 add(const Vec3& a, const Vec3& b);
Vec3 sub(const Vec3& a, const Vec3& b);
Vec3 scale(const Vec3& v, double s);
Vec3 negate(const Vec3& v);
double dot(const Vec3& a, const Vec3& b);
Vec3 cross(const Vec3& a, const Vec3& b);
double length(const Vec3& v);
double length_squared(const Vec3& v);
Vec3 normalize(const Vec3& v);
Vec3 mid(const Vec3& a, const Vec3& b);
Vec3 madd(const Vec3& base, const Vec3& add, double scale);
bool nearly_parallel(const Vec3& d1, const Vec3& d2);
bool nearly_parallel_normalized(const Vec3& d1, const Vec3& d2);

/// 3D line-line intersection (Blender isect_line_line_v3).
/// Returns: 0 = no intersection (skew lines, return closest points),
///          1 = lines intersect at a single point (r_i1),
///          2 = lines are collinear (r_i1 = point on line 1)
int isect_line_line_v3(const Vec3& p1, const Vec3& p2,
                       const Vec3& p3, const Vec3& p4,
                       Vec3& r_i1, Vec3& r_i2);

/// Intersect line with plane (Blender isect_line_plane_v3).
bool isect_line_plane_v3(Vec3& isect_co,
                         const Vec3& line_a, const Vec3& line_b,
                         const Vec3& plane_co, const Vec3& plane_no);

/// Closest point on plane to pt (Blender closest_to_plane_v3).
Vec3 closest_to_plane(const Vec3& plane_co, const Vec3& plane_no, const Vec3& pt);

/// Build plane equation (Blender plane_from_point_normal_v3).
void plane_from_point_normal(Vec3& r_plane_co, Vec3& r_plane_no,
                             const Vec3& p, const Vec3& normal);

/// Angle between two vectors in radians (Blender angle_v3v3).
double angle_v3v3(const Vec3& a, const Vec3& b);

/// Angle between two normalized vectors (Blender angle_normalized_v3v3).
double angle_normalized_v3v3(const Vec3& a, const Vec3& b);

/// Angle va-vb-vc (angle at vertex vb between edges to va and vc).
double angle_v3v3v3(const Vec3& va, const Vec3& vb, const Vec3& vc);

double safe_divide(double a, double b);

// ── 4x4 Matrix ──────────────────────────────────────────────────────────────

struct Mat4 {
    double m[4][4] = {};
};

/// Build a 4x4 mapping that takes (0,0,0)->start, (1,0,0)->middle, (0,1,0)->end (Blender make_unit_square_map).
bool make_unit_square_map(const Vec3& start, const Vec3& middle, const Vec3& end, Mat4& r_map);

/// Invert 4x4 matrix (Blender invert_m4_m4).
bool invert_m4(const Mat4& src, Mat4& r_inv);

/// Transform point by 4x4 matrix (Blender mul_v3_m4v3).
Vec3 mul_m4v3(const Mat4& mat, const Vec3& v);

// ── Enums ──────────────────────────────────────────────────────────────────

enum class MeshKind {
    None,    // No polygon mesh needed.
    Poly,    // A simple polygon.
    Adj,     // "Adjacent edges" mesh pattern (Catmull-Clark).
    TriFan,  // A simple polygon - fan filled.
    Cutoff,  // A triangulated face at the end of each profile.
};

enum class AngleKind {
    Smaller = -1,  // < 180 degrees
    Straight = 0,   // == 180 degrees
    Larger = 1,     // > 180 degrees (reflex)
};

enum class BevelMiter {
    Sharp,
    Patch,
    Arc,
};

enum class BevelVMeshMethod {
    Adj,
    Cutoff,
};

enum class BevelOffsetType {
    Offset,
    Width,
};

// ── Profile ─────────────────────────────────────────────────────────────────

struct ProfileSpacing {
    std::vector<double> xvals;
    std::vector<double> yvals;
    std::vector<double> xvals_2;
    std::vector<double> yvals_2;
    int seg_2 = 0;
    float fullness = 0.0f;
};

struct Profile {
    float super_r = PRO_LINE_R;
    Vec3 start{};
    Vec3 middle{};
    Vec3 end{};
    Vec3 plane_no{};
    Vec3 plane_co{};
    Vec3 proj_dir{};
    float height = 0.0f;
    std::vector<Vec3> prof_co;     // seg+1 points
    std::vector<Vec3> prof_co_2;    // seg_2+1 points
    bool special_params = false;
};

/// Superellipse function: (abs(x/a))^r + (abs(y/b))^r = 1
/// Returns y for a given x on the superellipse.
double superellipse_co(double x, float r, bool rbig);

/// Find parameter values that produce evenly-spaced chords on a superellipse.
void find_even_superellipse_chords(int seg, float super_r,
                                   std::vector<double>& xvals,
                                   std::vector<double>& yvals);

/// Get a point on the profile at parameter i (0..nseg).
Vec3 get_profile_point(const Profile& pro, int i, int nseg, int bp_seg);

/// Find the fullness parameter for the ADJ vertex mesh.
float find_profile_fullness(int seg, float super_r, float profile_param);

/// Fill ProfileSpacing with evenly-spaced superellipse chord coordinates.
void set_profile_spacing(int seg, float super_r, float profile, ProfileSpacing& pro_spacing);

// ── Core Bevel Structures ──────────────────────────────────────────────────

struct NewVert {
    Vec3 co{};
    /// Set when this VMesh slot has a real profile point (Blender mesh_vert->v != nullptr).
    bool valid = false;
};

struct EdgeHalf;

struct BoundVert {
    BoundVert* next = nullptr;
    BoundVert* prev = nullptr;
    NewVert nv{};
    EdgeHalf* efirst = nullptr;
    EdgeHalf* elast = nullptr;
    EdgeHalf* eon = nullptr;   // "edge between" this boundvert is on
    EdgeHalf* ebev = nullptr;  // beveled edge whose left side is attached here
    int index = 0;
    float sinratio = 1.0f;
    BoundVert* adjchain = nullptr;
    Profile profile{};
    bool visited = false;
    bool is_arc_start = false;
    bool is_patch_start = false;
};

struct EdgeHalf {
    EdgeHalf* next = nullptr;
    EdgeHalf* prev = nullptr;
    int edge_v0 = 0;  // vertex at this end (the BevVert's vertex)
    int edge_v1 = 0;  // vertex at other end
    /// BMesh polygon face between this edge and previous (Blender BMFace* fprev).
    int fprev = -1;
    /// BMesh polygon face between this edge and next (Blender BMFace* fnext).
    int fnext = -1;
    BoundVert* leftv = nullptr;
    BoundVert* rightv = nullptr;
    int profile_index = 0;
    int seg = 2;
    float offset_l = 0.0f;
    float offset_r = 0.0f;
    float offset_l_spec = 0.0f;
    float offset_r_spec = 0.0f;
    bool is_bev = false;
    bool is_rev = false;  // is edge_v1 the vertex at this end?
};

struct VMesh {
    std::vector<NewVert> mesh;  // n * (ns2+1) * (ns+1)
    BoundVert* boundstart = nullptr;
    int count = 0;
    int seg = 2;
    MeshKind mesh_kind = MeshKind::Adj;

    int ns2() const { return seg / 2; }

    NewVert& at(int i, int j, int k) {
        int nj = (seg / 2) + 1;
        int nk = seg + 1;
        return mesh[static_cast<size_t>(i) * nk * nj + static_cast<size_t>(j) * nk + k];
    }
    const NewVert& at(int i, int j, int k) const {
        int nj = (seg / 2) + 1;
        int nk = seg + 1;
        return mesh[static_cast<size_t>(i) * nk * nj + static_cast<size_t>(j) * nk + k];
    }
};

struct BevVert {
    int v_idx = 0;  // original vertex index in welded mesh
    int edgecount = 0;
    int selcount = 0;  // number of beveled edges
    std::vector<EdgeHalf> edges;
    std::unique_ptr<VMesh> vmesh;
};

// ── BevelParams ─────────────────────────────────────────────────────────────

struct BevelParams {
    double offset = 0.0;
    BevelOffsetType offset_type = BevelOffsetType::Offset;
    int seg = 2;
    float profile = 0.5f;
    float pro_super_r = PRO_LINE_R;
    bool loop_slide = true;
    bool limit_offset = true;
    bool offset_adjust = true;
    BevelMiter miter_outer = BevelMiter::Sharp;
    BevelMiter miter_inner = BevelMiter::Sharp;
    BevelVMeshMethod vmesh_method = BevelVMeshMethod::Adj;
    ProfileSpacing pro_spacing;
    float spread = 0.1f;
    Vec3 mesh_center{0.0, 0.0, 0.0};

    /// Canonical polygon mesh; EdgeHalf.fprev/fnext index into bmesh->faces.
    const geometry::BMesh* bmesh = nullptr;

    // Input mesh data (welded) — positions for offset math; not face identity.
    const std::vector<Vec3>* positions = nullptr;
    const std::vector<std::array<int, 3>>* triangles = nullptr;

    // Edge→face adjacency (key = edge_key(v0, v1))
    std::unordered_map<int64_t, std::array<int, 2>> edge_faces;

    // Hard edges set (edge_key values)
    std::unordered_set<int64_t> hard_edges;

    // All BevVerts, indexed by original vertex index
    std::vector<BevVert> bevverts;

    // Output mesh builder
    struct OutputFace {
        std::vector<int> verts;
        int origin = -1;
    };

    struct OutputMesh {
        std::vector<Vec3> vertices;
        std::vector<OutputFace> faces;  // PRIMARY source of truth
        int current_face_origin = -1;
        std::unordered_map<std::string, int> vertex_cache;

        // Derived cache — rebuilt by rebuild_triangles_from_faces().
        std::vector<int> triangles;
        std::vector<int> face_origins;

        int get_vertex(const Vec3& v) {
            auto quantize = [](double val) -> int64_t {
                return static_cast<int64_t>(std::llround(val / 1e-6));
            };
            std::string key = std::to_string(quantize(v.x)) + ',' +
                              std::to_string(quantize(v.y)) + ',' +
                              std::to_string(quantize(v.z));
            auto it = vertex_cache.find(key);
            if (it != vertex_cache.end())
                return it->second;
            int idx = static_cast<int>(vertices.size());
            vertices.push_back(v);
            vertex_cache[key] = idx;
            return idx;
        }

        /// Emit a face from vertex indices. Folds consecutive duplicates
        /// and first==last; <3 distinct verts → skipped.
        void emit_face(const std::vector<int>& indices) {
            if (indices.size() < 3) return;
            std::vector<int> clean;
            clean.reserve(indices.size());
            for (size_t i = 0; i < indices.size(); ++i) {
                if (!clean.empty() && indices[i] == clean.back()) continue;
                clean.push_back(indices[i]);
            }
            if (clean.size() > 1 && clean.front() == clean.back())
                clean.pop_back();
            if (clean.size() < 3) return;
            std::unordered_set<int> distinct(clean.begin(), clean.end());
            if (distinct.size() < 3) return;
            faces.push_back({std::move(clean), current_face_origin});
        }

        void emit_face(std::initializer_list<int> indices) {
            emit_face(std::vector<int>(indices));
        }

        /// Emit an oriented face from positions + desired normal.
        void emit_oriented_face(const std::vector<Vec3>& points, const Vec3& desired_normal) {
            if (points.size() < 3) return;
            Vec3 n{};
            for (size_t i = 1; i + 1 < points.size(); ++i)
                n = add(n, cross(sub(points[i], points[0]), sub(points[i + 1], points[0])));
            const bool flip = dot(n, desired_normal) < 0.0;
            std::vector<int> indices;
            indices.reserve(points.size());
            for (const auto& p : points)
                indices.push_back(get_vertex(p));
            if (flip)
                std::reverse(indices.begin(), indices.end());
            emit_face(indices);
        }

        /// Emit an oriented quad (single n-gon face, not two triangles).
        void emit_oriented_quad(const Vec3& v0, const Vec3& v1, const Vec3& v2, const Vec3& v3,
                                const Vec3& desired_normal) {
            Vec3 n = cross(sub(v1, v0), sub(v2, v0));
            if (length_squared(n) < 1e-20)
                n = cross(sub(v2, v0), sub(v3, v0));
            const bool flip = dot(n, desired_normal) < 0.0;
            const int i0 = get_vertex(v0);
            const int i1 = get_vertex(v1);
            const int i2 = get_vertex(v2);
            const int i3 = get_vertex(v3);
            if (flip)
                emit_face({i0, i3, i2, i1});
            else
                emit_face({i0, i1, i2, i3});
        }

        /// Emit an oriented triangle (single tri face).
        void emit_oriented_triangle(const Vec3& a, const Vec3& b, const Vec3& c,
                                    const Vec3& desired_normal) {
            Vec3 n = cross(sub(b, a), sub(c, a));
            int ia = get_vertex(a);
            int ib = get_vertex(b);
            int ic = get_vertex(c);
            if (dot(n, desired_normal) < 0.0)
                emit_face({ia, ic, ib});
            else
                emit_face({ia, ib, ic});
        }

        // Legacy compat wrappers — all route through emit_face.
        void add_triangle(int a, int b, int c) { emit_face({a, b, c}); }

        void add_quad(const Vec3& v0, const Vec3& v1, const Vec3& v2, const Vec3& v3) {
            emit_face({get_vertex(v0), get_vertex(v1), get_vertex(v2), get_vertex(v3)});
        }

        void add_polygon(const std::vector<Vec3>& poly) {
            if (poly.size() < 3) return;
            std::vector<int> indices;
            indices.reserve(poly.size());
            for (const auto& p : poly)
                indices.push_back(get_vertex(p));
            emit_face(indices);
        }

        void add_oriented_triangle(const Vec3& a, const Vec3& b, const Vec3& c,
                                    const Vec3& desired_normal) {
            emit_oriented_triangle(a, b, c, desired_normal);
        }

        void add_oriented_polygon(const std::vector<Vec3>& poly, const Vec3& desired_normal) {
            emit_oriented_face(poly, desired_normal);
        }

        void add_oriented_quad(const Vec3& v0, const Vec3& v1, const Vec3& v2, const Vec3& v3,
                               const Vec3& desired_normal) {
            emit_oriented_quad(v0, v1, v2, v3, desired_normal);
        }

        /// Rebuild derived triangle buffer from faces (fan triangulation).
        void rebuild_triangles_from_faces() {
            triangles.clear();
            face_origins.clear();
            for (const auto& face : faces) {
                if (face.verts.size() < 3) continue;
                const int i0 = face.verts[0];
                for (size_t i = 1; i + 1 < face.verts.size(); ++i) {
                    triangles.push_back(i0);
                    triangles.push_back(face.verts[i]);
                    triangles.push_back(face.verts[i + 1]);
                    face_origins.push_back(face.origin);
                }
            }
        }

        /// Fix opposite-winding manifold edges by BFS on polygon faces.
        void fix_winding() {
            if (faces.empty()) return;

            auto edge_key_fn = [](int a, int b) -> uint64_t {
                const int lo = std::min(a, b);
                const int hi = std::max(a, b);
                return (static_cast<uint64_t>(static_cast<uint32_t>(lo)) << 32) |
                       static_cast<uint32_t>(hi);
            };

            std::unordered_map<uint64_t, std::vector<std::pair<int, bool>>> edge_owners;
            for (int fi = 0; fi < static_cast<int>(faces.size()); ++fi) {
                const auto& verts = faces[fi].verts;
                for (size_t e = 0; e < verts.size(); ++e) {
                    const int a = verts[e];
                    const int b = verts[(e + 1) % verts.size()];
                    edge_owners[edge_key_fn(a, b)].push_back({fi, a < b});
                }
            }

            std::vector<bool> visited(faces.size(), false);
            std::vector<bool> need_flip(faces.size(), false);

            for (size_t seed = 0; seed < faces.size(); ++seed) {
                if (visited[seed]) continue;
                visited[seed] = true;
                need_flip[seed] = false;
                std::vector<int> queue = {static_cast<int>(seed)};
                size_t head = 0;
                while (head < queue.size()) {
                    int cur = queue[head++];
                    const auto& verts = faces[cur].verts;
                    for (size_t e = 0; e < verts.size(); ++e) {
                        const int a = verts[e];
                        const int b = verts[(e + 1) % verts.size()];
                        const uint64_t key = edge_key_fn(a, b);
                        auto it = edge_owners.find(key);
                        if (it == edge_owners.end()) continue;
                        for (const auto& [neighbor, neighbor_dir] : it->second) {
                            if (neighbor == cur || visited[neighbor]) continue;
                            visited[neighbor] = true;
                            const bool cur_dir = (a < b);
                            const bool cur_effective_dir = cur_dir ^ need_flip[cur];
                            need_flip[neighbor] = (neighbor_dir == cur_effective_dir);
                            queue.push_back(neighbor);
                        }
                    }
                }
            }

            for (size_t i = 0; i < faces.size(); ++i) {
                if (need_flip[i])
                    std::reverse(faces[i].verts.begin(), faces[i].verts.end());
            }

            // Check signed volume to ensure overall outward orientation.
            double vol = 0.0;
            for (const auto& face : faces) {
                if (face.verts.size() < 3) continue;
                const auto& a = vertices[face.verts[0]];
                for (size_t i = 1; i + 1 < face.verts.size(); ++i) {
                    const auto& b = vertices[face.verts[i]];
                    const auto& c = vertices[face.verts[i + 1]];
                    vol += a.x * (b.y * c.z - b.z * c.y) +
                           a.y * (b.z * c.x - b.x * c.z) +
                           a.z * (b.x * c.y - b.y * c.x);
                }
            }
            vol /= 6.0;
            if (vol < 0.0) {
                for (auto& face : faces)
                    std::reverse(face.verts.begin(), face.verts.end());
            }
        }
    } output;
    bool (*is_cancel_requested)() = nullptr;
};

// ── Cap Extents (shared by production and diagnostic code) ──────────────────

struct CapExtents {
    double x0 = 0.0;
    double x1 = 0.0;
};

CapExtents mesh_cap_extents(const std::vector<Vec3>& positions);
bool on_cap_plane_x(const Vec3& p, const CapExtents& cap, double tol);

// ── Public API ──────────────────────────────────────────────────────────────

/// Limit Method — Blender Bevel subset (None / Angle). Weight / Vertex Group out of scope.
enum class BevelLimitMethod {
    None,
    Angle,
};

/// Main bevel function — completely aligned with Blender's BM_mesh_bevel pipeline.
struct BevelEdgeSelection {
    std::string edge_group;
    bool exclude_unshared = true;
    std::vector<std::string> exclude_groups;
    /// When false, empty group → Angle and non-empty group → None (legacy graph behavior).
    bool limit_method_explicit = false;
    BevelLimitMethod limit_method = BevelLimitMethod::Angle;
};

data::PcgMeshData bevel_mesh_blender(
    const data::PcgMeshData& mesh,
    double amount,
    int segments,
    BevelOffsetType offset_type = BevelOffsetType::Offset,
    bool clamp_overlap = true,
    double angle_limit_deg = 30.0,
    float profile = 0.5f,
    BevelMiter miter_outer = BevelMiter::Sharp,
    BevelMiter miter_inner = BevelMiter::Sharp,
    BevelVMeshMethod vmesh_method = BevelVMeshMethod::Adj,
    bool (*is_cancel_requested)() = nullptr,
    const BevelEdgeSelection& edge_selection = {},
    const data::PcgGeometry* geometry = nullptr,
    data::PcgGeometry* out_geometry = nullptr);

} // namespace pcg::internal::elements::bevel
