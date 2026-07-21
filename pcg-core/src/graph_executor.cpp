#include "graph_executor.hpp"

#include "cook_hash.hpp"
#include "data/pcg_context.hpp"
#include "data/pcg_geometry.hpp"
#include "elements/facade_foundation_algorithms.hpp"
#include "elements/pcg_element.hpp"
#include "internal/error_util.hpp"
#include "texture_runtime.hpp"
#include "heightfield_runtime.hpp"

#include <chrono>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <algorithm>

namespace pcg::internal {
namespace {

PcgResultCode fail(char* err_buf, int err_buf_size, PcgResultCode code, const char* message)
{
    write_error(err_buf, err_buf_size, message);
    return code;
}

/// Builds lightweight group statistics JSON for execution results.
/// Output format: {"groups": [{"name": "side", "domain": "face", "count": 24, "members": [0,1,2,...]}, ...]}
/// Face group members are remapped from geometry face indices to mesh triangle indices
/// (fan-triangulation: an N-gon face at tri offset T produces triangles T..T+N-3).
/// Face groups also emit facePolygons: packed rings [n, x,y,z * n, ...] so Scene View
/// can highlight intermediate-node groups without indexing the final MeshFilter.
nlohmann::json build_group_stats(const data::PcgGeometry& geometry)
{
    // Build face-index → first-mesh-triangle mapping
    std::vector<int> face_to_tri;
    face_to_tri.reserve(geometry.faces().size());
    int tri_offset = 0;
    for (const auto& face : geometry.faces()) {
        face_to_tri.push_back(tri_offset);
        if (face.size() >= 3)
            tri_offset += static_cast<int>(face.size()) - 2;
    }

    auto groups = nlohmann::json::array();
    static const char* kDomainNames[] = {"point", "edge", "face", "vertex"};
    const auto& points = geometry.points();
    for (int d = 0; d < 4; ++d) {
        const auto domain = static_cast<geometry::GroupDomain>(d);
        for (const auto& name : geometry.groups().group_names(domain)) {
            const auto& members = geometry.groups().members(domain, name);
            auto memberArray = nlohmann::json::array();
            auto edgeEndpoints = nlohmann::json::array();
            auto facePolygons = nlohmann::json::array();
            int face_count = 0;
            for (geometry::GroupId id : members) {
                if (d == static_cast<int>(geometry::GroupDomain::Face)) {
                    // Expand face index into constituent mesh triangles
                    if (id >= 0 && id < static_cast<geometry::GroupId>(face_to_tri.size())) {
                        const auto& face = geometry.faces()[static_cast<size_t>(id)];
                        const int first_tri = face_to_tri[static_cast<size_t>(id)];
                        const int tri_count = face.size() >= 3
                            ? static_cast<int>(face.size()) - 2 : 0;
                        for (int t = 0; t < tri_count; ++t)
                            memberArray.push_back(first_tri + t);

                        // Packed polygon ring for Scene View (independent of MeshFilter).
                        if (face.size() >= 3) {
                            bool ring_ok = true;
                            for (int pi : face) {
                                if (pi < 0 || pi >= static_cast<int>(points.size())) {
                                    ring_ok = false;
                                    break;
                                }
                            }
                            if (ring_ok) {
                                facePolygons.push_back(static_cast<int>(face.size()));
                                for (int pi : face) {
                                    const auto& p = points[static_cast<size_t>(pi)];
                                    facePolygons.push_back(p.x);
                                    facePolygons.push_back(p.y);
                                    facePolygons.push_back(p.z);
                                }
                                ++face_count;
                            }
                        }
                    }
                } else {
                    memberArray.push_back(id);
                }
                if (d == static_cast<int>(geometry::GroupDomain::Edge)) {
                    const auto endpoints = geometry::edge_group_points(id);
                    const int a = endpoints[0];
                    const int b = endpoints[1];
                    if (a >= 0 && a < static_cast<int>(points.size()) &&
                        b >= 0 && b < static_cast<int>(points.size())) {
                        const auto& pa = points[static_cast<size_t>(a)];
                        const auto& pb = points[static_cast<size_t>(b)];
                        edgeEndpoints.push_back(pa.x);
                        edgeEndpoints.push_back(pa.y);
                        edgeEndpoints.push_back(pa.z);
                        edgeEndpoints.push_back(pb.x);
                        edgeEndpoints.push_back(pb.y);
                        edgeEndpoints.push_back(pb.z);
                    }
                }
            }
            auto entry = nlohmann::json::object({
                {"name", name},
                {"domain", kDomainNames[d]},
                {"count", d == static_cast<int>(geometry::GroupDomain::Face)
                    ? face_count
                    : static_cast<int>(memberArray.size())},
                {"members", std::move(memberArray)},
            });
            if (d == static_cast<int>(geometry::GroupDomain::Edge))
                entry["edgeEndpoints"] = std::move(edgeEndpoints);
            if (d == static_cast<int>(geometry::GroupDomain::Face) && !facePolygons.empty())
                entry["facePolygons"] = std::move(facePolygons);
            groups.push_back(std::move(entry));
        }
    }
    auto obj = nlohmann::json::object();
    obj["groups"] = std::move(groups);
    return obj;
}

std::vector<std::string> topological_order(const Graph& graph,
                                           char* err_buf,
                                           int err_buf_size,
                                           PcgResultCode& code)
{
    std::unordered_map<std::string, int> indegree;
    std::unordered_map<std::string, std::vector<std::string>> adjacency;
    for (const auto& node : graph.nodes)
        indegree[node.id] = 0;

    for (const auto& edge : graph.edges) {
        adjacency[edge.source].push_back(edge.target);
        ++indegree[edge.target];
    }

    std::queue<std::string> ready;
    for (const auto& [node_id, degree] : indegree) {
        if (degree == 0)
            ready.push(node_id);
    }

    std::vector<std::string> order;
    order.reserve(graph.nodes.size());
    while (!ready.empty()) {
        const std::string current = ready.front();
        ready.pop();
        order.push_back(current);

        for (const auto& next : adjacency[current]) {
            if (--indegree[next] == 0)
                ready.push(next);
        }
    }

    if (order.size() != graph.nodes.size()) {
        code = fail(err_buf, err_buf_size, PCG_ERR_CYCLE_DETECTED, "Cycle detected during execution");
        return {};
    }

    code = PCG_OK;
    return order;
}

void gather_inputs(const std::vector<const GraphEdge*>& incoming_edges,
                   const NodeOutputMap& outputs,
                   data::PcgDataCollection& inputs,
                   char* err_buf,
                   int err_buf_size,
                   PcgResultCode& code)
{
    for (const GraphEdge* edge : incoming_edges) {
        const auto it = outputs.find(edge->source);
        if (it == outputs.end()) {
            code = fail(err_buf, err_buf_size, PCG_ERR_EXECUTION, "Missing upstream output");
            return;
        }

        const std::string pin = edge->target_handle.empty() ? "in" : edge->target_handle;
        const std::string source_pin = edge->source_handle.empty() ? "out" : edge->source_handle;
        const data::PcgDataCollection& upstream = it->second;

        if (auto points = upstream.find_points_shared(source_pin)) {
            inputs.add_points_shared(pin, points);
            if (auto spawn_mesh = upstream.find_mesh_shared("spawnMesh"))
                inputs.add_mesh_shared("spawnMesh", spawn_mesh);
            continue;
        }

        if (const data::PcgSplineData* splines = upstream.find_splines(source_pin)) {
            inputs.add_splines(pin, *splines);
            continue;
        }

        if (auto heightfield = upstream.find_heightfield_shared(source_pin)) {
            inputs.add_heightfield_shared(pin, heightfield);
            continue;
        }

        // Prefer Geometry over Mesh: mesh-first wrongly drops n-gon when both exist,
        // and mesh-only mid nodes (e.g. SubdivideMesh) must not hide upstream Geometry.
        if (auto geometry = upstream.find_geometry_shared(source_pin)) {
            inputs.add_geometry_shared(pin, geometry);
            continue;
        }

        if (auto mesh = upstream.find_mesh_shared(source_pin)) {
            inputs.add_mesh_shared(pin, mesh);
            continue;
        }

        if (const nlohmann::json* payload = upstream.find_json(source_pin)) {
            inputs.add(pin, data::PcgDataType::Unknown, *payload);
            if (auto spawn_mesh = upstream.find_mesh_shared("spawnMesh"))
                inputs.add_mesh_shared("spawnMesh", spawn_mesh);
            continue;
        }

        const nlohmann::json primary = upstream.primary_json();
        if (!primary.is_object() || primary.empty()) {
            if (auto heightfield = upstream.primary_heightfield_shared()) {
                inputs.add_heightfield_shared(pin, heightfield);
                continue;
            }
            if (auto geometry = upstream.primary_geometry_shared()) {
                inputs.add_geometry_shared(pin, geometry);
                continue;
            }
            if (auto mesh = upstream.primary_mesh_shared()) {
                inputs.add_mesh_shared(pin, mesh);
                continue;
            }
            code = fail(err_buf, err_buf_size, PCG_ERR_EXECUTION, "Missing upstream output");
            return;
        }

        inputs.add(pin, data::PcgDataType::Unknown, primary);
    }

    code = PCG_OK;
}

/// Collect per-node mesh statistics (Houdini-style geometry info).
/// Returns a JSON array: [{"node_id", "node_type", "point_count", "face_count", "triangle_count"}, ...]
nlohmann::json build_node_stats(
    const NodeOutputMap& outputs,
    const std::unordered_map<std::string, const GraphNode*>& node_by_id)
{
    auto stats = nlohmann::json::array();
    for (const auto& [node_id, collection] : outputs) {
        const auto it = node_by_id.find(node_id);
        const std::string node_type = it != node_by_id.end() ? it->second->type : "";

        int point_count = 0;
        int face_count = 0;
        int triangle_count = 0;

        if (const auto* geom = collection.primary_geometry()) {
            point_count = static_cast<int>(geom->points().size());
            face_count = static_cast<int>(geom->faces().size());
            for (const auto& face : geom->faces()) {
                if (face.size() >= 3)
                    triangle_count += static_cast<int>(face.size()) - 2;
            }
        } else if (const auto* mesh = collection.primary_mesh()) {
            point_count = static_cast<int>(mesh->vertices().size());
            triangle_count = static_cast<int>(mesh->triangles().size()) / 3;
        } else if (const auto* heightfield = collection.primary_heightfield()) {
            point_count = static_cast<int>(heightfield->sample_count());
        } else if (const auto pts = collection.find_points_shared("out")) {
            point_count = static_cast<int>(pts->points().size());
        } else {
            for (const auto& item : collection.items()) {
                if (item.points) {
                    point_count = static_cast<int>(item.points->points().size());
                    break;
                }
            }
        }

        stats.push_back({
            {"node_id", node_id},
            {"node_type", node_type},
            {"point_count", point_count},
            {"face_count", face_count},
            {"triangle_count", triangle_count},
        });
    }
    return stats;
}

/// Builds a flat array of per-node group stats: [{"node_id", "name", "domain", "count", "members", "edgeEndpoints"}, ...]
/// Flattened (not nested) so Unity's JsonUtility can deserialize it.
nlohmann::json build_per_node_groups(const NodeOutputMap& outputs)
{
    auto result = nlohmann::json::array();
    for (const auto& [node_id, collection] : outputs) {
        if (const auto* geom = collection.primary_geometry()) {
            auto groups_json = build_group_stats(*geom);
            if (groups_json.contains("groups") && groups_json["groups"].is_array()) {
                for (auto& g : groups_json["groups"]) {
                    g["node_id"] = node_id;
                    result.push_back(g);
                }
            }
        }
    }
    return result;
}

nlohmann::json build_heightfield_summary(const data::PcgHeightField& heightfield)
{
    auto layer_names = nlohmann::json::array();
    for (const auto& [name, layer] : heightfield.layers()) {
        layer_names.push_back({
            {"name", name},
            {"tupleSize", layer.tuple_size},
        });
    }

    return nlohmann::json{
        {"kind", "heightfield"},
        {"resolutionX", heightfield.resolution_x()},
        {"resolutionZ", heightfield.resolution_z()},
        {"sizeX", heightfield.size_x()},
        {"sizeZ", heightfield.size_z()},
        {"sampling", heightfield.sampling() == data::HeightFieldSampling::Corner
            ? "corner" : "center"},
        {"layers", std::move(layer_names)},
    };
}

struct ForEachRegion {
    std::string begin_id;
    std::string end_id;
    std::vector<std::string> body_order;
    std::unordered_set<std::string> body_nodes;
};

std::vector<std::string> reachable_from(
    const std::string& start,
    const std::unordered_map<std::string, std::vector<std::string>>& adjacency)
{
    std::vector<std::string> result;
    std::unordered_set<std::string> visited;
    std::queue<std::string> queue;
    queue.push(start);
    visited.insert(start);
    while (!queue.empty()) {
        const std::string current = queue.front();
        queue.pop();
        result.push_back(current);
        const auto it = adjacency.find(current);
        if (it == adjacency.end())
            continue;
        for (const auto& next : it->second) {
            if (visited.insert(next).second)
                queue.push(next);
        }
    }
    return result;
}

bool discover_foreach_regions(
    const Graph& graph,
    const std::vector<std::string>& topo_order,
    std::vector<ForEachRegion>& regions,
    char* err_buf,
    int err_buf_size,
    PcgResultCode& code)
{
    std::unordered_map<std::string, const GraphNode*> node_by_id;
    std::unordered_map<std::string, std::vector<std::string>> adjacency;
    std::unordered_map<std::string, std::vector<std::string>> reverse_adjacency;
    for (const auto& node : graph.nodes)
        node_by_id[node.id] = &node;
    for (const auto& edge : graph.edges) {
        adjacency[edge.source].push_back(edge.target);
        reverse_adjacency[edge.target].push_back(edge.source);
    }

    std::unordered_set<std::string> claimed_ends;
    for (const auto& node : graph.nodes) {
        if (node.type != "ForEachBegin")
            continue;

        std::vector<std::string> end_candidates;
        for (const auto& reached : reachable_from(node.id, adjacency)) {
            if (reached == node.id)
                continue;
            const auto it = node_by_id.find(reached);
            if (it != node_by_id.end() && it->second->type == "ForEachEnd")
                end_candidates.push_back(reached);
        }
        if (end_candidates.empty()) {
            code = fail(err_buf, err_buf_size, PCG_ERR_EXECUTION,
                        "ForEachBegin has no reachable ForEachEnd");
            return false;
        }
        // Prefer the nearest End (fewest hops among topo-later ends).
        std::string end_id;
        size_t best_index = topo_order.size();
        for (const auto& candidate : end_candidates) {
            if (claimed_ends.count(candidate) > 0)
                continue;
            const auto pos = std::find(topo_order.begin(), topo_order.end(), candidate);
            if (pos == topo_order.end())
                continue;
            const size_t index = static_cast<size_t>(pos - topo_order.begin());
            if (index < best_index) {
                best_index = index;
                end_id = candidate;
            }
        }
        if (end_id.empty()) {
            code = fail(err_buf, err_buf_size, PCG_ERR_EXECUTION,
                        "ForEachBegin could not pair a unique ForEachEnd");
            return false;
        }
        claimed_ends.insert(end_id);

        const auto from_begin = reachable_from(node.id, adjacency);
        const auto from_end_rev = reachable_from(end_id, reverse_adjacency);
        std::unordered_set<std::string> can_reach_end(from_end_rev.begin(), from_end_rev.end());

        ForEachRegion region;
        region.begin_id = node.id;
        region.end_id = end_id;
        for (const auto& id : topo_order) {
            if (id == node.id || id == end_id)
                continue;
            const bool from_begin_hit =
                std::find(from_begin.begin(), from_begin.end(), id) != from_begin.end();
            if (from_begin_hit && can_reach_end.count(id) > 0) {
                region.body_nodes.insert(id);
                region.body_order.push_back(id);
            }
        }
        regions.push_back(std::move(region));
    }

    code = PCG_OK;
    return true;
}

PcgResultCode cook_single_node(
    const Graph& graph,
    const GraphNode& node,
    int seed,
    const std::vector<const GraphEdge*>& incoming_edges,
    NodeOutputMap& outputs,
    std::unordered_map<std::string, uint64_t>& output_hashes,
    const TextureRuntime* textures,
    const MeshRuntime* meshes,
    const SplineRuntime* splines,
    const HeightFieldRuntime* heightfields,
    GraphCookCache* cache,
    bool (*is_cancel_requested)(),
    GraphPerfReport* perf,
    char* err_buf,
    int err_buf_size,
    bool allow_cache)
{
    if (is_cancel_requested && is_cancel_requested())
        return fail(err_buf, err_buf_size, PCG_ERR_EXECUTION, "Execution cancelled");

    std::vector<std::pair<std::string, uint64_t>> upstream_hashes;
    upstream_hashes.reserve(incoming_edges.size());
    for (const GraphEdge* edge : incoming_edges) {
        const auto it = output_hashes.find(edge->source);
        if (it == output_hashes.end())
            continue;
        const std::string pin = edge->target_handle.empty() ? "in" : edge->target_handle;
        std::string connection_key;
        connection_key.reserve(pin.size() + edge->source.size() + edge->source_handle.size() + 2);
        connection_key.append(pin);
        connection_key.push_back('\0');
        connection_key.append(edge->source);
        connection_key.push_back('\0');
        connection_key.append(edge->source_handle);
        upstream_hashes.emplace_back(std::move(connection_key), it->second);
    }
    std::sort(upstream_hashes.begin(), upstream_hashes.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    uint64_t input_hash = 0;
    if (cache && allow_cache)
        input_hash = compute_node_input_hash(
            node, seed, upstream_hashes, textures, meshes, splines, heightfields);

    if (cache && allow_cache) {
        data::PcgDataCollection cached_outputs;
        uint64_t cached_output_hash = 0;
        if (cache->try_get(node.id, input_hash, cached_outputs, cached_output_hash)) {
            outputs[node.id] = std::move(cached_outputs);
            output_hashes[node.id] = cached_output_hash;
            if (perf)
                perf->add(node.id, node.type, 0.0, true);
            return PCG_OK;
        }
    }

    const auto node_start = std::chrono::steady_clock::now();
    const elements::IPcgElement* element = elements::find_element(node.type);
    if (!element)
        return fail(err_buf, err_buf_size, PCG_ERR_UNKNOWN_NODE, "Unknown node type");

    PcgContext ctx;
    ctx.graph_seed = seed;
    ctx.graph = &graph;
    ctx.node = &node;
    ctx.textures = textures;
    ctx.meshes = meshes;
    ctx.splines = splines;
    ctx.heightfields = heightfields;
    ctx.err_buf = err_buf;
    ctx.err_buf_size = err_buf_size;
    ctx.is_cancel_requested = is_cancel_requested;

    PcgResultCode input_code = PCG_OK;
    gather_inputs(incoming_edges, outputs, ctx.inputs, err_buf, err_buf_size, input_code);
    if (input_code != PCG_OK)
        return input_code;

    const PcgResultCode rc = element->execute(ctx);
    if (rc != PCG_OK)
        return rc;

    outputs[node.id] = std::move(ctx.outputs);
    const uint64_t out_hash = (cache && allow_cache)
        ? input_hash
        : compute_output_hash(outputs[node.id]);
    output_hashes[node.id] = out_hash;
    if (cache && allow_cache)
        cache->put(node.id, input_hash, out_hash, outputs[node.id]);
    if (perf) {
        const auto node_end = std::chrono::steady_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(node_end - node_start).count();
        perf->add(node.id, node.type, ms, false);
    }
    return PCG_OK;
}

PcgResultCode cook_foreach_region(
    const ForEachRegion& region,
    const Graph& graph,
    const std::unordered_map<std::string, const GraphNode*>& node_by_id,
    const std::unordered_map<std::string, std::vector<const GraphEdge*>>& incoming_by_node,
    int seed,
    NodeOutputMap& outputs,
    std::unordered_map<std::string, uint64_t>& output_hashes,
    const TextureRuntime* textures,
    const MeshRuntime* meshes,
    const SplineRuntime* splines,
    const HeightFieldRuntime* heightfields,
    bool (*is_cancel_requested)(),
    GraphPerfReport* perf,
    char* err_buf,
    int err_buf_size)
{
    static const std::vector<const GraphEdge*> kNoIncomingEdges;
    const GraphNode* begin_node = node_by_id.at(region.begin_id);
    const GraphNode* end_node = node_by_id.at(region.end_id);

    const auto begin_incoming_it = incoming_by_node.find(region.begin_id);
    const auto& begin_incoming = begin_incoming_it != incoming_by_node.end()
        ? begin_incoming_it->second : kNoIncomingEdges;

    data::PcgDataCollection begin_inputs;
    PcgResultCode input_code = PCG_OK;
    gather_inputs(begin_incoming, outputs, begin_inputs, err_buf, err_buf_size, input_code);
    if (input_code != PCG_OK)
        return input_code;

    const data::PcgGeometry* seed_geometry = begin_inputs.find_geometry("in");
    data::PcgGeometry seed_storage;
    if (!seed_geometry) {
        if (const data::PcgMeshData* mesh = begin_inputs.find_mesh("in")) {
            seed_storage = data::geometry_from_mesh(*mesh);
            seed_geometry = &seed_storage;
        }
    }
    if (!seed_geometry)
        return fail(err_buf, err_buf_size, PCG_ERR_EXECUTION, "ForEachBegin missing mesh input");

    const std::string method = begin_node->data.value("method", std::string("primitive"));
    const std::string piece_attribute =
        begin_node->data.value("pieceAttribute", std::string("piece"));
    const int iterations = std::max(1, begin_node->data.value("iterations", 1));
    const std::string gather_method =
        end_node->data.value("gatherMethod", std::string("merge"));

    auto cook_body_once = [&](const data::PcgGeometry& piece,
                              data::PcgGeometry& end_geometry) -> PcgResultCode {
        data::PcgDataCollection begin_out;
        begin_out.add_geometry("out", piece);
        outputs[region.begin_id] = std::move(begin_out);
        output_hashes[region.begin_id] = compute_output_hash(outputs[region.begin_id]);

        for (const auto& body_id : region.body_order) {
            const GraphNode* body_node = node_by_id.at(body_id);
            const auto incoming_it = incoming_by_node.find(body_id);
            const auto& incoming = incoming_it != incoming_by_node.end()
                ? incoming_it->second : kNoIncomingEdges;
            const PcgResultCode rc = cook_single_node(
                graph, *body_node, seed, incoming, outputs, output_hashes,
                textures, meshes, splines, heightfields, nullptr, is_cancel_requested,
                perf, err_buf, err_buf_size, false);
            if (rc != PCG_OK)
                return rc;
        }

        const auto end_incoming_it = incoming_by_node.find(region.end_id);
        const auto& end_incoming = end_incoming_it != incoming_by_node.end()
            ? end_incoming_it->second : kNoIncomingEdges;
        const PcgResultCode end_rc = cook_single_node(
            graph, *end_node, seed, end_incoming, outputs, output_hashes,
            textures, meshes, splines, heightfields, nullptr, is_cancel_requested,
            perf, err_buf, err_buf_size, false);
        if (end_rc != PCG_OK)
            return end_rc;

        const data::PcgGeometry* end_geo = outputs[region.end_id].find_geometry("out");
        if (!end_geo)
            end_geo = outputs[region.end_id].primary_geometry();
        if (!end_geo)
            return fail(err_buf, err_buf_size, PCG_ERR_EXECUTION,
                        "ForEachEnd produced no geometry");
        end_geometry = *end_geo;
        return PCG_OK;
    };

    data::PcgGeometry accumulated;
    if (method == "count") {
        data::PcgGeometry current = *seed_geometry;
        for (int i = 0; i < iterations; ++i) {
            data::PcgGeometry end_geometry;
            const PcgResultCode rc = cook_body_once(current, end_geometry);
            if (rc != PCG_OK)
                return rc;
            if (gather_method == "feedback") {
                current = end_geometry;
                accumulated = end_geometry;
            } else {
                accumulated = data::merge_geometries(accumulated, end_geometry);
                current = end_geometry;
            }
        }
    } else {
        const auto pieces = elements::foreach_pieces(*seed_geometry, method, piece_attribute);
        for (const auto& piece : pieces) {
            data::PcgGeometry end_geometry;
            const PcgResultCode rc = cook_body_once(piece, end_geometry);
            if (rc != PCG_OK)
                return rc;
            accumulated = data::merge_geometries(accumulated, end_geometry);
        }
    }

    data::PcgDataCollection end_out;
    end_out.add_geometry("out", std::move(accumulated));
    outputs[region.end_id] = std::move(end_out);
    output_hashes[region.end_id] = compute_output_hash(outputs[region.end_id]);
    return PCG_OK;
}

} // namespace

PcgResultCode execute_graph(const Graph& graph,
                            int seed,
                            GraphExecutionResult& out_result,
                            char* err_buf,
                            int err_buf_size,
                            const TextureRuntime* textures,
                            const MeshRuntime* meshes,
                            const SplineRuntime* splines,
                            const HeightFieldRuntime* heightfields,
                            GraphCookCache* cache,
                            bool (*is_cancel_requested)(),
                            GraphPerfReport* perf)
{
    elements::register_builtin_elements();
    if (perf)
        perf->clear();

    PcgResultCode topo_code = PCG_OK;
    const auto order = topological_order(graph, err_buf, err_buf_size, topo_code);
    if (topo_code != PCG_OK)
        return topo_code;

    if (cache) {
        const uint64_t structure_hash = compute_graph_structure_hash(graph);
        // Do not clear node entries when only the graph topology changes. Node
        // input hashes below include incoming connection identity, parameters,
        // seed, runtime bindings, and upstream output hashes. This makes entries
        // safe to reuse across Editor preview subgraphs, where changing the sink
        // would otherwise discard an expensive full-graph cook.
        cache->set_structure_hash(structure_hash);
        cache->reset_stats();
    }

    std::unordered_map<std::string, const GraphNode*> node_by_id;
    node_by_id.reserve(graph.nodes.size());
    for (const auto& node : graph.nodes)
        node_by_id[node.id] = &node;

    std::unordered_map<std::string, std::vector<const GraphEdge*>> incoming_by_node;
    incoming_by_node.reserve(graph.nodes.size());
    std::unordered_set<std::string> nodes_with_outgoing;
    nodes_with_outgoing.reserve(graph.nodes.size());
    for (const auto& edge : graph.edges) {
        incoming_by_node[edge.target].push_back(&edge);
        nodes_with_outgoing.insert(edge.source);
    }

    static const std::vector<const GraphEdge*> kNoIncomingEdges;

    std::vector<ForEachRegion> foreach_regions;
    PcgResultCode foreach_code = PCG_OK;
    if (!discover_foreach_regions(graph, order, foreach_regions, err_buf, err_buf_size, foreach_code))
        return foreach_code;

    std::unordered_map<std::string, const ForEachRegion*> foreach_by_begin;
    std::unordered_set<std::string> foreach_managed;
    for (const auto& region : foreach_regions) {
        foreach_by_begin[region.begin_id] = &region;
        foreach_managed.insert(region.begin_id);
        foreach_managed.insert(region.end_id);
        for (const auto& body_id : region.body_nodes)
            foreach_managed.insert(body_id);
    }

    std::unordered_map<std::string, uint64_t> output_hashes;
    NodeOutputMap outputs;
    for (const auto& node_id : order) {
        if (is_cancel_requested && is_cancel_requested())
            return fail(err_buf, err_buf_size, PCG_ERR_EXECUTION, "Execution cancelled");

        if (const auto begin_it = foreach_by_begin.find(node_id); begin_it != foreach_by_begin.end()) {
            const PcgResultCode rc = cook_foreach_region(
                *begin_it->second, graph, node_by_id, incoming_by_node, seed, outputs,
                output_hashes, textures, meshes, splines, heightfields, is_cancel_requested,
                perf, err_buf, err_buf_size);
            if (rc != PCG_OK)
                return rc;
            continue;
        }

        if (foreach_managed.count(node_id) > 0)
            continue;

        const GraphNode* node = node_by_id[node_id];
        const auto incoming_it = incoming_by_node.find(node_id);
        const auto& incoming_edges = incoming_it != incoming_by_node.end()
            ? incoming_it->second
            : kNoIncomingEdges;
        const PcgResultCode rc = cook_single_node(
            graph, *node, seed, incoming_edges, outputs, output_hashes, textures, meshes,
            splines, heightfields, cache, is_cancel_requested, perf, err_buf, err_buf_size,
            true);
        if (rc != PCG_OK)
            return rc;
    }

    auto node_stats = build_node_stats(outputs, node_by_id);
    auto per_node_groups = build_per_node_groups(outputs);

    const GraphNode* sink = nullptr;
    const GraphNode* fallback_sink = nullptr;
    for (const auto& node : graph.nodes) {
        const bool has_outgoing = nodes_with_outgoing.count(node.id) != 0;
        if (!has_outgoing) {
            if (node.type == "Output" && !sink)
                sink = &node;
            if (!fallback_sink)
                fallback_sink = &node;
        }
    }

    if (!sink)
        sink = fallback_sink;

    if (!sink)
        return fail(err_buf, err_buf_size, PCG_ERR_EXECUTION, "Graph has no sink node");

    const auto sink_it = outputs.find(sink->id);
    if (sink_it == outputs.end())
        return fail(err_buf, err_buf_size, PCG_ERR_EXECUTION, "Sink node produced no output");

    const data::PcgDataCollection& sink_output = sink_it->second;

    // Keep HeightField as a typed sidecar even when ConvertHeightField feeds the
    // Mesh Sink. This lets host terrain adapters update native Terrain/Landscape
    // while the existing mesh preview remains unchanged.
    auto find_nearest_heightfield = [&]() -> std::shared_ptr<const data::PcgHeightField> {
        std::queue<std::string> frontier;
        std::unordered_set<std::string> visited{sink->id};
        frontier.push(sink->id);
        while (!frontier.empty()) {
            const std::string current = frontier.front();
            frontier.pop();
            const auto current_output = outputs.find(current);
            if (current_output != outputs.end()) {
                if (auto heightfield = current_output->second.primary_heightfield_shared())
                    return heightfield;
            }
            const auto incoming_it = incoming_by_node.find(current);
            if (incoming_it == incoming_by_node.end())
                continue;
            for (const GraphEdge* edge : incoming_it->second) {
                if (visited.insert(edge->source).second)
                    frontier.push(edge->source);
            }
        }
        return nullptr;
    };
    out_result.source_heightfield = find_nearest_heightfield();
    if (const data::PcgMeshData* spawn_mesh = sink_output.find_mesh("spawnMesh")) {
        out_result.spawn_mesh = *spawn_mesh;
    } else {
        out_result.spawn_mesh = data::PcgMeshData{};
        for (const auto& [node_id, collection] : outputs) {
            (void)node_id;
            if (const data::PcgMeshData* upstream_spawn = collection.find_mesh("spawnMesh")) {
                out_result.spawn_mesh = *upstream_spawn;
                break;
            }
        }
    }

    if (const data::PcgTaggedData* out_item = sink_output.find("out"); out_item && out_item->points) {
        out_result.kind = GraphResultKind::Points;
        out_result.points = out_item->points;
        out_result.point_sidecar = out_item->payload;
        out_result.json = nlohmann::json::object();
        out_result.mesh = data::PcgMeshData{};
        out_result.json["node_stats"] = node_stats;
        out_result.json["node_groups"] = per_node_groups;
        return PCG_OK;
    }

    if (auto heightfield = sink_output.find_heightfield_shared("out")) {
        out_result.source_heightfield = heightfield;
        out_result.kind = GraphResultKind::Json;
        out_result.json = build_heightfield_summary(*heightfield);
        out_result.mesh = data::PcgMeshData{};
        out_result.json["node_stats"] = node_stats;
        out_result.json["node_groups"] = per_node_groups;
        return PCG_OK;
    }

    const nlohmann::json primary = sink_output.primary_json();
    if (primary.is_object() && primary.contains("points") && primary["points"].is_array()) {
        out_result.kind = GraphResultKind::Json;
        out_result.json = primary;
        out_result.mesh = data::PcgMeshData{};
        out_result.json["node_stats"] = node_stats;
        out_result.json["node_groups"] = per_node_groups;
        return PCG_OK;
    }

    if (auto geometry = sink_output.find_geometry_shared("out")) {
        out_result.kind = GraphResultKind::Mesh;
        out_result.source_geometry = geometry;
        const auto& d = geometry->detail();
        out_result.mesh = data::compute_split_normals(*geometry,
            data::NormalComputeOptions{d.shade_mode, d.cusp_angle_deg, true});
        out_result.json = build_group_stats(*geometry);
        out_result.json["node_stats"] = node_stats;
        out_result.json["node_groups"] = per_node_groups;
        out_result.json["geometry_export"] = "sink_geometry";
        if (!out_result.mesh.metadata().raw().empty())
            out_result.json["mesh_metadata"] = out_result.mesh.metadata().raw();
        return PCG_OK;
    }

    if (auto geometry = sink_output.primary_geometry_shared()) {
        out_result.kind = GraphResultKind::Mesh;
        out_result.source_geometry = geometry;
        const auto& d = geometry->detail();
        out_result.mesh = data::compute_split_normals(*geometry,
            data::NormalComputeOptions{d.shade_mode, d.cusp_angle_deg, true});
        out_result.json = build_group_stats(*geometry);
        out_result.json["node_stats"] = node_stats;
        out_result.json["node_groups"] = per_node_groups;
        out_result.json["geometry_export"] = "sink_geometry";
        if (!out_result.mesh.metadata().raw().empty())
            out_result.json["mesh_metadata"] = out_result.mesh.metadata().raw();
        return PCG_OK;
    }

    auto try_salvage_upstream_geometry = [&]() {
        // BFS from sink along reverse edges; prefer nearest Geometry for Scene wire.
        std::queue<std::string> frontier;
        std::unordered_set<std::string> visited{sink->id};
        frontier.push(sink->id);
        while (!frontier.empty()) {
            const std::string current = frontier.front();
            frontier.pop();
            const auto incoming_it = incoming_by_node.find(current);
            if (incoming_it == incoming_by_node.end())
                continue;
            for (const GraphEdge* edge : incoming_it->second) {
                if (visited.count(edge->source))
                    continue;
                visited.insert(edge->source);
                const auto up = outputs.find(edge->source);
                if (up == outputs.end())
                    continue;
                if (auto geometry = up->second.primary_geometry_shared()) {
                    out_result.source_geometry = geometry;
                    out_result.json["geometry_export"] = "salvaged_upstream";
                    out_result.json["geometry_export_from"] = edge->source;
                    return;
                }
                frontier.push(edge->source);
            }
        }
        out_result.json["geometry_export"] = "mesh_only";
    };

    if (const data::PcgMeshData* mesh = sink_output.find_mesh("out")) {
        out_result.kind = GraphResultKind::Mesh;
        out_result.mesh = *mesh;
        out_result.json = nlohmann::json::object();
        out_result.json["node_stats"] = node_stats;
        out_result.json["node_groups"] = per_node_groups;
        if (!mesh->metadata().raw().empty())
            out_result.json["mesh_metadata"] = mesh->metadata().raw();
        try_salvage_upstream_geometry();
        return PCG_OK;
    }

    if (const data::PcgMeshData* mesh = sink_output.primary_mesh()) {
        out_result.kind = GraphResultKind::Mesh;
        out_result.mesh = *mesh;
        out_result.json = nlohmann::json::object();
        out_result.json["node_stats"] = node_stats;
        out_result.json["node_groups"] = per_node_groups;
        if (!mesh->metadata().raw().empty())
            out_result.json["mesh_metadata"] = mesh->metadata().raw();
        try_salvage_upstream_geometry();
        return PCG_OK;
    }

    out_result.kind = GraphResultKind::Json;
    out_result.json = sink_output.primary_json();
    out_result.mesh = data::PcgMeshData{};
    out_result.json["node_stats"] = node_stats;
    out_result.json["node_groups"] = per_node_groups;
    return PCG_OK;
}

} // namespace pcg::internal
