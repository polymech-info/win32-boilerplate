#pragma once

#include <string>
#include <vector>

namespace polymech {

/// Fetch all rows from the "pages" table.
/// Returns raw JSON array string from Supabase.
std::string fetch_pages();

/// Fetch pages with a specific select clause and optional filter.
std::string fetch_pages(const std::string &select,
                        const std::string &filter = "", int limit = 0);

} // namespace polymech
