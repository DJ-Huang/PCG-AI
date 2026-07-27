#pragma once

#include <string>
#include <unordered_set>

namespace pcg::internal::geometry {

/// Parse Houdini-style element number patterns and ranges into index sets.
/// Out-of-range indices are ignored; invalid tokens are skipped without error.

/// Evaluate a Houdini-style element pattern into a fresh index set.
/// `!*` matches nothing; negated tokens (`!0`, `!1-3`) are relative to all elements.
bool compute_element_pattern(const std::string& pattern,
                             int element_count,
                             std::unordered_set<int>& selected);

/// Union the indices matched by pattern into selected.
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
