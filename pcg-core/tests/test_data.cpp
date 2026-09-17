#include "data/pcg_data_collection.hpp"
#include "data/pcg_metadata.hpp"
#include "data/pcg_mesh_binary.hpp"
#include "data/pcg_mesh_data.hpp"
#include "data/pcg_param_data.hpp"
#include "data/pcg_point_data.hpp"
#include "data/pcg_spline_data.hpp"
#include "cook_diagnostics.hpp"
#include "elements/pcg_element.hpp"
#include "scripting/operation_bridge.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace pcg::internal::data;

namespace {

bool cancel_immediately()
{
    return true;
}

int cancellation_checks = 0;

bool cancel_after_first_check()
{
    ++cancellation_checks;
    return cancellation_checks >= 2;
}

} // namespace

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

    // The bridge executes the same registered native element as the visual path.
    // Use omitted/default parameters to catch drift in the normalization contract.
    pcg::internal::GraphNode visual_node;
    visual_node.id = "visual-box";
    visual_node.type = "CreateBoxMesh";
    visual_node.data = nlohmann::json::object();
    char visual_error[256] = {};
    pcg::internal::PcgContext visual_ctx;
    visual_ctx.node = &visual_node;
    visual_ctx.err_buf = visual_error;
    visual_ctx.err_buf_size = static_cast<int>(sizeof(visual_error));
    const auto* visual_element = pcg::internal::elements::find_element("CreateBoxMesh");
    assert(visual_element != nullptr);
    assert(visual_element->execute(visual_ctx) == PCG_OK);
    const auto* visual_geometry = visual_ctx.outputs.find_geometry("out");
    assert(visual_geometry != nullptr);

    pcg::internal::scripting::OperationInvocationRequest bridge_request;
    bridge_request.operation = "CreateBoxMesh";
    pcg::internal::scripting::OperationInvocationResult bridge_result;
    assert(pcg::internal::scripting::invoke_operation(bridge_request, bridge_result) == PCG_OK);
    const auto* bridge_geometry = bridge_result.outputs.find_geometry("out");
    assert(bridge_geometry != nullptr);
    assert(bridge_geometry->points().size() == visual_geometry->points().size());
    assert(bridge_geometry->faces().size() == visual_geometry->faces().size());
    for (size_t i = 0; i < visual_geometry->points().size(); ++i) {
        const auto& visual_point = visual_geometry->points()[i];
        const auto& bridge_point = bridge_geometry->points()[i];
        assert(bridge_point.x == visual_point.x);
        assert(bridge_point.y == visual_point.y);
        assert(bridge_point.z == visual_point.z);
    }
    for (size_t i = 0; i < visual_geometry->faces().size(); ++i)
        assert(bridge_geometry->faces()[i] == visual_geometry->faces()[i]);
    assert(bridge_result.diagnostic.is_null());
    assert(bridge_result.statistics["outcome"] == "success");
    std::printf("PASS: visual/script operation bridge default equivalence\n");

    // Fail-closed policy and validation errors are structured and publish no output.
    pcg::internal::scripting::OperationInvocationRequest denied_request;
    denied_request.operation = "Output";
    pcg::internal::scripting::OperationInvocationResult denied_result;
    assert(pcg::internal::scripting::invoke_operation(denied_request, denied_result) == PCG_ERR_INVALID_ARGUMENT);
    assert(denied_result.diagnostic["code"] == "operation_disallowed");
    assert(denied_result.outputs.items().empty());

    pcg::internal::scripting::OperationInvocationRequest unknown_request;
    unknown_request.operation = "DefinitelyUnknownOperation";
    pcg::internal::scripting::OperationInvocationResult unknown_result;
    assert(pcg::internal::scripting::invoke_operation(unknown_request, unknown_result) == PCG_ERR_UNKNOWN_NODE);
    assert(unknown_result.diagnostic["code"] == "unknown_operation");

    pcg::internal::scripting::OperationInvocationRequest invalid_request;
    invalid_request.operation = "CreateBoxMesh";
    invalid_request.parameters = {{"width", -1.0}};
    pcg::internal::scripting::OperationInvocationResult invalid_result;
    assert(pcg::internal::scripting::invoke_operation(invalid_request, invalid_result) == PCG_ERR_INVALID_ARGUMENT);
    assert(invalid_result.diagnostic["code"] == "invalid_parameters");
    assert(invalid_result.diagnostic["path"] == "parameters.width");

    // vector3 values must use the canonical 3-number representation and obey
    // manifest component constraints instead of falling back inside native code.
    pcg::internal::scripting::OperationInvocationRequest invalid_vector_request;
    invalid_vector_request.operation = "TransformMesh";
    invalid_vector_request.inputs.add_geometry("in", *bridge_geometry);
    invalid_vector_request.parameters = {{"translate", "bad"}};
    pcg::internal::scripting::OperationInvocationResult invalid_vector_result;
    assert(pcg::internal::scripting::invoke_operation(invalid_vector_request, invalid_vector_result) == PCG_ERR_INVALID_ARGUMENT);
    assert(invalid_vector_result.diagnostic["code"] == "invalid_parameters");
    assert(invalid_vector_result.diagnostic["path"] == "parameters.translate");

    invalid_vector_request.parameters = {{"scale", {-1.0, 1.0, 1.0}}};
    invalid_vector_result = {};
    assert(pcg::internal::scripting::invoke_operation(invalid_vector_request, invalid_vector_result) == PCG_ERR_INVALID_ARGUMENT);
    assert(invalid_vector_result.diagnostic["code"] == "invalid_parameters");
    assert(invalid_vector_result.diagnostic["path"] == "parameters.scale");
    std::printf("PASS: operation bridge vector3 parameter validation\n");

    // Reserved Core diagnostics are sidecars, not script-visible manifest pins.
    // They must survive successful native execution without invalidating outputs.
    pcg::internal::scripting::OperationInvocationRequest boolean_request;
    boolean_request.operation = "BooleanMesh";
    boolean_request.inputs.add_geometry("a", *bridge_geometry);
    boolean_request.inputs.add_geometry("b", PcgGeometry{});
    pcg::internal::scripting::OperationInvocationResult boolean_result;
    assert(pcg::internal::scripting::invoke_operation(boolean_request, boolean_result) == PCG_OK);
    assert(boolean_result.outputs.find_geometry("out") != nullptr);
    const auto* boolean_diagnostic =
        boolean_result.outputs.find_json(pcg::internal::kNodeDiagnosticTag);
    assert(boolean_diagnostic != nullptr);
    assert((*boolean_diagnostic)["outcome"] == "noop");
    assert(boolean_result.diagnostic.is_null());
    std::printf("PASS: operation bridge preserves native diagnostic sidecars\n");

    pcg::internal::scripting::OperationInvocationRequest cancelled_request;
    cancelled_request.operation = "CreateBoxMesh";
    cancelled_request.context.is_cancel_requested = &cancel_immediately;
    pcg::internal::scripting::OperationInvocationResult cancelled_result;
    assert(pcg::internal::scripting::invoke_operation(cancelled_request, cancelled_result) == PCG_ERR_EXECUTION);
    assert(cancelled_result.diagnostic["code"] == "cancelled");
    assert(cancelled_result.outputs.items().empty());

    // The first bridge cancellation check is false; BooleanMesh observes the
    // second check while executing. The bridge must keep the stable cancelled
    // diagnostic instead of converting the native PCG_ERR_EXECUTION to failure.
    cancellation_checks = 0;
    pcg::internal::scripting::OperationInvocationRequest mid_cancel_request;
    mid_cancel_request.operation = "BooleanMesh";
    mid_cancel_request.context.is_cancel_requested = &cancel_after_first_check;
    pcg::internal::scripting::OperationInvocationResult mid_cancel_result;
    assert(pcg::internal::scripting::invoke_operation(mid_cancel_request, mid_cancel_result) == PCG_ERR_EXECUTION);
    assert(mid_cancel_result.diagnostic["code"] == "cancelled");
    assert(mid_cancel_result.outputs.items().empty());
    std::printf("PASS: operation bridge structured diagnostics + cancellation\n");

    const auto discovery = pcg::internal::scripting::operation_discovery_json();
    assert(discovery["schemaVersion"] == 1);
    bool found_box = false;
    bool found_output = false;
    for (const auto& operation : discovery["operations"]) {
        if (operation["operation"] == "CreateBoxMesh") {
            found_box = true;
            assert(operation["scriptCallable"] == true);
            assert(operation.contains("parameters"));
            assert(operation.contains("inputs"));
            assert(operation.contains("outputs"));
        }
        if (operation["operation"] == "Output") {
            found_output = true;
            assert(operation["scriptCallable"] == false);
            assert(operation["capability"] == "graph-control");
        }
    }
    assert(found_box && found_output);
    const std::string declarations = pcg::internal::scripting::operation_declarations();
    assert(declarations.find("CreateBoxMesh") != std::string::npos);
    assert(declarations.find("\"Output\"") == std::string::npos);
    std::printf("PASS: operation bridge discovery + declarations\n");

    return 0;
}
