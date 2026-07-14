#include "data/pcg_data_collection.hpp"
#include "data/pcg_geometry.hpp"
#include "data/pcg_geometry_binary.hpp"
#include "data/pcg_mesh_data.hpp"
#include "elements/geometry_algorithms.hpp"
#include "elements/mesh_algorithms.hpp"
#include "geometry/bmesh.hpp"
#include "geometry/group_table.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

using namespace pcg::internal::data;
using namespace pcg::internal::elements;
using namespace pcg::internal::geometry;

namespace {

[[noreturn]] void fail(const char* msg)
{
    std::printf("FAIL: %s\n", msg);
    std::exit(1);
}

// Centroid-vs-normal heuristic is unreliable on concave bevel fillets; keep it
// only as a soft signal. Manifold opposite-winding is the hard invariant.
double outward_flip_ratio(const PcgMeshData& mesh)
{
    if (mesh.vertices().empty() || mesh.triangles().size() < 3)
        return 1.0;
    double cx = 0, cy = 0, cz = 0;
    for (const auto& v : mesh.vertices()) {
        cx += v.x; cy += v.y; cz += v.z;
    }
    const double inv = 1.0 / static_cast<double>(mesh.vertices().size());
    cx *= inv; cy *= inv; cz *= inv;
    int flipped = 0, total = 0;
    for (size_t i = 0; i + 2 < mesh.triangles().size(); i += 3) {
        const auto& a = mesh.vertices()[static_cast<size_t>(mesh.triangles()[i])];
        const auto& b = mesh.vertices()[static_cast<size_t>(mesh.triangles()[i + 1])];
        const auto& c = mesh.vertices()[static_cast<size_t>(mesh.triangles()[i + 2])];
        const double nx = (b.y - a.y) * (c.z - a.z) - (b.z - a.z) * (c.y - a.y);
        const double ny = (b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z);
        const double nz = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        const double mx = (a.x + b.x + c.x) / 3.0 - cx;
        const double my = (a.y + b.y + c.y) / 3.0 - cy;
        const double mz = (a.z + b.z + c.z) / 3.0 - cz;
        if (nx * nx + ny * ny + nz * nz < 1e-20) continue;
        ++total;
        if (nx * mx + ny * my + nz * mz < 0.0) ++flipped;
    }
    if (total == 0) return 1.0;
    return static_cast<double>(flipped) / static_cast<double>(total);
}

int manifold_winding_bad_count(const PcgMeshData& mesh)
{
    struct DirCount { int ab = 0; int ba = 0; };
    std::unordered_map<uint64_t, DirCount> edges;
    auto edge_key_u64 = [](int a, int b) -> uint64_t {
        const int lo = std::min(a, b);
        const int hi = std::max(a, b);
        return (static_cast<uint64_t>(static_cast<uint32_t>(lo)) << 32) |
               static_cast<uint32_t>(hi);
    };
    for (size_t i = 0; i + 2 < mesh.triangles().size(); i += 3) {
        const int verts[3] = {mesh.triangles()[i], mesh.triangles()[i + 1], mesh.triangles()[i + 2]};
        for (int e = 0; e < 3; ++e) {
            const int a = verts[e];
            const int b = verts[(e + 1) % 3];
            DirCount& dc = edges[edge_key_u64(a, b)];
            if (a < b) ++dc.ab; else ++dc.ba;
        }
    }
    int bad = 0;
    for (const auto& entry : edges) {
        if (entry.second.ab + entry.second.ba != 2) continue;
        if (entry.second.ab != 1 || entry.second.ba != 1) ++bad;
    }
    return bad;
}

double signed_volume(const PcgMeshData& mesh)
{
    double vol = 0.0;
    for (size_t i = 0; i + 2 < mesh.triangles().size(); i += 3) {
        const auto& a = mesh.vertices()[static_cast<size_t>(mesh.triangles()[i])];
        const auto& b = mesh.vertices()[static_cast<size_t>(mesh.triangles()[i + 1])];
        const auto& c = mesh.vertices()[static_cast<size_t>(mesh.triangles()[i + 2])];
        vol += a.x * (b.y * c.z - b.z * c.y) +
               a.y * (b.z * c.x - b.x * c.z) +
               a.z * (b.x * c.y - b.y * c.x);
    }
    return vol / 6.0;
}

} // namespace

