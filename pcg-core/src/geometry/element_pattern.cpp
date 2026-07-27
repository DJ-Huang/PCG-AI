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

bool parse_single_token(const std::string& token,
                        int element_count,
                        bool exclude,
                        std::unordered_set<int>& selected)
{
    if (token.empty() || element_count <= 0)
        return false;

    const bool negated = !token.empty() && token[0] == '!';
    const std::string body = negated ? token.substr(1) : token;
    const bool effective_exclude = exclude || negated;

    if (body == "*" || body.empty()) {
        if (effective_exclude) {
            selected.clear();
        } else {
            for (int index = 0; index < element_count; ++index)
                selected.insert(index);
        }
        return true;
    }

    const auto dash = body.find('-');
    if (dash != std::string::npos) {
        try {
            const int start = std::stoi(body.substr(0, dash));
            const int end = std::stoi(body.substr(dash + 1));
            std::unordered_set<int> range;
            add_range(start, end, element_count, range);
            if (effective_exclude) {
                for (int index : range)
                    selected.erase(index);
            } else {
                for (int index : range)
                    selected.insert(index);
            }
            return true;
        } catch (...) {
            return false;
        }
    }

    try {
        const int index = std::stoi(body);
        if (effective_exclude)
            selected.erase(index);
        else
            add_index(index, element_count, selected);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace

bool parse_element_pattern(const std::string& pattern,
                           int element_count,
                           std::unordered_set<int>& selected)
{
    const std::string trimmed = trim_copy(pattern);
    if (trimmed.empty() || element_count <= 0)
        return false;

    if (trimmed == "*") {
        for (int index = 0; index < element_count; ++index)
            selected.insert(index);
        return true;
    }
    if (trimmed == "!*") {
        selected.clear();
        return true;
    }

    const auto tokens = split_tokens(trimmed);
    if (tokens.empty())
        return false;

  for (const auto& token : tokens)
        parse_single_token(token, element_count, false, selected);
    return !selected.empty();
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
