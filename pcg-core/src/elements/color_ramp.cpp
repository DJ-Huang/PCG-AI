#include "elements/color_ramp.hpp"

#include <algorithm>
#include <cmath>

namespace pcg::internal::elements {
namespace {

bool read_color(const nlohmann::json& value, std::array<double, 3>& out)
{
    if (value.is_array() && value.size() >= 3 && value[0].is_number() && value[1].is_number() &&
        value[2].is_number()) {
        out = {value[0].get<double>(), value[1].get<double>(), value[2].get<double>()};
        return true;
    }
    if (value.is_object()) {
        const auto read_component = [&](const char* key, double& component) -> bool {
            const auto it = value.find(key);
            return it != value.end() && it->is_number() &&
                   ((component = it->get<double>()), true);
        };
        return read_component("r", out[0]) && read_component("g", out[1]) &&
               read_component("b", out[2]);
    }
    return false;
}

bool parse_interpolation(const std::string& value, ColorRampInterpolation& out)
{
    if (value == "linear")
        out = ColorRampInterpolation::Linear;
    else if (value == "constant")
        out = ColorRampInterpolation::Constant;
    else
        return false;
    return true;
}

bool parse_ramp_object(const nlohmann::json& object, ColorRamp& out, std::string& error)
{
    if (!object.is_object()) {
        error = "each ramp must be a JSON object";
        return false;
    }

    out = ColorRamp{};
    if (object.contains("interpolation")) {
        if (!object["interpolation"].is_string() ||
            !parse_interpolation(object["interpolation"].get<std::string>(), out.interpolation)) {
            error = "ramp interpolation must be 'linear' or 'constant'";
            return false;
        }
    }

    const nlohmann::json* keys = nullptr;
    if (object.contains("keys"))
        keys = &object["keys"];
    else if (object.contains("points"))
        keys = &object["points"];
    if (!keys || !keys->is_array()) {
        error = "ramp requires a keys array";
        return false;
    }

    for (const auto& item : *keys) {
        if (!item.is_object()) {
            error = "ramp keys must be objects";
            return false;
        }
        ColorRampKey key;
        if (item.contains("t") && item["t"].is_number())
            key.position = item["t"].get<double>();
        else if (item.contains("position") && item["position"].is_number())
            key.position = item["position"].get<double>();
        else {
            error = "ramp key requires numeric t or position";
            return false;
        }

        if (item.contains("color")) {
            if (!read_color(item["color"], key.color)) {
                error = "ramp key color must be [r,g,b] or {r,g,b}";
                return false;
            }
        } else if (!read_color(item, key.color)) {
            error = "ramp key requires color";
            return false;
        }
        out.keys.push_back(key);
    }

    if (out.keys.empty()) {
        error = "ramp requires at least one key";
        return false;
    }

    std::sort(out.keys.begin(), out.keys.end(),
              [](const ColorRampKey& a, const ColorRampKey& b) { return a.position < b.position; });
    return true;
}

} // namespace

double houdini_rand(double seed)
{
    const double value = std::sin(seed * 12.9898 + 78.233) * 43758.5453;
    return value - std::floor(value);
}

bool parse_color_ramps(const nlohmann::json& data, ColorRampMap& out, std::string& error)
{
    out.clear();
    if (!data.contains("ramps"))
        return true;

    nlohmann::json ramps;
    const auto& raw = data["ramps"];
    if (raw.is_object()) {
        ramps = raw;
    } else if (raw.is_string()) {
        const std::string text = raw.get<std::string>();
        if (text.empty())
            return true;
        try {
            ramps = nlohmann::json::parse(text);
        } catch (const std::exception& exception) {
            error = std::string("ramps must be a JSON object: ") + exception.what();
            return false;
        }
    } else {
        error = "ramps must be a JSON object or JSON string";
        return false;
    }

    if (!ramps.is_object()) {
        error = "ramps must decode to a JSON object";
        return false;
    }

    for (auto it = ramps.begin(); it != ramps.end(); ++it) {
        ColorRamp ramp;
        if (!parse_ramp_object(it.value(), ramp, error))
            return false;
        out.emplace(it.key(), std::move(ramp));
    }
    return true;
}

bool sample_color_ramp(const ColorRamp& ramp, double t, std::array<double, 3>& out)
{
    if (ramp.keys.empty())
        return false;

    t = std::clamp(t, 0.0, 1.0);
    if (t <= ramp.keys.front().position) {
        out = ramp.keys.front().color;
        return true;
    }
    if (t >= ramp.keys.back().position) {
        out = ramp.keys.back().color;
        return true;
    }

    const auto next_it = std::upper_bound(
        ramp.keys.begin(), ramp.keys.end(), t,
        [](double value, const ColorRampKey& key) { return value < key.position; });
    if (next_it == ramp.keys.end()) {
        out = ramp.keys.back().color;
        return true;
    }
    const auto prev_it = next_it - 1;
    if (ramp.interpolation == ColorRampInterpolation::Constant) {
        out = prev_it->color;
        return true;
    }

    const double span = next_it->position - prev_it->position;
    const double alpha = span <= 0.0 ? 0.0 : (t - prev_it->position) / span;
    for (size_t i = 0; i < 3; ++i)
        out[i] = prev_it->color[i] + (next_it->color[i] - prev_it->color[i]) * alpha;
    return true;
}

bool sample_named_color_ramp(const ColorRampMap& ramps,
                             const std::string& name,
                             double t,
                             std::array<double, 3>& out,
                             std::string& error)
{
    const auto it = ramps.find(name);
    if (it == ramps.end()) {
        error = "unknown ramp '" + name + "'";
        return false;
    }
    if (!sample_color_ramp(it->second, t, out)) {
        error = "ramp '" + name + "' has no keys";
        return false;
    }
    return true;
}

} // namespace pcg::internal::elements
