#include "data/pcg_data_collection.hpp"
#include "data/pcg_metadata.hpp"
#include "data/pcg_param_data.hpp"
#include "data/pcg_point_data.hpp"

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

    return 0;
}
