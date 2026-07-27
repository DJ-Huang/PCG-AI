#pragma once

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace pcg::internal::elements {

enum class ColorRampInterpolation {
    Linear,
    Constant,
};

struct ColorRampKey {
    double position = 0.0;
    std::array<double, 3> color{0.0, 0.0, 0.0};
};

struct ColorRamp {
    ColorRampInterpolation interpolation = ColorRampInterpolation::Linear;
    std::vector<ColorRampKey> keys;
};

using ColorRampMap = std::unordered_map<std::string, ColorRamp>;

double houdini_rand(double seed);

bool parse_color_ramps(const nlohmann::json& data, ColorRampMap& out, std::string& error);

bool sample_color_ramp(const ColorRamp& ramp, double t, std::array<double, 3>& out);

bool sample_named_color_ramp(const ColorRampMap& ramps,
                             const std::string& name,
                             double t,
                             std::array<double, 3>& out,
                             std::string& error);

} // namespace pcg::internal::elements
