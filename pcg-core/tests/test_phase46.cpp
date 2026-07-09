#include "data/pcg_data_collection.hpp"
#include "data/pcg_geometry.hpp"
#include "data/pcg_geometry_binary.hpp"
#include "data/pcg_mesh_data.hpp"
#include "elements/geometry_algorithms.hpp"
#include "elements/mesh_algorithms.hpp"
#include "geometry/bmesh.hpp"
#include "geometry/group_table.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>

using namespace pcg::internal::data;
using namespace pcg::internal::elements;
using namespace pcg::internal::geometry;

int main()
{
    GroupTable groups;
    groups.add(GroupDomain::Edge, "sharp", 1000001);
    groups.add(GroupDomain::Edge, "sharp", 1000002);
    groups.add(GroupDomain::Face, "cap_start", 0);

    assert(groups.contains(GroupDomain::Edge, "sharp", 1000001));
    assert(!groups.contains(GroupDomain::Edge, "sharp", 99));

    groups.subtract_into(GroupDomain::Edge, "sharp", "sharp");
    groups.add(GroupDomain::Edge, "a", 1);
    groups.add(GroupDomain::Edge, "a", 2);
    groups.add(GroupDomain::Edge, "b", 2);
    groups.add(GroupDomain::Edge, "b", 3);
    const auto diff = groups.eval(GroupDomain::Edge, "a - b");
    assert(diff.count(1) == 1);
    assert(diff.count(2) == 0);
    std::printf("PASS: GroupTable subset ops\n");

    PcgGeometry box;
    box.points_mut() = {
        {0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0},
        {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1},
    };
    box.faces_mut() = {
        {0, 1, 2, 3},
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
    assert(round_trip.points().size() == box.points().size());
    assert(round_trip.faces().size() >= 1);
    std::printf("PASS: geometry mesh round-trip\n");

    const BMesh bmesh = bmesh_from_geometry(box);
    assert(bmesh.faces.size() == box.faces().size());
    assert(bmesh.edges.find(edge_key(0, 1)) != bmesh.edges.end());
    assert(bmesh.edges.at(edge_key(0, 1)).groups.count("test_edge") == 1);
    const PcgGeometry from_bmesh = geometry_from_bmesh(bmesh);
    assert(from_bmesh.groups().contains(GroupDomain::Edge, "test_edge", static_cast<int>(edge_key(0, 1))));
    std::printf("PASS: bmesh_from_geometry preserves edge groups\n");

    PcgDataCollection collection;
    collection.add_geometry("out", box);
    assert(collection.find_geometry("out") != nullptr);
    assert(collection.primary_geometry() != nullptr);
    std::printf("PASS: PcgDataCollection geometry bus\n");

    GroupCreateOptions create_opts;
    create_opts.output_group = "hard_edges";
    create_opts.min_edge_angle_deg = 30.0;
    const PcgGeometry with_group = group_create(box, create_opts);
    assert(!with_group.groups().members(GroupDomain::Edge, "hard_edges").empty());
    std::printf("PASS: GroupCreate angle selection\n");

    bevel::BevelEdgeSelection edge_sel;
    edge_sel.exclude_unshared = true;
    const PcgMeshData beveled =
        bevel_mesh(triangulate_geometry(box), 0.05, 2, BevelMethod::Edge, BevelOffsetType::Offset,
                   true, 30.0, 0.5f, BevelMiter::Sharp, BevelMiter::Sharp, BevelVMeshMethod::Adj,
                   nullptr, edge_sel, &box);
    assert(!beveled.vertices().empty());
    std::printf("PASS: Bevel excludeUnshared with geometry groups\n");

    std::vector<uint8_t> geo_buf(static_cast<size_t>(geometry_binary_size(box)));
    assert(write_geometry_binary(box, geo_buf.data(), static_cast<int>(geo_buf.size())));
    PcgGeometry binary_round_trip;
    assert(read_geometry_binary(geo_buf.data(), static_cast<int>(geo_buf.size()), binary_round_trip));
    assert(binary_round_trip.groups().contains(GroupDomain::Edge, "test_edge",
                                               static_cast<int>(edge_key(0, 1))));
    std::printf("PASS: geometry_binary v2 round-trip\n");

    return 0;
}
