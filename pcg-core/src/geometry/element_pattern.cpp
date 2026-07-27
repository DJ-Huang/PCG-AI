#include "geometry/element_pattern.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>

namespace pcg::internal::geometry {

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

void select_all(int element_count, std::unordered_set<int>& selected)
{
    for (int index = 0; index < element_count; ++index)
        selected.insert(index);
}

void add_index(int index, int element_count, std::unordered_set<int>& selected)
{
    if (index >= 0 && index < element_count)
        selected.insert(index);
}

void add_range(int start, int end, int element_count, std::unordered_set<int>& selected)
{
    if (element_count <= 0)
        return;
    const int clamped_start = std::max(0, start);
    const int clamped_end = std::min(element_count - 1, end);
    if (clamped_start > clamped_end)
        return;
    for (int index = clamped_start; index <= clamped_end; ++index)
        selected.insert(index);
}

void erase_range(int start, int end, int element_count, std::unordered_set<int>& selected)
{
    if (element_count <= 0)
        return;
    const int clamped_start = std::max(0, start);
    const int clamped_end = std::min(element_count - 1, end);
    for (int index = clamped_start; index <= clamped_end; ++index)
        selected.erase(index);
}

void ensure_universe(int element_count, std::unordered_set<int>& selected)
{
    if (!selected.empty())
        return;
    select_all(element_count, selected);
}

void apply_pattern_token(const std::string& token,
                         int element_count,
                         std::unordered_set<int>& selected)
{
    if (token.empty() || element_count <= 0)
        return;

    const bool negated = token[0] == '!';
    const std::string body = negated ? token.substr(1) : token;

    if (body == "*" || body.empty()) {
        if (negated)
            return;
        selected.clear();
        select_all(element_count, selected);
        return;
    }

    const auto dash = body.find('-');
    if (dash != std::string::npos) {
        try {
            const int start = std::stoi(body.substr(0, dash));
            const int end = std::stoi(body.substr(dash + 1));
            if (negated) {
                ensure_universe(element_count, selected);
                erase_range(start, end, element_count, selected);
            } else {
                add_range(start, end, element_count, selected);
            }
            return;
        } catch (...) {
            return;
        }
    }

    try {
        const int index = std::stoi(body);
        if (negated) {
            ensure_universe(element_count, selected);
            selected.erase(index);
        } else {
            add_index(index, element_count, selected);
        }
    } catch (...) {
    }
}

} // namespace

bool compute_element_pattern(const std::string& pattern,
                             int element_count,
                             std::unordered_set<int>& selected)
{
    selected.clear();
    const std::string trimmed = trim_copy(pattern);
    if (trimmed.empty() || element_count <= 0)
        return false;

    if (trimmed == "!*")
        return true;

    const auto tokens = split_tokens(trimmed);
    if (tokens.empty())
        return false;

    for (const auto& token : tokens)
        apply_pattern_token(token, element_count, selected);
    return true;
}

bool parse_element_pattern(const std::string& pattern,
                           int element_count,
                           std::unordered_set<int>& selected)
{
    std::unordered_set<int> computed;
    if (!compute_element_pattern(pattern, element_count, computed))
        return false;
    for (int index : computed)
        selected.insert(index);
    return true;
}

bool parse_element_range(int start,
                         int end,
                         int select_of,
                         int select_offset,
                         int element_count,
                         std::unordered_set<int>& selected)
{
    if (element_count <= 0)
        return false;

    const int clamped_start = std::max(0, start);
    const int clamped_end = std::min(element_count - 1, end);
    if (clamped_start > clamped_end)
        return false;

    const int period = std::max(1, select_of);
    const int offset = select_offset;
    for (int index = clamped_start; index <= clamped_end; ++index) {
        if ((index - offset) % period == 0)
            selected.insert(index);
    }
    return !selected.empty();
}

} // namespace pcg::internal::geometry
