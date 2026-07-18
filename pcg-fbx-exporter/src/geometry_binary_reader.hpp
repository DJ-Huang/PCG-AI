#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace pcg::fbx {

struct Geometry {
    std::vector<std::array<float, 3>> points;
    std::vector<std::vector<uint32_t>> faces;
    std::vector<std::array<float, 4>> colors;
    std::vector<std::array<float, 2>> uvs;
    std::vector<std::array<float, 2>> corner_uvs;
    std::string material;
    std::vector<std::string> face_materials;
};

bool read_geometry_binary(
    const void* data,
    int size,
    Geometry& geometry,
    std::string& error);

} // namespace pcg::fbx
