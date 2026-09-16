#include "elements/add_algorithms.hpp"

#include "geometry/element_pattern.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_set>

namespace pcg::internal::elements {
namespace {

std::string trim_copy(const std::string& text)
{
    size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])))
        ++start;
    size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])))
        --end;
    return text.substr(start, end - start);
}

std::vector<std::string> split_tokens(const std::string& text)
{
    std::vector<std::string> tokens;
    std::istringstream stream(text);
    std::string token;
    while (stream >> token)
        tokens.push_back(token);
    return tokens;
}

void append_token_indices(const std::string& token, int point_count, std::vector<int>& out)
{
    if (token.empty() || point_count <= 0)
        return;

    const auto colon = token.find(':');
    const auto dash = token.find('-');
    if (dash != std::string::npos && colon != std::string::npos && colon > dash) {
        try {
            const int start = std::stoi(token.substr(0, dash));
            const int end = std::stoi(token.substr(dash + 1, colon - dash - 1));
            const std::string step_spec = token.substr(colon + 1);
            const auto comma = step_spec.find(',');
            const int every = comma == std::string::npos ? 1 : std::stoi(step_spec.substr(0, comma));
            const int of = comma == std::string::npos ? 0 : std::stoi(step_spec.substr(comma + 1));
            std::unordered_set<int> selected;
            geometry::parse_element_range(start, end, std::max(1, of), every, point_count, selected);
            std::vector<int> ordered(selected.begin(), selected.end());
            std::sort(ordered.begin(), ordered.end());
            for (int index : ordered)
                out.push_back(index);
            return;
        } catch (...) {
            return;
        }
    }

    if (dash != std::string::npos) {
        try {
            const int start = std::stoi(token.substr(0, dash));
            const int end = std::stoi(token.substr(dash + 1));
            const int clamped_start = std::max(0, start);
            const int clamped_end = std::min(point_count - 1, end);
            for (int index = clamped_start; index <= clamped_end; ++index)
                out.push_back(index);
            return;
        } catch (...) {
            return;
        }
    }

    try {
        const int index = std::stoi(token);
        if (index >= 0 && index < point_count)
            out.push_back(index);
    } catch (...) {
    }
}

AddPointSpec parse_point_item(const nlohmann::json& item)
{
    AddPointSpec spec;
    if (item.is_array() && item.size() >= 3) {
        spec.x = item[0].is_number() ? item[0].get<double>() : 0.0;
        spec.y = item[1].is_number() ? item[1].get<double>() : 0.0;
        spec.z = item[2].is_number() ? item[2].get<double>() : 0.0;
        if (item.size() >= 4 && item[3].is_number())
            spec.w = item[3].get<double>();
        return spec;
    }
    if (!item.is_object())
        return spec;
    spec.enabled = item.value("enabled", true);
    spec.x = item.value("x", 0.0);
    spec.y = item.value("y", 0.0);
    spec.z = item.value("z", 0.0);
    spec.w = item.value("w", 1.0);
    return spec;
}

} // namespace

std::vector<int> parse_add_polygon_line(const std::string& line, int point_count)
{
    std::vector<int> indices;
    const std::string trimmed = trim_copy(line);
    if (trimmed.empty() || point_count <= 0)
        return indices;
    for (const auto& token : split_tokens(trimmed))
        append_token_indices(token, point_count, indices);
    return indices;
}

std::vector<AddPointSpec> parse_add_points(const nlohmann::json& data)
{
    std::vector<AddPointSpec> points;
    const nlohmann::json* source = nullptr;
    nlohmann::json parsed;

    if (data.contains("points")) {
        const auto& raw = data["points"];
        if (raw.is_array()) {
            source = &raw;
        } else if (raw.is_string()) {
            try {
                parsed = nlohmann::json::parse(raw.get<std::string>());
                if (parsed.is_array())
                    source = &parsed;
            } catch (...) {
            }
        }
    }

    if (!source)
        return points;

    points.reserve(source->size());
    for (const auto& item : *source) {
        const AddPointSpec spec = parse_point_item(item);
        if (spec.enabled)
            points.push_back(spec);
    }
    return points;
}

data::PcgGeometry add_geometry(const data::PcgGeometry& input, const AddOptions& options)
{
    data::PcgGeometry out = input;
    if (options.delete_primitives_keep_points)
        out.faces_mut().clear();

    for (const auto& spec : options.points) {
        if (!spec.enabled)
            continue;
        out.points_mut().push_back({spec.x, spec.y, spec.z});
    }

    const int point_count = static_cast<int>(out.points().size());
    if (options.polygons_spec.empty() || point_count <= 0)
        return out;

    std::istringstream lines(options.polygons_spec);
    std::string line;
    while (std::getline(lines, line)) {
        const std::vector<int> indices = parse_add_polygon_line(line, point_count);
        if (indices.size() < 2)
            continue;
        out.faces_mut().push_back(indices);
    }

    return out;
}

} // namespace pcg::internal::elements