int main()
{
    GroupTable groups;
    groups.add(GroupDomain::Edge, "sharp", 1000001);
    groups.add(GroupDomain::Edge, "sharp", 1000002);
    groups.add(GroupDomain::Face, "cap_start", 0);
    if (!groups.contains(GroupDomain::Edge, "sharp", 1000001)) fail("group contains");
    if (groups.contains(GroupDomain::Edge, "sharp", 99)) fail("group false positive");

    groups.subtract_into(GroupDomain::Edge, "sharp", "sharp");
    groups.add(GroupDomain::Edge, "a", 1);
    groups.add(GroupDomain::Edge, "a", 2);
    groups.add(GroupDomain::Edge, "b", 2);
    groups.add(GroupDomain::Edge, "b", 3);
    const auto diff = groups.eval(GroupDomain::Edge, "a - b");
    if (diff.count(1) != 1 || diff.count(2) != 0) fail("group subset");
    std::printf("PASS: GroupTable subset ops\n");

    PcgGeometry box;
    box.points_mut() = {
        {0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0},
        {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1},
    };
    // Outward winding (bottom CW from +Z → normal -Z).
    box.faces_mut() = {
        {0, 3, 2, 1},
        {4, 5, 6, 7},
        {0, 1, 5, 4},
        {1, 2, 6, 5},
        {2, 3, 7, 6},
        {3, 0, 4, 7},
    };
    box.groups().add(GroupDomain::Face, "side", 2);
    box.groups().add(GroupDomain::Edge, "test_edge", static_cast<int>(edge_key(0, 1)));

    const PcgMeshData tri = triangulate_geometry(box);
    const PcgGeometry round_trip = geometry_from_mesh(tri);
    if (round_trip.points().size() != box.points().size()) fail("round-trip points");
    if (round_trip.faces().empty()) fail("round-trip faces");
    std::printf("PASS: geometry mesh round-trip\n");

    const BMesh bmesh = bmesh_from_geometry(box);
    if (bmesh.faces.size() != box.faces().size()) fail("bmesh face count");
    if (bmesh.edges.find(edge_key(0, 1)) == bmesh.edges.end()) fail("bmesh missing edge");
    if (bmesh.edges.at(edge_key(0, 1)).groups.count("test_edge") != 1) fail("bmesh edge group");
    const PcgGeometry from_bmesh = geometry_from_bmesh(bmesh);
    if (!from_bmesh.groups().contains(GroupDomain::Edge, "test_edge", static_cast<int>(edge_key(0, 1))))
        fail("geometry_from_bmesh group");
    std::printf("PASS: bmesh_from_geometry preserves edge groups\n");

    PcgDataCollection collection;
    collection.add_geometry("out", box);
    if (!collection.find_geometry("out") || !collection.primary_geometry()) fail("collection");
    std::printf("PASS: PcgDataCollection geometry bus\n");

    GroupCreateOptions create_opts;
    create_opts.output_group = "hard_edges";
    create_opts.min_edge_angle_deg = 30.0;
    const PcgGeometry with_group = group_create(box, create_opts);
    if (with_group.groups().members(GroupDomain::Edge, "hard_edges").empty()) fail("GroupCreate");
    std::printf("PASS: GroupCreate angle selection\n");

    bevel::BevelEdgeSelection edge_sel;
    edge_sel.exclude_unshared = true;
    const PcgMeshData beveled =
        bevel_mesh(triangulate_geometry(box), 0.05, 2, BevelMethod::Edge, BevelOffsetType::Offset,
                   true, 30.0, 0.5f, BevelMiter::Sharp, BevelMiter::Sharp, BevelVMeshMethod::Adj,
                   nullptr, edge_sel, &box);
    if (beveled.vertices().empty()) fail("bevel empty");
    std::printf("PASS: Bevel excludeUnshared with geometry groups\n");

    PcgGeometry box_grouped = box;
    box_grouped.groups().add(GroupDomain::Edge, "bevel_edges", static_cast<int>(edge_key(0, 1)));
    box_grouped.groups().add(GroupDomain::Edge, "bevel_edges", static_cast<int>(edge_key(1, 2)));
    box_grouped.groups().add(GroupDomain::Edge, "bevel_edges", static_cast<int>(edge_key(2, 3)));
    box_grouped.groups().add(GroupDomain::Edge, "bevel_edges", static_cast<int>(edge_key(3, 0)));

    bevel::BevelEdgeSelection group_sel;
    group_sel.edge_group = "bevel_edges";
    group_sel.exclude_unshared = true;

    // Legacy path: non-empty group without limit_method_explicit → None (no angle filter).
    const PcgGeometry partial1_geo = bevel_geometry(
        box_grouped, 0.1, 1, BevelMethod::Edge, BevelOffsetType::Offset, true, 30.0, 0.5f,
        BevelMiter::Sharp, BevelMiter::Sharp, BevelVMeshMethod::Adj, nullptr, group_sel);
    const PcgMeshData partial1 = triangulate_geometry(partial1_geo);
    if (partial1.vertices().size() <= box.points().size()) fail("partial seg1 verts");
    const int bad1 = manifold_winding_bad_count(partial1);
    if (bad1 != 0) {
        std::printf("FAIL: partial seg1 manifold winding bad=%d\n", bad1);
        std::exit(1);
    }
    if (signed_volume(partial1) <= 0.0) fail("partial seg1 volume");

    const PcgGeometry partial_geo = bevel_geometry(
        box_grouped, 0.1, 3, BevelMethod::Edge, BevelOffsetType::Offset, true, 30.0, 0.5f,
        BevelMiter::Sharp, BevelMiter::Sharp, BevelVMeshMethod::Adj, nullptr, group_sel);
    const PcgMeshData partial = triangulate_geometry(partial_geo);
    if (partial.vertices().size() <= box.points().size()) fail("partial bevel verts");

    const int bad = manifold_winding_bad_count(partial);
    if (bad != 0) {
        std::printf("FAIL: Bevel BMesh-native manifold winding bad=%d\n", bad);
        std::exit(1);
    }
    if (signed_volume(partial) <= 0.0) {
        std::printf("FAIL: Bevel BMesh-native signed volume <= 0\n");
        std::exit(1);
    }

    // Unknown group must not fall back to beveling all edges.
    bevel::BevelEdgeSelection unknown_sel;
    unknown_sel.edge_group = "does_not_exist";
    unknown_sel.exclude_unshared = true;
    unknown_sel.limit_method_explicit = true;
    unknown_sel.limit_method = bevel::BevelLimitMethod::None;
    const PcgGeometry unknown_geo = bevel_geometry(
        box_grouped, 0.1, 2, BevelMethod::Edge, BevelOffsetType::Offset, true, 30.0, 0.5f,
        BevelMiter::Sharp, BevelMiter::Sharp, BevelVMeshMethod::Adj, nullptr, unknown_sel);
    if (unknown_geo.points().size() != box_grouped.points().size())
        fail("unknown edgeGroup must leave mesh unchanged (no bevel)");
    std::printf("PASS: unknown edgeGroup does not bevel all edges\n");

    // Explicit Angle on a named group: only sharp edges within the group.
    bevel::BevelEdgeSelection angle_group_sel = group_sel;
    angle_group_sel.limit_method_explicit = true;
    angle_group_sel.limit_method = bevel::BevelLimitMethod::Angle;
    const PcgGeometry angle_group_geo = bevel_geometry(
        box_grouped, 0.1, 2, BevelMethod::Edge, BevelOffsetType::Offset, true, 30.0, 0.5f,
        BevelMiter::Sharp, BevelMiter::Sharp, BevelVMeshMethod::Adj, nullptr, angle_group_sel);
    const PcgMeshData angle_group = triangulate_geometry(angle_group_geo);
    if (angle_group.vertices().size() <= box.points().size()) fail("angle+group verts");
    if (manifold_winding_bad_count(angle_group) != 0) fail("angle+group winding");
    if (signed_volume(angle_group) <= 0.0) fail("angle+group volume");
    std::printf("PASS: limitMethod=Angle with named group\n");
    // Full-edge bevel must stay clean under the centroid heuristic.
    bevel::BevelEdgeSelection all_sel;
    all_sel.exclude_unshared = true;
    const PcgGeometry full_geo = bevel_geometry(
        box, 0.1, 3, BevelMethod::Edge, BevelOffsetType::Offset, true, 30.0, 0.5f,
        BevelMiter::Sharp, BevelMiter::Sharp, BevelVMeshMethod::Adj, nullptr, all_sel);
    const PcgMeshData full = triangulate_geometry(full_geo);
    if (outward_flip_ratio(full) >= 0.05)
        fail("full-edge bevel normals");

    std::printf("PASS: Bevel BMesh-native box normals (partial_flip=%.3f, verts=%zu, vol=%.3f)\n",
                outward_flip_ratio(partial), partial.vertices().size(), signed_volume(partial));

    std::vector<uint8_t> geo_buf(static_cast<size_t>(geometry_binary_size(box)));
    if (!write_geometry_binary(box, geo_buf.data(), static_cast<int>(geo_buf.size())))
        fail("write geometry binary");
    PcgGeometry binary_round_trip;
    if (!read_geometry_binary(geo_buf.data(), static_cast<int>(geo_buf.size()), binary_round_trip))
        fail("read geometry binary");
    if (!binary_round_trip.groups().contains(GroupDomain::Edge, "test_edge",
                                             static_cast<int>(edge_key(0, 1))))
        fail("binary group");
    std::printf("PASS: geometry_binary v2 round-trip\n");
    return 0;
}
