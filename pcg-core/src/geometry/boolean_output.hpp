#pragma once

// Boolean output: convert arrangement result to PcgGeometry with groups.
// Handles detriangulation (All/Unchanged/None) and group encoding.

#include "geometry/arrangement.hpp"
#include "data/pcg_geometry.hpp"

namespace pcg::internal::geometry {

/// Convert boolean result to final PcgGeometry with detriangulation.
/// @param result The boolean operation result
/// @param mode Detriangulation mode
/// @return Final geometry with source polygons reconstructed according to mode
data::PcgGeometry finalize_boolean_output(const BooleanResult& result,
                                          DetriangulateMode mode);

} // namespace pcg::internal::geometry
