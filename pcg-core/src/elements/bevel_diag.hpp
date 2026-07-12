#pragma once

// Bevel diagnostic utilities — gated by PCG_BEVEL_DIAG env var (runtime, zero cost when off).
// Extracted from bevel_blender.cpp to reduce production file volume.

#include "elements/bevel_blender.hpp"

namespace pcg::internal::elements::bevel {

bool bevel_diag_enabled();

struct BevelTopoStats {
    int verts = 0;
    int tris = 0;
    int boundary = 0;
    int nonmanifold = 0;
    int cap_boundary = 0;
    int loops = 0;
    int loops_size_3 = 0;
    int cap_loops_size_3 = 0;
    int index_degenerate = 0;
};

BevelTopoStats analyze_bevel_output(const BevelParams::OutputMesh& out, const CapExtents& cap, double tol);

void log_bevel_stage(const char* stage, const BevelParams::OutputMesh& out, const CapExtents& cap);

void diag_terminal_verts_after_vmesh(BevelParams& bp);

} // namespace pcg::internal::elements::bevel
