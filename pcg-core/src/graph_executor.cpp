#include "graph_executor.hpp"

#include "cook_hash.hpp"
#include "cook_diagnostics.hpp"
#include "data/pcg_context.hpp"
#include "data/pcg_geometry.hpp"
#include "elements/facade_foundation_algorithms.hpp"
#include "elements/topology_parity_algorithms.hpp"
#include "elements/pcg_element.hpp"
#include "internal/error_util.hpp"
#include "texture_runtime.hpp"
#include "heightfield_runtime.hpp"

#include <chrono>
#include <exception>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <algorithm>

namespace pcg::internal {
namespace {

// Group member geometry is an editor-only highlight sidecar. Expanding every
// polygon ring for a scanned/reconstructed hero mesh duplicates the binary
// geometry as hundreds of megabytes of decimal JSON (and repeats it per node).
// Preserve exact group counts for large groups, but keep interactive member
// details only while they remain practical to render in the editor.
constexpr size_t kMaxGroupHighlightMembers = 20000;

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
            const bool include_highlight_detail =
                members.size() <= kMaxGroupHighlightMembers;
            auto memberArray = nlohmann::json::array();
            auto edgeEndpoints = nlohmann::json::array();
            auto facePolygons = nlohmann::json::array();
            auto pointPositions = nlohmann::json::array();
            int face_count = 0;
            if (include_highlight_detail) for (geometry::GroupId id : members) {
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
                if (d == static_cast<int>(geometry::GroupDomain::Point)) {
                    // Packed XYZ for Scene View — do not index final MeshFilter / preview.
                    if (id >= 0 && id < static_cast<geometry::GroupId>(points.size())) {
                        const auto& p = points[static_cast<size_t>(id)];
                        pointPositions.push_back(p.x);
                        pointPositions.push_back(p.y);
                        pointPositions.push_back(p.z);
                    }
                } else if (d == static_cast<int>(geometry::GroupDomain::Vertex)) {
                    // Vertex IDs are global face-corner indices; map corner → point.
                    int corner = 0;
                    int point_index = -1;
                    for (const auto& face : geometry.faces()) {
                        const int face_corners = static_cast<int>(face.size());
                        if (id >= corner && id < corner + face_corners) {
                            point_index = face[static_cast<size_t>(id - corner)];
                            break;
                        }
                        corner += face_corners;
                    }
                    if (point_index >= 0 &&
                        point_index < static_cast<int>(points.size())) {
                        const auto& p = points[static_cast<size_t>(point_index)];
                        pointPositions.push_back(p.x);
                        pointPositions.push_back(p.y);
                        pointPositions.push_back(p.z);
                    }
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
                {"count", include_highlight_detail
                    ? (d == static_cast<int>(geometry::GroupDomain::Face)
                        ? face_count
                        : static_cast<int>(memberArray.size()))
                    : static_cast<int>(members.size())},
                {"members", std::move(memberArray)},
            });
            if (!include_highlight_detail) {
                entry["detailTruncated"] = true;
                entry["detailLimit"] = kMaxGroupHighlightMembers;
            }
            if (d == static_cast<int>(geometry::GroupDomain::Edge))
                entry["edgeEndpoints"] = std::move(edgeEndpoints);
            if (d == static_cast<int>(geometry::GroupDomain::Face) && !facePolygons.empty())
                entry["facePolygons"] = std::move(facePolygons);
            if ((d == static_cast<int>(geometry::GroupDomain::Point) ||
                 d == static_cast<int>(geometry::GroupDomain::Vertex)) &&
                !pointPositions.empty())
                entry["pointPositions"] = std::move(pointPositions);
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
            if (const data::PcgTaggedData* src_item = upstream.find(source_pin);
                src_item && src_item->points) {
                inputs.add_points_shared_with_meta(pin, points, src_item->payload);
            } else {
                inputs.add_points_shared(pin, points);
            }
            // Forward every spawnMesh sidecar (multi-prototype MergeSpawnPoints).
            for (const auto& item : upstream.items()) {
                if (item.tag == "spawnMesh" && item.mesh)
                    inputs.add_mesh_shared("spawnMesh", item.mesh);
            }
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

void accumulate_spline_stats(const data::PcgSplineData& splines,
                             int& point_count,
                             int& face_count,
                             int& vertex_count,
                             bool& has_bbox,
                             data::PcgVec3& bmin,
                             data::PcgVec3& bmax)
{
    face_count = static_cast<int>(splines.splines().size());
    vertex_count = 0;
    for (const auto& spline : splines.splines()) {
        vertex_count += static_cast<int>(spline.points.size());
        for (const auto& p : spline.points) {
            ++point_count;
            if (!has_bbox) {
                has_bbox = true;
                bmin = {p.x, p.y, p.z};
                bmax = bmin;
            } else {
                bmin.x = std::min(bmin.x, p.x);
                bmin.y = std::min(bmin.y, p.y);
                bmin.z = std::min(bmin.z, p.z);
                bmax.x = std::max(bmax.x, p.x);
                bmax.y = std::max(bmax.y, p.y);
                bmax.z = std::max(bmax.z, p.z);
            }
        }
    }
}

const data::PcgSplineData* find_primary_splines(const data::PcgDataCollection& collection)
{
    if (const data::PcgSplineData* splines = collection.find_splines("out"))
        return splines;
    for (const auto& item : collection.items()) {
        if (item.splines)
            return &*item.splines;
    }
    return nullptr;
}

/// Collect per-node mesh statistics (Houdini-style geometry info).
/// Returns a JSON array of node_stats entries with counts + bbox.
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
        int vertex_count = 0;
        int triangle_count = 0;
        bool has_bbox = false;
        data::PcgVec3 bmin{};
        data::PcgVec3 bmax{};

