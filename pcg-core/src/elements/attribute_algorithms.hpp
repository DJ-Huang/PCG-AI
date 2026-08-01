#pragma once

#include "data/pcg_geometry.hpp"

#include <string>

namespace pcg::internal::elements {

/// Houdini Attribute Transfer SOP subset:
/// https://www.sidefx.com/docs/houdini/nodes/sop/attribtransfer.html
struct AttributeTransferOptions {
    std::string source_group;
    /// points | primitives | vertices | edges
    std::string source_group_type = "primitives";

    std::string destination_group;
    std::string destination_group_type = "primitives";

    bool transfer_detail = true;
    /// empty / "*" = all detail attrs on source
    std::string detail_attributes = "*";

    bool transfer_primitives = false;
    std::string primitive_attributes = "*";

    bool transfer_points = false;
    std::string point_attributes = "*";

    bool transfer_vertices = false;
    std::string vertex_attributes = "*";

    /// When false, skip attribute named "P" (Houdini Allow P Attribute).
    bool allow_p_attribute = false;
    /// Accepted for UI parity; PCG has no Houdini local variables — no-op.
    bool copy_local_variables = true;

    /// elendt | wyvill | blinn | hart | links | heron | uniform
    std::string kernel_function = "elendt";
    double kernel_radius = 10.0;
    int max_sample_count = 1;

    bool enable_distance_threshold = true;
    double distance_threshold = 10.0;
    double blend_width = 0.0;
    /// Used when kernel_function == "uniform" for blend-width / sample weighting.
    double uniform_bias = 0.5;
};

data::PcgGeometry attribute_transfer_geometry(const data::PcgGeometry& target,
                                              const data::PcgGeometry& source,
                                              const AttributeTransferOptions& options);

} // namespace pcg::internal::elements
