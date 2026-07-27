#pragma once

#include "data/pcg_geometry.hpp"

#include <string>

namespace pcg::internal::elements {

struct LotSubdivisionOptions {
    double min_size = 1.0;
    int iterations = 3;
    double irregularity = 0.5;
    /// Houdini-style float Random Seed (2.3 is meaningful; do not truncate to int).
    double seed = 0.0;
    int graph_seed = 0;
    /// "longestEdge" (shape-relative) or "boundingBox" (world-axis).
    std::string alignment = "longestEdge";
};

/// Iteratively bipartition polygon faces into smaller lots (Labs Lot Subdivision).
/// Output is polygon SpatialMesh geometry with primitive int attribute `lotid`.
data::PcgGeometry lot_subdivide_geometry(const data::PcgGeometry& input,
                                         const LotSubdivisionOptions& options);

} // namespace pcg::internal::elements