        if (const auto* geom = collection.primary_geometry()) {
            point_count = static_cast<int>(geom->points().size());
            face_count = static_cast<int>(geom->faces().size());
            vertex_count = geom->corner_count();
            for (const auto& face : geom->faces()) {
                if (face.size() >= 3)
                    triangle_count += static_cast<int>(face.size()) - 2;
            }
            if (!geom->points().empty()) {
                has_bbox = true;
                bmin = geom->points()[0];
                bmax = geom->points()[0];
                for (const auto& p : geom->points()) {
                    bmin.x = std::min(bmin.x, p.x);
                    bmin.y = std::min(bmin.y, p.y);
                    bmin.z = std::min(bmin.z, p.z);
                    bmax.x = std::max(bmax.x, p.x);
                    bmax.y = std::max(bmax.y, p.y);
                    bmax.z = std::max(bmax.z, p.z);
                }
            }
        } else if (const auto* mesh = collection.primary_mesh()) {
            point_count = static_cast<int>(mesh->vertices().size());
            vertex_count = point_count;
            triangle_count = static_cast<int>(mesh->triangles().size()) / 3;
            face_count = triangle_count;
            if (!mesh->vertices().empty()) {
                has_bbox = true;
                const auto& v0 = mesh->vertices()[0];
                bmin = {v0.x, v0.y, v0.z};
                bmax = bmin;
                for (const auto& v : mesh->vertices()) {
                    bmin.x = std::min(bmin.x, v.x);
                    bmin.y = std::min(bmin.y, v.y);
                    bmin.z = std::min(bmin.z, v.z);
                    bmax.x = std::max(bmax.x, v.x);
                    bmax.y = std::max(bmax.y, v.y);
                    bmax.z = std::max(bmax.z, v.z);
                }
            }
        } else if (const auto* heightfield = collection.primary_heightfield()) {
            point_count = static_cast<int>(heightfield->sample_count());
        } else if (const auto pts = collection.find_points_shared("out")) {
            point_count = static_cast<int>(pts->points().size());
            if (!pts->points().empty()) {
                has_bbox = true;
                const auto& p0 = pts->points()[0];
                bmin = {p0.x, p0.y, p0.z};
                bmax = bmin;
                for (const auto& p : pts->points()) {
                    bmin.x = std::min(bmin.x, p.x);
                    bmin.y = std::min(bmin.y, p.y);
                    bmin.z = std::min(bmin.z, p.z);
                    bmax.x = std::max(bmax.x, p.x);
                    bmax.y = std::max(bmax.y, p.y);
                    bmax.z = std::max(bmax.z, p.z);
                }
            }
        } else if (const data::PcgSplineData* splines = find_primary_splines(collection)) {
            accumulate_spline_stats(*splines, point_count, face_count, vertex_count,
                                    has_bbox, bmin, bmax);
        } else {
            for (const auto& item : collection.items()) {
                if (item.points) {
                    point_count = static_cast<int>(item.points->points().size());
                    break;
                }
            }
        }

