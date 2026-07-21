#pragma once

#include "data/pcg_geometry.hpp"
#include "data/pcg_point_data.hpp"
#include "data/pcg_spline_data.hpp"

#include <string>
#include <vector>

namespace pcg::internal::elements {

struct PrimitiveTransformOptions {
    double scale = 0.85;
    std::string face_group;
};

struct ConvertLineOptions {
    std::string edge_group;
    std::string mode = "unshared"; // unshared | all | group
};

struct ExtractCentroidOptions {
    std::string method = "primitives"; // primitives | points
    std::string face_group;
};

struct GroupTransferOptions {
    std::string group_name;
    std::string domain = "face"; // face | point
    double distance = 0.01;
};

struct ClipOptions {
    double origin_x = 0.0;
    double origin_y = 0.0;
    double origin_z = 0.0;
    double normal_x = 0.0;
    double normal_y = 1.0;
    double normal_z = 0.0;
    bool keep_positive = true;
};

data::PcgGeometry primitive_transform_geometry(const data::PcgGeometry& input,
                                               const PrimitiveTransformOptions& options);

data::PcgSplineData convert_line_geometry(const data::PcgGeometry& input,
                                          const ConvertLineOptions& options);

data::PcgPointData extract_centroid_geometry(const data::PcgGeometry& input,
                                             const ExtractCentroidOptions& options);

data::PcgGeometry group_transfer_geometry(const data::PcgGeometry& target,
                                          const data::PcgGeometry& source,
                                          const GroupTransferOptions& options);

data::PcgGeometry clip_geometry(const data::PcgGeometry& input, const ClipOptions& options);

std::vector<data::PcgGeometry> foreach_pieces(const data::PcgGeometry& input,
                                              const std::string& method,
                                              const std::string& piece_attribute);

} // namespace pcg::internal::elements
