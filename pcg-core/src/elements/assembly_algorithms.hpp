#pragma once

#include "data/pcg_geometry.hpp"

#include <filesystem>
#include <string>

namespace pcg::internal::elements {

struct ImportMeshOptions {
    double scale = 1.0;
    std::string axis_conversion = "none";
};

bool import_geometry_file(const std::filesystem::path& path,
                          const ImportMeshOptions& options,
                          data::PcgGeometry& output,
                          std::string& error);

struct MatchSizeOptions {
    bool scale_to_fit = true;
    bool uniform_scale = false;
    std::string uniform_scale_mode = "fit";
    std::string source_justify_x = "center";
    std::string source_justify_y = "center";
    std::string source_justify_z = "center";
    std::string target_justify_x = "center";
    std::string target_justify_y = "center";
    std::string target_justify_z = "center";
    data::PcgVec3 target_center{0.0, 0.0, 0.0};
    data::PcgVec3 target_size{1.0, 1.0, 1.0};
};

bool match_size_geometry(const data::PcgGeometry& source,
                         const data::PcgGeometry* reference,
                         const MatchSizeOptions& options,
                         data::PcgGeometry& output,
                         std::string& error);

struct BendMeshOptions {
    data::PcgVec3 capture_origin{0.0, 0.0, 0.0};
    data::PcgVec3 capture_direction{0.0, 1.0, 0.0};
    data::PcgVec3 up_direction{1.0, 0.0, 0.0};
    double capture_length = 1.0;
    double angle_degrees = 0.0;
    std::string mask_attribute = "bendmask";
};

bool bend_geometry(const data::PcgGeometry& source,
                   const data::PcgGeometry* rest,
                   const BendMeshOptions& options,
                   data::PcgGeometry& output,
                   std::string& error);

} // namespace pcg::internal::elements