        nlohmann::json entry = {
            {"node_id", node_id},
            {"node_type", node_type},
            {"point_count", point_count},
            {"face_count", face_count},
            {"vertex_count", vertex_count},
            {"triangle_count", triangle_count},
            {"has_bbox", has_bbox},
        };
        if (has_bbox) {
            entry["bbox_min_x"] = bmin.x;
            entry["bbox_min_y"] = bmin.y;
            entry["bbox_min_z"] = bmin.z;
            entry["bbox_max_x"] = bmax.x;
            entry["bbox_max_y"] = bmax.y;
            entry["bbox_max_z"] = bmax.z;
        }
        stats.push_back(std::move(entry));
    }
    return stats;
}

const char* attribute_type_name(data::AttributeType type)
{
    switch (type) {
    case data::AttributeType::Int:
        return "int";
    case data::AttributeType::String:
        return "string";
    case data::AttributeType::Float:
    default:
        return "float";
    }
}

const char* attribute_owner_name(data::AttributeOwner owner)
{
    switch (owner) {
    case data::AttributeOwner::Point:
        return "point";
    case data::AttributeOwner::Vertex:
        return "vertex";
    case data::AttributeOwner::Primitive:
        return "primitive";
    case data::AttributeOwner::Detail:
        return "detail";
    }
    return "point";
}

