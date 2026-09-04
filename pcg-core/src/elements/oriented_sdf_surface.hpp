#pragma once

#include "data/pcg_geometry.hpp"
#include "elements/pcg_element.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace pcg::internal::elements {

/**
 * A compact, topology-free measurement of a reference surface.
 *
 * The positions and oriented normals are sampled from the reference mesh, but
 * faces/indices are deliberately not retained.  OrientedSdfSurface rebuilds a
 * new implicit surface from these samples at cook time.
 */
struct OrientedSurfacePoint {
    data::PcgVec3 position;
    data::PcgVec3 normal;
    data::PcgColor color;
    data::PcgVec2 uv;
};

struct OrientedPointCloudStats {
    size_t point_count = 0;
    size_t source_point_count = 0;
    data::PcgVec3 minimum{};
    data::PcgVec3 maximum{};
    bool has_source_colors = false;
    bool has_source_uvs = false;
    size_t payload_bytes = 0;
    double sampling_spacing = 0.0;
};

struct OrientedSdfOptions {
    double cell_size = 0.004;
    double support_radius_cells = 2.5;
    double iso_offset = 0.0;
    size_t max_active_cells = 3000000;
    bool transfer_colors = true;
    bool transfer_uvs = true;
    bool flip_uv_v = false;
};

struct EmbeddedTexturePayload {
    std::string mime_type;
    std::vector<uint8_t> bytes;

    bool empty() const { return bytes.empty(); }
};

struct EmbeddedPbrTextures {
    EmbeddedTexturePayload base_color;
    EmbeddedTexturePayload normal;
    EmbeddedTexturePayload orm;
};

/** Encode/decode the OPC1 quantised oriented-point payload used in graph data. */
bool encode_oriented_point_cloud(const std::vector<OrientedSurfacePoint>& points,
                                 bool has_source_colors,
                                 bool has_source_uvs,
                                 std::string& base64,
                                 OrientedPointCloudStats& stats,
                                 std::string& error);
bool decode_oriented_point_cloud(const std::string& base64,
                                 std::vector<OrientedSurfacePoint>& points,
                                 OrientedPointCloudStats& stats,
                                 std::string& error);

/**
 * Measure vertices plus deterministic barycentric samples over triangle
 * interiors. Source faces are used only during this bake call and are never
 * retained in the returned point set or OPC1 payload.
 */
bool sample_oriented_geometry(const data::PcgGeometry& geometry,
                              double sample_spacing,
                              std::vector<OrientedSurfacePoint>& points,
                              OrientedPointCloudStats& stats,
                              std::string& error);

/**
 * Read a polygon file through the existing ImportMesh path and encode only its
 * points, oriented normals, and optional vertex colours.  No source topology is
 * present in the returned payload.
 */
bool encode_oriented_point_cloud_file(const std::string& path,
                                      std::string& base64,
                                      OrientedPointCloudStats& stats,
                                      std::string& error,
                                      double sample_spacing = 0.0);

/** Extract compressed PBR images embedded in GLB/glTF material zero. */
bool extract_embedded_pbr_textures_file(const std::string& path,
                                        EmbeddedPbrTextures& textures,
                                        std::string& error);

/** Reconstruct a fresh polygon surface with sparse MLS-SDF + Surface Nets. */
bool reconstruct_oriented_sdf_surface(const std::vector<OrientedSurfacePoint>& points,
                                      const OrientedSdfOptions& options,
                                      data::PcgGeometry& output,
                                      std::string& error,
                                      const std::function<bool()>& cancelled = {});

void register_oriented_sdf_surface_elements(
    std::unordered_map<std::string, std::unique_ptr<IPcgElement>>& map);

} // namespace pcg::internal::elements
