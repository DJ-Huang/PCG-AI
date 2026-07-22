#pragma once

#include "data/pcg_geometry.hpp"

#include <string>

namespace pcg::internal::elements {

struct LotSubdivisionOptions {
    double min_size = 1.0;
    int iterations = 3;
    double irregularity = 0.5;
    int seed = 0;
    /// "longestEdge" (shape-relative) or "boundingBox" (world-axis).
    std::string alignment = "longestEdge";
};

/// Iteratively bipartition polygon faces into smaller lots (Labs Lot Subdivision).
/// Output is polygon SpatialMesh geometry with primitive int attribute `lotid`.
data::PcgGeometry lot_subdivide_geometry(const data::PcgGeometry& input,
                                         const LotSubdivisionOptions& options);

} // namespace pcg::internal::elements
