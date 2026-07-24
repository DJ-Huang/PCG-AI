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
    std::string justify_with = "inputIfWired";
    std::string group;
    std::string group_type = "guess";
    bool use_groups_for_bounds = false;
    std::string source_group;
    std::string source_group_type = "guess";
    std::string target_group;
    std::string target_group_type = "guess";
    bool translate = true;
    bool scale_to_fit = true;
    bool uniform_scale = true;
    std::string scale_axis = "bestFit";
    bool scale_x = true;
    bool scale_y = true;
    bool scale_z = true;
    std::string justify_x = "center";
    std::string justify_y = "center";
    std::string justify_z = "center";
    std::string target_justify_x = "same";
    std::string target_justify_y = "same";
    std::string target_justify_z = "same";
    data::PcgVec3 offset{0.0, 0.0, 0.0};
    data::PcgVec3 target_position{0.0, 0.0, 0.0};
    data::PcgVec3 target_size{1.0, 1.0, 1.0};
    bool restore_transform = false;
    std::string restore_attribute = "xform";
    bool stash_transform = true;
    std::string stash_attribute = "xform";
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
