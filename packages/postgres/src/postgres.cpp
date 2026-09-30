#include "postgres/postgres.h"
#include "http/http.h"
#include "logger/logger.h"
#include "json/json.h"

#include <curl/curl.h>
#include <stdexcept>

namespace postgres {

static Config s_config;
static bool s_initialized = false;

void init(const Config &config) {
  s_config = config;
  s_initialized = true;
  logger::debug("postgres::init → " + config.supabase_url);
}

static void ensure_init() {
  if (!s_initialized) {
    throw std::runtime_error("postgres::init() must be called first");
  }
}

/// Build the REST URL for a table query.
static std::string build_url(const std::string &table,
                             const std::string &select,
                             const std::string &filter, int limit) {
  std::string url = s_config.supabase_url + "/rest/v1/" + table;
  url += "?select=" + select;
  if (!filter.empty()) {
    url += "&" + filter;
  }
  if (limit > 0) {
    url += "&limit=" + std::to_string(limit);
  }
  return url;
}

/// Make an authenticated GET request to the Supabase REST API.
static http::Response supabase_get(const std::string &url) {
  // We need custom headers, so we use curl directly
  CURL *curl = curl_easy_init();
  http::Response resp{};
  if (!curl) {
    resp.status_code = -1;
    resp.body = "curl_easy_init failed";
    return resp;
  }

  struct curl_slist *headers = nullptr;
  headers =
      curl_slist_append(headers, ("apikey: " + s_config.supabase_key).c_str());
  headers = curl_slist_append(
      headers, ("Authorization: Bearer " + s_config.supabase_key).c_str());

  auto write_cb = [](void *contents, size_t size, size_t nmemb, void *userp) {
    auto *out = static_cast<std::string *>(userp);
    out->append(static_cast<char *>(contents), size * nmemb);
    return size * nmemb;
  };

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(
      curl, CURLOPT_WRITEFUNCTION,
      static_cast<size_t (*)(void *, size_t, size_t, void *)>(+write_cb));
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

  CURLcode res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    resp.status_code = -1;
    resp.body = curl_easy_strerror(res);
  } else {
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.status_code);
  }

  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  return resp;
}

/// Make an authenticated request with a JSON body (POST, PATCH, DELETE).
static http::Response supabase_request(const std::string &method,
                                       const std::string &url,
                                       const std::string &body,
                                       const std::string &prefer_header) {
  CURL *curl = curl_easy_init();
  http::Response resp{};
  if (!curl) {
    resp.status_code = -1;
    resp.body = "curl_easy_init failed";
    return resp;
  }

  struct curl_slist *headers = nullptr;
  if (!body.empty()) {
    headers = curl_slist_append(headers, "Content-Type: application/json");
  }
  if (!prefer_header.empty()) {
    headers = curl_slist_append(headers, ("Prefer: " + prefer_header).c_str());
  }
  headers =
      curl_slist_append(headers, ("apikey: " + s_config.supabase_key).c_str());
  headers = curl_slist_append(
      headers, ("Authorization: Bearer " + s_config.supabase_key).c_str());

  auto write_cb = [](void *contents, size_t size, size_t nmemb, void *userp) {
    auto *out = static_cast<std::string *>(userp);
    out->append(static_cast<char *>(contents), size * nmemb);
    return size * nmemb;
  };

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  if (!body.empty()) {
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
  }
  curl_easy_setopt(
      curl, CURLOPT_WRITEFUNCTION,
      static_cast<size_t (*)(void *, size_t, size_t, void *)>(+write_cb));
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

  CURLcode res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    resp.status_code = -1;
    resp.body = curl_easy_strerror(res);
  } else {
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.status_code);
  }

  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  return resp;
}

std::string ping() {
  ensure_init();
  // Lightweight check: query profiles with limit=0 to verify connectivity
  auto resp = supabase_get(s_config.supabase_url +
                           "/rest/v1/profiles?select=id&limit=0");
  if (resp.status_code >= 200 && resp.status_code < 300) {
    logger::info("postgres::ping → ok (HTTP " +
                 std::to_string(resp.status_code) + ")");
    return "ok";
  }
  logger::error("postgres::ping → HTTP " + std::to_string(resp.status_code) +
                ": " + resp.body);
  return "error: HTTP " + std::to_string(resp.status_code);
}

std::string query(const std::string &table, const std::string &select,
                  const std::string &filter, int limit) {
  ensure_init();
  auto url = build_url(table, select, filter, limit);
  logger::debug("postgres::query → " + url);

  auto resp = supabase_get(url);
  if (resp.status_code >= 200 && resp.status_code < 300) {
    return resp.body;
  }
  logger::error("postgres::query → HTTP " + std::to_string(resp.status_code) +
                ": " + resp.body);
  return resp.body;
}

std::string insert(const std::string &table, const std::string &json_body) {
  ensure_init();
  auto url = s_config.supabase_url + "/rest/v1/" + table;
  logger::debug("postgres::insert → " + url);

  auto resp = supabase_request("POST", url, json_body, "return=representation");
  if (resp.status_code >= 200 && resp.status_code < 300) {
    return resp.body;
  }
  logger::error("postgres::insert → HTTP " + std::to_string(resp.status_code) +
                ": " + resp.body);
  return resp.body;
}

std::string upsert(const std::string &table, const std::string &json_body, const std::string &on_conflict) {
  ensure_init();
  auto url = s_config.supabase_url + "/rest/v1/" + table;
  if (!on_conflict.empty()) {
    url += "?on_conflict=" + on_conflict;
  }
  logger::debug("postgres::upsert → " + url);

  auto resp = supabase_request("POST", url, json_body, "return=minimal, resolution=merge-duplicates");
  if (resp.status_code >= 200 && resp.status_code < 300) {
    return resp.body;
  }
  logger::error("postgres::upsert → HTTP " + std::to_string(resp.status_code) +
                ": " + resp.body);
  return resp.body;
}

std::string update(const std::string &table, const std::string &json_body, const std::string &filter) {
  ensure_init();
  auto url = s_config.supabase_url + "/rest/v1/" + table;
  if (!filter.empty()) {
    url += "?" + filter;
  }
  logger::debug("postgres::update → " + url);

  auto resp = supabase_request("PATCH", url, json_body, "return=representation");
  if (resp.status_code >= 200 && resp.status_code < 300) {
    return resp.body;
  }
  logger::error("postgres::update → HTTP " + std::to_string(resp.status_code) +
                ": " + resp.body);
  return resp.body;
}

std::string del(const std::string &table, const std::string &filter) {
  ensure_init();
  auto url = s_config.supabase_url + "/rest/v1/" + table;
  if (!filter.empty()) {
    url += "?" + filter;
  }
  logger::debug("postgres::del → " + url);

  auto resp = supabase_request("DELETE", url, "", "return=representation");
  if (resp.status_code >= 200 && resp.status_code < 300) {
    return resp.body;
  }
  logger::error("postgres::del → HTTP " + std::to_string(resp.status_code) +
                ": " + resp.body);
  return resp.body;
}

} // namespace postgres