/// Flat attribute summaries for Unity JsonUtility:
/// [{"node_id","owner","name","type","tuple_size"}, ...]
nlohmann::json build_per_node_attributes(const NodeOutputMap& outputs)
{
    auto result = nlohmann::json::array();
    auto push_attr = [&](const std::string& node_id,
                         const char* owner,
                         const std::string& name,
                         const char* type,
                         int tuple_size) {
        result.push_back({
            {"node_id", node_id},
            {"owner", owner},
            {"name", name},
            {"type", type},
            {"tuple_size", tuple_size},
        });
    };

    for (const auto& [node_id, collection] : outputs) {
        if (const auto* geom = collection.primary_geometry()) {
            bool has_p = false;
            for (data::AttributeOwner owner :
                 {data::AttributeOwner::Point, data::AttributeOwner::Vertex,
                  data::AttributeOwner::Primitive, data::AttributeOwner::Detail}) {
                for (const auto& name : geom->attributes().names(owner)) {
                    const auto* attr = geom->attributes().find(owner, name);
                    if (!attr)
                        continue;
                    if (owner == data::AttributeOwner::Point && name == "P")
                        has_p = true;
                    push_attr(node_id, attribute_owner_name(owner), name,
                              attribute_type_name(attr->schema().type),
                              attr->schema().tuple_size);
                }
            }
            if (!geom->points().empty() && !has_p)
                push_attr(node_id, "point", "P", "float", 3);
        } else if (const data::PcgSplineData* splines = find_primary_splines(collection)) {
            int point_count = 0;
            for (const auto& spline : splines->splines())
                point_count += static_cast<int>(spline.points.size());
            if (point_count > 0)
                push_attr(node_id, "point", "P", "float", 3);

            for (const auto& [name, value] : splines->metadata().raw().items()) {
                const char* type = "float";
                int tuple_size = 1;
                if (value.is_array()) {
                    type = "float";
                    tuple_size = static_cast<int>(value.size());
                } else if (value.is_string()) {
                    type = "string";
                } else if (value.is_boolean() || value.is_number_integer()) {
                    type = "int";
                }
                push_attr(node_id, "detail", name, type, tuple_size);
            }

            if (!splines->splines().empty()) {
                // Attribute names may exist only on later splines (e.g. MeasureMesh
                // range groups). Sample the first spline that actually owns the key.
                std::unordered_map<std::string, nlohmann::json> spline_attr_samples;
                for (const auto& spline : splines->splines()) {
                    if (!spline.attributes.is_object())
                        continue;
                    for (const auto& [name, value] : spline.attributes.items()) {
                        if (spline_attr_samples.find(name) == spline_attr_samples.end())
                            spline_attr_samples.emplace(name, value);
                    }
                }
                for (const auto& [name, value] : spline_attr_samples) {
                    const char* type = "float";
                    int tuple_size = 1;
                    if (value.is_array()) {
                        type = "float";
                        tuple_size = static_cast<int>(value.size());
                    } else if (value.is_string()) {
                        type = "string";
                    } else if (value.is_boolean() || value.is_number_integer()) {
                        type = "int";
                    }
                    push_attr(node_id, "primitive", name, type, tuple_size);
                }
            }
        }
    }
    return result;
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

// Must match Unity PcgGraphPreviewSubgraph.PreviewSinkNodeId. Upstream-only
// preview cooks include ForEachBegin but truncate ForEachEnd; open regions are
// allowed only for that preview sink and cook the first iteration/piece.
constexpr const char* kPreviewSinkNodeId = "__pcg_preview_sink__";

struct ForEachRegion {
    std::string begin_id;
    std::string end_id; // empty => open preview region (no matching End in graph)
    bool first_iteration_only = false;
    std::vector<std::string> body_order;
    std::unordered_set<std::string> body_nodes;
};

bool graph_has_preview_sink(const Graph& graph)
{
    for (const auto& node : graph.nodes) {
        if (node.id == kPreviewSinkNodeId)
            return true;
    }
    return false;
}

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

    // Parentheses-style pairing so nested ForEachBegin/End match correctly.
    // Nearest-reachable-End pairing incorrectly binds an outer Begin to an inner End.
    std::vector<std::string> begin_stack;
    std::vector<std::pair<std::string, std::string>> pairs;
    for (const auto& id : topo_order) {
        const auto it = node_by_id.find(id);
        if (it == node_by_id.end())
            continue;
        if (it->second->type == "ForEachBegin") {
            begin_stack.push_back(id);
            continue;
        }
        if (it->second->type != "ForEachEnd")
            continue;
        if (begin_stack.empty()) {
            code = fail(err_buf, err_buf_size, PCG_ERR_EXECUTION,
                        "ForEachEnd has no matching ForEachBegin");
            return false;
        }
        pairs.emplace_back(begin_stack.back(), id);
        begin_stack.pop_back();
    }
    if (!begin_stack.empty() && !graph_has_preview_sink(graph)) {
        code = fail(err_buf, err_buf_size, PCG_ERR_EXECUTION,
                    "ForEachBegin has no matching ForEachEnd");
        return false;
    }

    for (const auto& [begin_id, end_id] : pairs) {
        const auto from_begin = reachable_from(begin_id, adjacency);
        const auto from_end_rev = reachable_from(end_id, reverse_adjacency);
        std::unordered_set<std::string> can_reach_end(from_end_rev.begin(), from_end_rev.end());

        ForEachRegion region;
        region.begin_id = begin_id;
        region.end_id = end_id;
        for (const auto& id : topo_order) {
            if (id == begin_id || id == end_id)
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

    // Open ForEach regions: Editor node preview truncates End. Cook first
    // iteration/piece only; leave Output sinks for the main topo pass.
    for (const auto& begin_id : begin_stack) {
        const auto from_begin = reachable_from(begin_id, adjacency);
        ForEachRegion region;
        region.begin_id = begin_id;
        region.first_iteration_only = true;
        for (const auto& id : topo_order) {
            if (id == begin_id)
                continue;
            const auto node_it = node_by_id.find(id);
            if (node_it == node_by_id.end())
                continue;
            if (node_it->second->type == "Output")
                continue;
            const bool from_begin_hit =
                std::find(from_begin.begin(), from_begin.end(), id) != from_begin.end();
            if (!from_begin_hit)
                continue;
            region.body_nodes.insert(id);
            region.body_order.push_back(id);
        }
        regions.push_back(std::move(region));
    }

    code = PCG_OK;
    return true;
}

// ── Preview pick attribution ─────────────────────────────
// Stamps a primitive int attribute with a stable per-node code so a preview
// consumer can map a picked triangle back to the graph node that produced it.
// Faces already stamped by an upstream node are left untouched; only
// unattributed faces (freshly created, or rebuilt by topology-dropping ops
// like Bevel/Boolean) take the current node's code.
constexpr const char* kSourceNodeAttr = "__pcg_src";

int64_t source_node_code(const std::string& node_id)
{
    uint64_t hash = 14695981039346656037ull; // FNV-1a 64
    for (const unsigned char c : node_id) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    return static_cast<int64_t>(hash | 1ull); // 0 is reserved = unattributed
}

void stamp_geometry_source(data::PcgGeometry& geometry, int64_t code)
{
    const size_t face_count = geometry.faces().size();
    data::AttributeArray* attr =
        geometry.attributes().find(data::AttributeOwner::Primitive, kSourceNodeAttr);
    if (!attr) {
        data::AttributeArray& created = geometry.attributes().create_int(
            data::AttributeOwner::Primitive, kSourceNodeAttr, 1, {0});
        created.resize(face_count);
        attr = &created;
    } else if (attr->schema().type != data::AttributeType::Int) {
        return;
    } else if (attr->size() != face_count) {
        // Topology grew without remapping the attribute (e.g. faces appended
        // without propagate_geometry_data). Appended faces land at the tail
        // with default 0 and get stamped below.
        attr->resize(face_count);
    }
    auto& values = attr->int_values_mut();
    for (size_t i = 0; i < face_count; ++i) {
        if (values[i] == 0)
            values[i] = code;
    }
}

void stamp_collection_sources(data::PcgDataCollection& collection, int64_t code)
{
    for (auto& item : collection.items_mut()) {
        if (!item.geometry)
            continue;
        if (item.geometry.use_count() == 1) {
            // Freshly cooked output is uniquely owned by this collection.
            stamp_geometry_source(const_cast<data::PcgGeometry&>(*item.geometry), code);
            continue;
        }
        auto stamped = std::make_shared<data::PcgGeometry>(*item.geometry);
        stamp_geometry_source(*stamped, code);
        item.geometry = std::move(stamped);
    }
}

/// Emits source_nodes (node id per index) + triangle_sources (index per
/// triangle, -1 = unattributed) aligned with compute_split_normals triangle
/// order, which is the PCGM mesh blob the web preview raycasts against.
void attach_source_mapping(nlohmann::json& json,
                           const Graph& graph,
                           const data::PcgGeometry& geometry)
{
    const data::AttributeArray* attr =
        geometry.attributes().find(data::AttributeOwner::Primitive, kSourceNodeAttr);
    std::unordered_map<int64_t, std::string> id_by_code;
    for (const auto& node : graph.nodes)
        id_by_code.emplace(source_node_code(node.id), node.id);

    auto source_nodes = nlohmann::json::array();
    auto triangle_sources = nlohmann::json::array();
    std::unordered_map<int64_t, size_t> index_by_code;

    const auto& points = geometry.points();
    const auto& faces = geometry.faces();
    for (size_t fi = 0; fi < faces.size(); ++fi) {
        const auto& face = faces[fi];
        if (face.size() < 3)
            continue;
        int source_index = -1;
        if (attr && attr->schema().type == data::AttributeType::Int && fi < attr->size()) {
            const int64_t code = attr->int_values()[fi];
            if (code != 0) {
                if (const auto idx_it = index_by_code.find(code); idx_it != index_by_code.end()) {
                    source_index = static_cast<int>(idx_it->second);
                } else if (const auto id_it = id_by_code.find(code); id_it != id_by_code.end()) {
                    source_index = static_cast<int>(source_nodes.size());
                    index_by_code[code] = source_nodes.size();
                    source_nodes.push_back(id_it->second);
                }
            }
        }
        const size_t tri_count = data::triangulate_face_corners(points, face).size();
        for (size_t t = 0; t < tri_count; ++t)
            triangle_sources.push_back(source_index);
    }
    json["source_nodes"] = std::move(source_nodes);
    json["triangle_sources"] = std::move(triangle_sources);
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
    bool allow_cache,
    CookDiagnostics& diagnostics)
{
    allow_cache = allow_cache && !diagnostics.degraded();
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
            diagnostics.append(cached_outputs, true);
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

    PcgResultCode rc = PCG_OK;
    try {
        rc = element->execute(ctx);
    } catch (const std::exception& exception) {
        const std::string message = node.type + " [" + node.id + "] failed: " + exception.what();
        ctx.outputs.add(kNodeDiagnosticTag, data::PcgDataType::Unknown, {
            {"node_id", node.id}, {"node_type", node.type}, {"outcome", "failure"},
            {"fallback_used", false}, {"reason", message},
        });
        rc = fail(err_buf, err_buf_size, PCG_ERR_EXECUTION, message.c_str());
    }
    diagnostics.append(ctx.outputs, false);
    // A transient fallback must not poison this node or downstream caches.
    allow_cache = allow_cache && !diagnostics.degraded();
    if (rc != PCG_OK)
        return rc;

    outputs[node.id] = std::move(ctx.outputs);
    // Stamp unconditionally: the node cache reuses upstream outputs across
    // preview-sink renames, so stamps must exist (and be identical) whether or
    // not this particular cook has a preview sink.
    stamp_collection_sources(outputs[node.id], source_node_code(node.id));
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
    const std::vector<ForEachRegion>& all_regions,
    const std::unordered_map<std::string, const ForEachRegion*>& foreach_by_begin,
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
    int err_buf_size,
    CookDiagnostics& diagnostics)
{
    static const std::vector<const GraphEdge*> kNoIncomingEdges;
    const GraphNode* begin_node = node_by_id.at(region.begin_id);
    const GraphNode* end_node = nullptr;
    if (!region.end_id.empty()) {
        const auto end_it = node_by_id.find(region.end_id);
        if (end_it == node_by_id.end())
            return fail(err_buf, err_buf_size, PCG_ERR_EXECUTION,
                        "ForEachEnd node missing from graph");
        end_node = end_it->second;
    }
    const bool first_iteration_only = region.first_iteration_only || end_node == nullptr;

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
    const int configured_iterations = std::max(0, begin_node->data.value("iterations", 1));
    const std::string iterations_attribute =
        begin_node->data.value("iterationsAttribute", std::string());
    const int iterations = elements::read_iterations_attribute(
        *seed_geometry, iterations_attribute, configured_iterations);
    const std::string gather_method = end_node
        ? end_node->data.value("gatherMethod", std::string("merge"))
        : std::string("merge");

    auto inject_loop_metadata = [](data::PcgGeometry& geometry,
                                   int iteration,
                                   int num_iterations,
                                   int64_t value) {
        elements::set_detail_int(geometry, "iteration", iteration);
        elements::set_detail_int(geometry, "numiterations", num_iterations);
        elements::set_detail_int(geometry, "ivalue", value);
        elements::set_detail_float(geometry, "value", static_cast<double>(value));
    };

    auto is_nested_managed = [&](const std::string& body_id) -> bool {
        for (const auto& other : all_regions) {
            if (other.begin_id == region.begin_id)
                continue;
            if (region.body_nodes.count(other.begin_id) == 0)
                continue;
            if (other.body_nodes.count(body_id) > 0 ||
                (!other.end_id.empty() && other.end_id == body_id))
                return true;
        }
        return false;
    };

    auto cook_body_once = [&](const data::PcgGeometry& piece,
                              data::PcgGeometry& end_geometry) -> PcgResultCode {
        data::PcgDataCollection begin_out;
        begin_out.add_geometry("out", piece);
        outputs[region.begin_id] = std::move(begin_out);
        output_hashes[region.begin_id] = compute_output_hash(outputs[region.begin_id]);

        for (const auto& body_id : region.body_order) {
            if (const auto nested_it = foreach_by_begin.find(body_id);
                nested_it != foreach_by_begin.end()) {
                const PcgResultCode nested_rc = cook_foreach_region(
                    *nested_it->second, all_regions, foreach_by_begin, graph, node_by_id,
                    incoming_by_node, seed, outputs, output_hashes, textures, meshes, splines,
                    heightfields, is_cancel_requested, perf, err_buf, err_buf_size, diagnostics);
                if (nested_rc != PCG_OK)
                    return nested_rc;
                continue;
            }
            if (is_nested_managed(body_id))
                continue;

            const GraphNode* body_node = node_by_id.at(body_id);
            const auto incoming_it = incoming_by_node.find(body_id);
            const auto& incoming = incoming_it != incoming_by_node.end()
                ? incoming_it->second : kNoIncomingEdges;
            const PcgResultCode rc = cook_single_node(
                graph, *body_node, seed, incoming, outputs, output_hashes,
                textures, meshes, splines, heightfields, nullptr, is_cancel_requested,
                perf, err_buf, err_buf_size, false, diagnostics);
            if (rc != PCG_OK)
                return rc;
        }

        if (end_node == nullptr) {
            end_geometry = piece;
            return PCG_OK;
        }

        const auto end_incoming_it = incoming_by_node.find(region.end_id);
        const auto& end_incoming = end_incoming_it != incoming_by_node.end()
            ? end_incoming_it->second : kNoIncomingEdges;
        const PcgResultCode end_rc = cook_single_node(
            graph, *end_node, seed, end_incoming, outputs, output_hashes,
            textures, meshes, splines, heightfields, nullptr, is_cancel_requested,
            perf, err_buf, err_buf_size, false, diagnostics);
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
        const int loop_count = first_iteration_only ? std::min(1, iterations) : iterations;
        if (loop_count <= 0) {
            if (end_node != nullptr) {
                data::PcgDataCollection end_out;
                end_out.add_geometry("out", data::PcgGeometry{});
                outputs[region.end_id] = std::move(end_out);
                output_hashes[region.end_id] = compute_output_hash(outputs[region.end_id]);
            } else {
                data::PcgDataCollection begin_out;
                begin_out.add_geometry("out", data::PcgGeometry{});
                outputs[region.begin_id] = std::move(begin_out);
                output_hashes[region.begin_id] = compute_output_hash(outputs[region.begin_id]);
            }
            return PCG_OK;
        }
        data::PcgGeometry current = *seed_geometry;
        for (int i = 0; i < loop_count; ++i) {
            data::PcgGeometry piece = current;
            inject_loop_metadata(piece, i, iterations, i);
            data::PcgGeometry end_geometry;
            const PcgResultCode rc = cook_body_once(piece, end_geometry);
            if (rc != PCG_OK)
                return rc;
            if (first_iteration_only)
                return PCG_OK;
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
        const int num_pieces = static_cast<int>(pieces.size());
        if (pieces.empty()) {
            if (end_node != nullptr) {
                data::PcgDataCollection end_out;
                end_out.add_geometry("out", data::PcgGeometry{});
                outputs[region.end_id] = std::move(end_out);
                output_hashes[region.end_id] = compute_output_hash(outputs[region.end_id]);
            } else {
                data::PcgDataCollection begin_out;
                begin_out.add_geometry("out", data::PcgGeometry{});
                outputs[region.begin_id] = std::move(begin_out);
                output_hashes[region.begin_id] = compute_output_hash(outputs[region.begin_id]);
            }
            return PCG_OK;
        }
        if (first_iteration_only) {
            data::PcgGeometry piece = pieces.front();
            inject_loop_metadata(piece, 0, num_pieces, 0);
            data::PcgGeometry end_geometry;
            return cook_body_once(piece, end_geometry);
        }
        int piece_index = 0;
        for (const auto& piece_in : pieces) {
            data::PcgGeometry piece = piece_in;
            inject_loop_metadata(piece, piece_index, num_pieces, piece_index);
            data::PcgGeometry end_geometry;
            const PcgResultCode rc = cook_body_once(piece, end_geometry);
            if (rc != PCG_OK)
                return rc;
            accumulated = data::merge_geometries(accumulated, end_geometry);
            ++piece_index;
        }
    }

    if (end_node == nullptr)
        return PCG_OK;

    data::PcgDataCollection end_out;
    end_out.add_geometry("out", std::move(accumulated));
    outputs[region.end_id] = std::move(end_out);
    output_hashes[region.end_id] = compute_output_hash(outputs[region.end_id]);
    return PCG_OK;
}

} // namespace

static PcgResultCode execute_graph_impl(const Graph& graph,
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
                            GraphPerfReport* perf,
                            CookDiagnostics& diagnostics)
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
    std::unordered_set<std::string> nested_foreach_begins;
    for (const auto& region : foreach_regions) {
        foreach_by_begin[region.begin_id] = &region;
        foreach_managed.insert(region.begin_id);
        if (!region.end_id.empty())
            foreach_managed.insert(region.end_id);
        for (const auto& body_id : region.body_nodes)
            foreach_managed.insert(body_id);
    }
    for (const auto& region : foreach_regions) {
        for (const auto& body_id : region.body_nodes) {
            if (foreach_by_begin.count(body_id) > 0)
                nested_foreach_begins.insert(body_id);
        }
    }

    std::unordered_map<std::string, uint64_t> output_hashes;
    NodeOutputMap outputs;
    for (const auto& node_id : order) {
        if (is_cancel_requested && is_cancel_requested())
            return fail(err_buf, err_buf_size, PCG_ERR_EXECUTION, "Execution cancelled");

        if (const auto begin_it = foreach_by_begin.find(node_id); begin_it != foreach_by_begin.end()) {
            // Nested regions are cooked by their parent ForEach body.
            if (nested_foreach_begins.count(node_id) > 0)
                continue;
            const PcgResultCode rc = cook_foreach_region(
                *begin_it->second, foreach_regions, foreach_by_begin, graph, node_by_id,
                incoming_by_node, seed, outputs, output_hashes, textures, meshes, splines,
                heightfields, is_cancel_requested, perf, err_buf, err_buf_size, diagnostics);
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
            true, diagnostics);
        if (rc != PCG_OK)
            return rc;
    }

    auto node_stats = build_node_stats(outputs, node_by_id);
    auto per_node_groups = build_per_node_groups(outputs);
    auto per_node_attrs = build_per_node_attributes(outputs);

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
    out_result.spawn_meshes.clear();
    out_result.spawn_point_counts.clear();
    for (const auto& item : sink_output.items()) {
        if (item.tag == "spawnMesh" && item.mesh &&
            !item.mesh->vertices().empty() && item.mesh->triangles().size() >= 3) {
            out_result.spawn_meshes.push_back(*item.mesh);
        }
    }
    if (out_result.spawn_meshes.empty()) {
        for (const auto& [node_id, collection] : outputs) {
            (void)node_id;
            for (const auto& item : collection.items()) {
                if (item.tag == "spawnMesh" && item.mesh &&
                    !item.mesh->vertices().empty() && item.mesh->triangles().size() >= 3) {
                    out_result.spawn_meshes.push_back(*item.mesh);
                }
            }
            if (!out_result.spawn_meshes.empty())
                break;
        }
    }
    if (!out_result.spawn_meshes.empty())
        out_result.spawn_mesh = out_result.spawn_meshes.front();
    else
        out_result.spawn_mesh = data::PcgMeshData{};

    if (const data::PcgTaggedData* out_item = sink_output.find("out"); out_item && out_item->points) {
        out_result.kind = GraphResultKind::Points;
        out_result.points = out_item->points;
        out_result.point_sidecar = out_item->payload;
        if (out_result.point_sidecar.is_object() &&
            out_result.point_sidecar.contains("spawnProtoCounts") &&
            out_result.point_sidecar["spawnProtoCounts"].is_array()) {
            for (const auto& count : out_result.point_sidecar["spawnProtoCounts"]) {
                if (count.is_number_integer())
                    out_result.spawn_point_counts.push_back(count.get<int>());
            }
        }
        if (out_result.spawn_point_counts.empty() && out_result.spawn_meshes.size() == 1 &&
            out_result.points) {
            out_result.spawn_point_counts.push_back(
                static_cast<int>(out_result.points->points().size()));
        }
        out_result.json = nlohmann::json::object();
        out_result.mesh = data::PcgMeshData{};
        out_result.json["node_stats"] = node_stats;
        out_result.json["node_groups"] = per_node_groups;
        out_result.json["node_attrs"] = per_node_attrs;
        return PCG_OK;
    }

    if (auto heightfield = sink_output.find_heightfield_shared("out")) {
        out_result.source_heightfield = heightfield;
        out_result.kind = GraphResultKind::Json;
        out_result.json = build_heightfield_summary(*heightfield);
        out_result.mesh = data::PcgMeshData{};
        out_result.json["node_stats"] = node_stats;
        out_result.json["node_groups"] = per_node_groups;
        out_result.json["node_attrs"] = per_node_attrs;
        return PCG_OK;
    }

    const nlohmann::json primary = sink_output.primary_json();
    if (primary.is_object() && primary.contains("points") && primary["points"].is_array()) {
        out_result.kind = GraphResultKind::Json;
        out_result.json = primary;
        out_result.mesh = data::PcgMeshData{};
        out_result.json["node_stats"] = node_stats;
        out_result.json["node_groups"] = per_node_groups;
        out_result.json["node_attrs"] = per_node_attrs;
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
        out_result.json["node_attrs"] = per_node_attrs;
        out_result.json["geometry_export"] = "sink_geometry";
        if (graph_has_preview_sink(graph))
            attach_source_mapping(out_result.json, graph, *geometry);
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
        out_result.json["node_attrs"] = per_node_attrs;
        out_result.json["geometry_export"] = "sink_geometry";
        if (graph_has_preview_sink(graph))
            attach_source_mapping(out_result.json, graph, *geometry);
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
        out_result.json["node_attrs"] = per_node_attrs;
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
        out_result.json["node_attrs"] = per_node_attrs;
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
    out_result.json["node_attrs"] = per_node_attrs;
    return PCG_OK;
}

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
    // Per-call state: independent cooks and nested ForEach executions cannot
    // overwrite one another's receipts. Reset stale results on a failed cook.
    out_result = GraphExecutionResult{};
    CookDiagnostics diagnostics;
    const PcgResultCode code = execute_graph_impl(
        graph, seed, out_result, err_buf, err_buf_size, textures, meshes, splines,
        heightfields, cache, is_cancel_requested, perf, diagnostics);
    diagnostics.attach(out_result.json, code, graph, seed);
    return code;
}

} // namespace pcg::internal
