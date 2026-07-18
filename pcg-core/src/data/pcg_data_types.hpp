#pragma once

namespace pcg::internal::data {

enum class PcgDataType {
    Param,
    Point,
    Spline,
    Mesh,
    Geometry,
    HeightField,
    Unknown,
};

} // namespace pcg::internal::data
