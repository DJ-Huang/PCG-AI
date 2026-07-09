#pragma once

#include "data/pcg_mesh_data.hpp"
#include "geometry/spline_geometry.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace pcg::internal::geometry {

struct CrossSectionOptions {
    std::string plane = "auto";
    double weld_epsilon = 1e-4;
    bool center = true;
};

/** Prepared 2D cross-section in local space (XY plane, sweep along +Z). */
struct CrossSectionMesh {
    std::vector<Vec3> vertices;
    std::vector<int> triangles;
};

struct SweepAlongFramesOptions {
    bool cap_start = true;
    bool cap_end = true;
    double profile_roll_radians = 0.0;
    double twist_radians = 0.0;
    double scale_start = 1.0;
    double scale_end = 1.0;
    bool profile_closed = false;
    bool backbone_closed = false;
};

/** Cross-section curve in local XY plane (sweep along backbone tangent). */
struct CurveProfile {
    std::vector<Vec3> points;
    bool closed = false;
};

CrossSectionMesh prepare_cross_section(const data::PcgMeshData& mesh, const CrossSectionOptions& options);

CurveProfile prepare_curve_profile(const std::vector<Vec3>& points,
                                   bool closed_hint,
                                   bool center = true,
                                   const std::string& plane = "xy");

data::PcgMeshData sweep_cross_section(const CrossSectionMesh& section,
                                      const std::vector<Frame3>& frames,
                                      const SweepAlongFramesOptions& options);

data::PcgMeshData sweep_curve_profile(const CurveProfile& profile,
                                      const std::vector<Frame3>& frames,
                                      const SweepAlongFramesOptions& options);

} // namespace pcg::internal::geometry
