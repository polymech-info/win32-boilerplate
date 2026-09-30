#pragma once

#include <string>
#include <vector>

namespace postgres {

/// Supabase connection configuration.
struct Config {
  std::string supabase_url;
  std::string supabase_key;
};

/// Initialize the Supabase client with URL and API key.
void init(const Config &config);

/// Ping the Supabase REST API. Returns "ok" on success, error message on
/// failure.
std::string ping();

/// Query a table via the PostgREST API.
/// Returns the raw JSON response body.
/// @param table    Table name (e.g. "profiles")
/// @param select   Comma-separated columns (e.g. "id,username"), or "*"
/// @param filter   PostgREST filter (e.g. "id=eq.abc"), or "" for no filter
/// @param limit    Max rows (0 = no limit)
std::string query(const std::string &table, const std::string &select = "*",
                  const std::string &filter = "", int limit = 0);

/// Insert a row into a table. Body is a JSON object string.
/// Returns the created row as JSON.
std::string insert(const std::string &table, const std::string &json_body);

/// Upsert a row into a table. Body is a JSON array or object string.
/// Returns the upserted array as JSON.
std::string upsert(const std::string &table, const std::string &json_body, const std::string &on_conflict = "");

/// Update rows in a table. Body is a JSON object string.
/// Returns the updated rows as JSON.
std::string update(const std::string &table, const std::string &json_body, const std::string &filter);

/// Delete rows from a table.
/// Returns the deleted rows as JSON.
std::string del(const std::string &table, const std::string &filter);

} // namespace postgres
