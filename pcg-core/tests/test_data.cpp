#include "data/pcg_data_collection.hpp"
#include "data/pcg_metadata.hpp"
#include "data/pcg_mesh_binary.hpp"
#include "data/pcg_mesh_data.hpp"
#include "data/pcg_param_data.hpp"
#include "data/pcg_point_data.hpp"
#include "data/pcg_spline_data.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>

using namespace pcg::internal::data;

int main()
{
    PcgMetadata meta;
    meta.set("prefab", "Tree");
    meta.set("scale", 2.0);
    assert(meta.has("prefab"));
    assert(meta.get("prefab") == "Tree");

    PcgPointData points;
    points.add_point({1.0, 0.0, 2.0});
    points.metadata().set("tag", "a");
    const nlohmann::json point_json = points.to_json();
    assert(point_json["points"].size() == 1);
    assert(point_json["points"][0]["x"] == 1.0);

    const PcgPointData round_trip = PcgPointData::from_json(point_json);
    assert(round_trip.points().size() == 1);
    assert(round_trip.points()[0].z == 2.0);
    std::printf("PASS: PcgPointData round-trip\n");

    PcgDataCollection collection;
    collection.add_param("config", PcgParamData(nlohmann::json{{"seed", 7}, {"density", 1.0}}));
    assert(collection.find_json("config") != nullptr);
    assert((*collection.find_json("config"))["seed"] == 7);

    collection.add_points("points", round_trip);
    assert(collection.primary_json().contains("points"));
    std::printf("PASS: PcgDataCollection param + points\n");

    PcgSplineData splines;
    PcgSpline segment;
    segment.points.push_back({0.0, 0.0, 0.0});
    segment.points.push_back({1.0, 0.0, 1.0});
    splines.add_spline(segment);
    const nlohmann::json spline_json = splines.to_json();
    assert(spline_json["splines"].size() == 1);
    assert(spline_json["splines"][0]["points"].size() == 2);

    const PcgSplineData spline_round_trip = PcgSplineData::from_json(spline_json);
    assert(spline_round_trip.splines().size() == 1);
    assert(spline_round_trip.splines()[0].points.size() == 2);
    std::printf("PASS: PcgSplineData round-trip\n");

    PcgMeshData box_mesh;
    box_mesh.add_vertex({0.0, 0.0, 0.0});
    box_mesh.add_vertex({1.0, 0.0, 0.0});
    box_mesh.add_vertex({0.0, 1.0, 0.0});
    box_mesh.add_triangle(0, 1, 2);

    std::vector<uint8_t> mesh_buf(static_cast<size_t>(mesh_binary_size(box_mesh)));
    assert(write_mesh_binary(box_mesh, mesh_buf.data(), static_cast<int>(mesh_buf.size())));

    PcgMeshData mesh_round_trip;
    assert(read_mesh_binary(mesh_buf.data(), static_cast<int>(mesh_buf.size()), mesh_round_trip));
    assert(mesh_round_trip.vertices().size() == 3);
    assert(mesh_round_trip.triangles().size() == 3);

    PcgDataCollection mesh_collection;
    mesh_collection.add_mesh("out", box_mesh);
    assert(mesh_collection.find_mesh("out") != nullptr);
    assert(mesh_collection.find_json("out") == nullptr);
    assert(mesh_collection.primary_mesh() != nullptr);
    std::printf("PASS: PcgMeshData binary + typed collection\n");

    return 0;
}
