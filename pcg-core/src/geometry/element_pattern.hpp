#pragma once

#include <string>
#include <unordered_set>

namespace pcg::internal::geometry {

/// Parse Houdini-style element number patterns and ranges into index sets.
/// Out-of-range indices are ignored; invalid tokens are skipped without error.

bool parse_element_pattern(const std::string& pattern,
                           int element_count,
                           std::unordered_set<int>& selected);

bool parse_element_range(int start,
                         int end,
                         int select_of,
                         int select_offset,
                         int element_count,
                         std::unordered_set<int>& selected);

} // namespace pcg::internal::geometry
