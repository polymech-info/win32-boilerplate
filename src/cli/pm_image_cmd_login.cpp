#include "pm_image_cmd_login.hpp"

#include "pm_image_cli_state.hpp"
#include <CLI/CLI.hpp>

#include <curl/curl.h>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <picosha2.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>


#include "constants.hpp"
#include "lib/pm_service_upload.hpp"
#include "lib/pm_zitadel_oauth.hpp"
#include "core/settings_runtime.hpp"

#if defined(_WIN32)
#include <bcrypt.h>
#include <shellapi.h>
#include <windows.h>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "Shell32.lib")
#include "win/settings_store.hpp"
#include <filesystem>

#else
#include <fcntl.h>
#include <filesystem>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#endif

namespace {

constexpr const char *k_log_pfx = "[zitadel-login] ";

void zlog(const std::string &line) { std::cerr << k_log_pfx << line << '\n'; }

std::string getenv_trimmed(const char *key) {
  const char *v = std::getenv(key);
  if (!v)
    return {};
  std::string s(v);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' ||
                        s.back() == '\r' || s.back() == '\n'))
    s.pop_back();
  size_t i = 0;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
    ++i;
  return s.substr(i);
}

std::string trim_slash(std::string s) {
  while (!s.empty() && (s.back() == '/' || s.back() == '\\'))
    s.pop_back();
  return s;
}

size_t curl_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
  auto *out = static_cast<std::string *>(userdata);
  out->append(ptr, size * nmemb);
  return size * nmemb;
}

bool http_get_curl(const std::string &url, long &http_code, std::string &body,
                   std::string &err) {
  http_code = 0;
  body.clear();
  CURL *curl = curl_easy_init();
  if (!curl) {
    err = "curl_easy_init failed";
    return false;
  }
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
  CURLcode cc = curl_easy_perform(curl);
  if (cc != CURLE_OK) {
    err = std::string("GET failed: ") + curl_easy_strerror(cc);
    curl_easy_cleanup(curl);
    return false;
  }
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
  curl_easy_cleanup(curl);
  return true;
}

bool http_get_bearer_curl(const std::string &url,
                          const std::string &bearer_token, long &http_code,
                          std::string &body, std::string &err) {
  http_code = 0;
  body.clear();
  CURL *curl = curl_easy_init();
  if (!curl) {
    err = "curl_easy_init failed";
    return false;
  }
  struct curl_slist *hdr = nullptr;
  const std::string auth = "Authorization: Bearer " + bearer_token;
  hdr = curl_slist_append(hdr, auth.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdr);
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
  const CURLcode cc = curl_easy_perform(curl);
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
  curl_slist_free_all(hdr);
  curl_easy_cleanup(curl);
  if (cc != CURLE_OK) {
    err = std::string("GET failed: ") + curl_easy_strerror(cc);
    return false;
  }
  return true;
}

bool http_post_form_curl(const std::string &url, const std::string &form_body,
                         long &http_code, std::string &resp, std::string &err) {
  http_code = 0;
  resp.clear();
  CURL *curl = curl_easy_init();
  if (!curl) {
    err = "curl_easy_init failed";
    return false;
  }
  struct curl_slist *hdr = curl_slist_append(
      nullptr, "Content-Type: application/x-www-form-urlencoded");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdr);
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, form_body.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,
                   static_cast<long>(form_body.size()));
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
  CURLcode cc = curl_easy_perform(curl);
  curl_slist_free_all(hdr);
  if (cc != CURLE_OK) {
    err = std::string("POST failed: ") + curl_easy_strerror(cc);
    curl_easy_cleanup(curl);
    return false;
  }
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
  curl_easy_cleanup(curl);
  return true;
}

std::string base64url_encode_bytes(const unsigned char *data, size_t len,
                                   bool strip_padding = true) {
  static const char *tbl =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((len + 2) / 3 * 4);
  size_t i = 0;
  for (; i + 3 <= len; i += 3) {
    uint32_t n = (static_cast<uint32_t>(data[i]) << 16) |
                 (static_cast<uint32_t>(data[i + 1]) << 8) | data[i + 2];
    out.push_back(tbl[(n >> 18) & 63]);
    out.push_back(tbl[(n >> 12) & 63]);
    out.push_back(tbl[(n >> 6) & 63]);
    out.push_back(tbl[n & 63]);
  }
  const size_t rem = len - i;
  if (rem == 1) {
    uint32_t n = static_cast<uint32_t>(data[i]) << 16;
    out.push_back(tbl[(n >> 18) & 63]);
    out.push_back(tbl[(n >> 12) & 63]);
    if (!strip_padding)
      out += "==";
  } else if (rem == 2) {
    uint32_t n = (static_cast<uint32_t>(data[i]) << 16) |
                 (static_cast<uint32_t>(data[i + 1]) << 8);
    out.push_back(tbl[(n >> 18) & 63]);
    out.push_back(tbl[(n >> 12) & 63]);
    out.push_back(tbl[(n >> 6) & 63]);
    if (!strip_padding)
      out.push_back('=');
  }
  for (char &c : out) {
    if (c == '+')
      c = '-';
    else if (c == '/')
      c = '_';
  }
  if (strip_padding) {
    while (!out.empty() && out.back() == '=')
      out.pop_back();
  }
  return out;
}

bool random_bytes(void *dst, size_t n) {
#if defined(_WIN32)
  return BCRYPT_SUCCESS(BCryptGenRandom(nullptr, static_cast<PUCHAR>(dst),
                                        static_cast<ULONG>(n),
                                        BCRYPT_USE_SYSTEM_PREFERRED_RNG));
#else
  int fd = open("/dev/urandom", O_RDONLY);
  if (fd < 0)
    return false;
  size_t off = 0;
  auto *p = static_cast<unsigned char *>(dst);
  while (off < n) {
    ssize_t r = read(fd, p + off, n - off);
    if (r <= 0) {
      close(fd);
      return false;
    }
    off += static_cast<size_t>(r);
  }
  close(fd);
  return true;
#endif
}

/** RFC 7636 code_verifier: 43-128 chars from [A-Z] [a-z] [0-9] "-" "." "_" */
std::string make_code_verifier() {
  static const char cs[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
  constexpr size_t k_len = 64;
  std::string out;
  out.resize(k_len);
  std::vector<unsigned char> rnd(k_len);
  if (!random_bytes(rnd.data(), rnd.size())) {
    zlog("WARNING: OS RNG failed; falling back to std::random_device (weaker) "
         "for code_verifier.");
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<unsigned> dis(0, 255);
    for (size_t i = 0; i < k_len; ++i)
      rnd[i] = static_cast<unsigned char>(dis(gen));
  }
  for (size_t i = 0; i < k_len; ++i)
    out[i] = cs[rnd[i] % (sizeof(cs) - 1)];
  return out;
}

std::string make_code_challenge_s256(const std::string &verifier) {
  std::vector<unsigned char> hash(picosha2::k_digest_size);
  picosha2::hash256(verifier.begin(), verifier.end(), hash.begin(), hash.end());
  return base64url_encode_bytes(hash.data(), hash.size(), true);
}

std::string random_state() {
  std::vector<unsigned char> b(16);
  if (!random_bytes(b.data(), b.size())) {
    std::random_device rd;
    for (auto &x : b)
      x = static_cast<unsigned char>(rd());
  }
  std::ostringstream oss;
  oss << std::hex;
  for (unsigned char x : b)
    oss << std::setw(2) << std::setfill('0') << static_cast<int>(x);
  return oss.str();
}

bool url_encode_form_component(CURL *curl, const std::string &in,
                               std::string &out, std::string &err) {
  char *esc = curl_easy_escape(curl, in.c_str(), static_cast<int>(in.size()));
  if (!esc) {
    err = "curl_easy_escape failed";
    return false;
  }
  out.assign(esc);
  curl_free(esc);
  return true;
}

nlohmann::json try_parse_json(const std::string &s) {
  try {
    return nlohmann::json::parse(s);
  } catch (...) {
    return nlohmann::json();
  }
}

bool looks_like_uuid(const std::string &sub) {
  /* Rough: 8-4-4-4-12 hex */
  return sub.size() == 36 && sub[8] == '-' && sub[13] == '-' &&
         sub[18] == '-' && sub[23] == '-';
}

bool looks_like_zitadel_numeric_sub(const std::string &sub) {
  if (sub.empty() || sub.size() > 24)
    return false;
  for (char c : sub) {
    if (c < '0' || c > '9')
      return false;
  }
  return true;
}

void log_jwt_payload_verbose(const nlohmann::json &p) {
  zlog("--- JWT payload (unverified; for diagnostics only) ---");
  const std::string sub = p.value("sub", std::string());
  zlog(std::string("  iss: ") + p.value("iss", std::string()));
  zlog(std::string("  sub: ") + sub);
  if (p.contains("aud")) {
    if (p["aud"].is_string())
      zlog(std::string("  aud: ") + p["aud"].get<std::string>());
    else if (p["aud"].is_array()) {
      zlog("  aud (array):");
      for (const auto &a : p["aud"])
        if (a.is_string())
          zlog(std::string("    - ") + a.get<std::string>());
    }
  }
  if (p.contains("exp") && p["exp"].is_number())
    zlog(std::string("  exp: ") + std::to_string(p["exp"].get<std::int64_t>()) +
         " (unix seconds)");
  if (p.contains("email"))
    zlog(std::string("  email: ") + p.value("email", std::string()));
  if (p.contains("preferred_username"))
    zlog(std::string("  preferred_username: ") +
         p.value("preferred_username", std::string()));
  if (p.contains("name"))
    zlog(std::string("  name: ") + p.value("name", std::string()));
  if (p.contains("client_id"))
    zlog(std::string("  client_id: ") + p.value("client_id", std::string()));

  zlog("--- `sub` vs app user id (server `ref/pm-pics/zitadel.ts`) ---");
  zlog("  Access-token JWT `sub` is what ZITADEL issues (often a numeric "
       "string for human users).");
  zlog("  After verifyZitadelAccessToken(), the service calls "
       "resolveAppUserId(zitadelUser.id, email).");
  zlog("  When that returns an app UUID, the in-memory User.id becomes the "
       "UUID and user_metadata.zitadel_sub keeps the original JWT sub.");
  zlog("  So: API / DB \"user id\" may NOT equal raw JWT `sub` - compare logs "
       "to AuthZ / browser `profile.sub` accordingly.");

  if (looks_like_zitadel_numeric_sub(sub)) {
    zlog("  This token's `sub` looks like a ZITADEL numeric subject - expect "
         "resolveAppUserId() to map it to profiles.user_id (UUID) when "
         "configured.");
  } else if (looks_like_uuid(sub)) {
    zlog("  This token's `sub` looks like a UUID - may already be the app user "
         "id or a different IdP layout; still verify against your DB.");
  } else {
    zlog("  This token's `sub` is neither all-digit nor UUID-shaped - treat "
         "mapping as deployment-specific.");
  }
}

bool base64url_segment_to_utf8(const std::string &segment,
                               std::string &utf8_out, std::string &err) {
  signed char T[256];
  std::memset(T, -1, sizeof(T));
  static const char *alpha =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  for (int i = 0; alpha[i]; ++i)
    T[static_cast<unsigned char>(alpha[i])] = static_cast<signed char>(i);

  std::string b64 = segment;
  for (char &c : b64) {
    if (c == '-')
      c = '+';
    else if (c == '_')
      c = '/';
  }
  while (b64.size() % 4)
    b64.push_back('=');

  std::vector<unsigned char> raw;
  raw.reserve(b64.size() * 3 / 4);
  int val = 0;
  int valb = -8;
  for (unsigned char c : b64) {
    if (c == '=')
      break;
    const int d = T[c];
    if (d < 0) {
      err = "invalid base64url character in JWT segment";
      return false;
    }
    val = (val << 6) + d;
    valb += 6;
    if (valb >= 0) {
      raw.push_back(static_cast<unsigned char>((val >> valb) & 0xFF));
      valb -= 8;
    }
  }
  utf8_out.assign(raw.begin(), raw.end());
  return true;
}

bool decode_jwt_payload_part(const std::string &compact_jwt,
                             nlohmann::json &payload_out, std::string &err) {
  const size_t d1 = compact_jwt.find('.');
  const size_t d2 = compact_jwt.find('.', d1 == std::string::npos ? 0 : d1 + 1);
  if (d1 == std::string::npos || d2 == std::string::npos || d2 <= d1 + 1) {
    err = "not a compact JWT (expected two '.' separators)";
    return false;
  }
  const std::string seg = compact_jwt.substr(d1 + 1, d2 - d1 - 1);
  std::string utf8;
  if (!base64url_segment_to_utf8(seg, utf8, err))
    return false;
  try {
    payload_out = nlohmann::json::parse(utf8);
    return true;
  } catch (const std::exception &e) {
    err = std::string("JSON parse payload: ") + e.what();
    return false;
  }
}

bool open_system_browser(const std::string &url, std::string &err) {
#if defined(_WIN32)
  const HINSTANCE hi = ShellExecuteA(nullptr, "open", url.c_str(), nullptr,
                                     nullptr, SW_SHOWNORMAL);
  if (reinterpret_cast<std::intptr_t>(hi) <= 32) {
    err = "ShellExecuteA failed (browser not launched?)";
    return false;
  }
  return true;
#else
  pid_t pid = fork();
  if (pid < 0) {
    err = "fork failed";
    return false;
  }
  if (pid == 0) {
#if defined(__APPLE__)
    execlp("open", "open", url.c_str(), static_cast<char *>(nullptr));
#else
    execlp("xdg-open", "xdg-open", url.c_str(), static_cast<char *>(nullptr));
    execlp("open", "open", url.c_str(), static_cast<char *>(nullptr));
#endif
    _exit(127);
  }
  return true;
#endif
}

int run_probe(const std::string &issuer_in, const std::string &client_id,
              std::string &err) {
  const std::string issuer = trim_slash(issuer_in);
  zlog("probe: issuer (normalized) = " + issuer);
  zlog("probe: client_id (public)   = " +
       (client_id.empty() ? "(not set)" : client_id));

  const std::string disc_url = issuer + "/.well-known/openid-configuration";
  zlog("probe: GET " + disc_url);
  long code = 0;
  std::string body;
  if (!http_get_curl(disc_url, code, body, err))
    return 1;
  zlog(std::string("probe: discovery HTTP ") + std::to_string(code) +
       ", body bytes=" + std::to_string(body.size()));
  if (code < 200 || code >= 300) {
    err = "discovery non-2xx";
    return 1;
  }
  nlohmann::json j = try_parse_json(body);
  if (j.is_null() || !j.is_object()) {
    err = "discovery JSON parse failed";
    return 1;
  }
  const std::string auth_ep = j.value("authorization_endpoint", std::string());
  const std::string token_ep = j.value("token_endpoint", std::string());
  const std::string jwks_uri = j.value("jwks_uri", std::string());
  zlog(std::string("probe: authorization_endpoint = ") + auth_ep);
  zlog(std::string("probe: token_endpoint         = ") + token_ep);
  zlog(std::string("probe: jwks_uri               = ") + jwks_uri);
  if (jwks_uri.empty()) {
    err = "discovery missing jwks_uri";
    return 1;
  }
  zlog("probe: GET jwks_uri ...");
  long jwks_code = 0;
  std::string jwks_body;
  if (!http_get_curl(jwks_uri, jwks_code, jwks_body, err))
    return 1;
  zlog(std::string("probe: JWKS HTTP ") + std::to_string(jwks_code) +
       ", body bytes=" + std::to_string(jwks_body.size()));
  if (jwks_code < 200 || jwks_code >= 300) {
    err = "JWKS non-2xx";
    return 1;
  }
  nlohmann::json jwks = try_parse_json(jwks_body);
  if (!jwks.is_object() || !jwks.contains("keys")) {
    zlog("probe: WARNING: JWKS JSON has no top-level \"keys\" array "
         "(unexpected for ZITADEL).");
  } else {
    zlog(std::string("probe: JWKS key count = ") +
         std::to_string(jwks["keys"].size()));
  }
  zlog("probe: OK (discovery + JWKS reachable).");
  return 0;
}

int run_decode_jwt(std::string token, std::string &err) {
  while (!token.empty() && (token.front() == ' ' || token.front() == '\t' ||
                            token.front() == '"' || token.front() == '\''))
    token.erase(token.begin());
  while (!token.empty() && (token.back() == ' ' || token.back() == '\t' ||
                            token.back() == '"' || token.back() == '\''))
    token.pop_back();
  if (token.empty()) {
    err = "no JWT (pass as --decode-jwt=<token> or set "
          "ZITADEL_TEST_ACCESS_TOKEN)";
    return 1;
  }
  zlog("decode-jwt: input length = " + std::to_string(token.size()) + " chars");
  nlohmann::json payload;
  if (!decode_jwt_payload_part(token, payload, err))
    return 1;
  log_jwt_payload_verbose(payload);
  zlog("decode-jwt: OK.");
  return 0;
}

} // namespace

int pm_image_cmd_login(CLI::App & /*app*/, PmImageCliState &st) {
  std::string err;

  std::string issuer =
      trim_slash(!st.login_issuer.empty() ? st.login_issuer
                                          : getenv_trimmed("ZITADEL_ISSUER"));
  if (issuer.empty())
    issuer = trim_slash(getenv_trimmed("VITE_ZITADEL_AUTHORITY"));
  std::string client_id = !st.login_client_id.empty()
                              ? st.login_client_id
                              : getenv_trimmed("ZITADEL_OIDC_CLIENT_ID");
  if (client_id.empty())
    client_id = getenv_trimmed("VITE_ZITADEL_CLIENT_ID");
  if (issuer.empty() && pm::k_pixlwiz_zitadel_authority[0] != '\0')
    issuer = trim_slash(std::string(pm::k_pixlwiz_zitadel_authority));
  if (client_id.empty() && pm::k_pixlwiz_zitadel_client_id[0] != '\0')
    client_id = std::string(pm::k_pixlwiz_zitadel_client_id);

  zlog("command: login (verbose logging; stderr lines use the prefix above)");

  if (st.login_decode_opt && st.login_decode_opt->count() > 0) {
    std::string tok = st.login_decode_jwt_value;
    if (tok.empty())
      tok = getenv_trimmed("ZITADEL_TEST_ACCESS_TOKEN");
    return run_decode_jwt(std::move(tok), err) == 0
               ? 0
               : (std::cerr << k_log_pfx << err << "\n", 1);
  }

  if (st.login_probe) {
    if (issuer.empty()) {
      std::cerr
          << k_log_pfx
          << "Set ZITADEL_ISSUER or VITE_ZITADEL_AUTHORITY (or --issuer).\n";
      return 1;
    }
    const int pr = run_probe(issuer, client_id, err);
    if (pr != 0)
      std::cerr << k_log_pfx << err << "\n";
    return pr;
  }

  if (issuer.empty() || client_id.empty()) {
    std::cerr << k_log_pfx
              << "Need issuer + client id: set ZITADEL_ISSUER + "
                 "ZITADEL_OIDC_CLIENT_ID "
                 "(or VITE_* fallbacks), or pass --issuer / --client-id, or "
                 "rebuild with non-empty "
                 "defaults in src/constants.hpp (Pixlwiz menu login also "
                 "passes those flags when env "
                 "is unset).\n";
    return 1;
  }

  /* Interactive PKCE (authorization code + loopback callback) */
  const std::string disc_url = issuer + "/.well-known/openid-configuration";
  zlog("interactive: GET discovery " + disc_url);
  long dcode = 0;
  std::string dbody;
  if (!http_get_curl(disc_url, dcode, dbody, err)) {
    std::cerr << k_log_pfx << err << "\n";
    return 1;
  }
  if (dcode < 200 || dcode >= 300) {
    std::cerr << k_log_pfx << "discovery HTTP " << dcode << "\n";
    return 1;
  }
  nlohmann::json disc = try_parse_json(dbody);
  const std::string authorization_endpoint =
      disc.value("authorization_endpoint", std::string());
  const std::string token_endpoint =
      disc.value("token_endpoint", std::string());
  if (authorization_endpoint.empty() || token_endpoint.empty()) {
    std::cerr << k_log_pfx
              << "discovery missing authorization_endpoint or token_endpoint\n";
    return 1;
  }
  zlog("interactive: authorization_endpoint = " + authorization_endpoint);
  zlog("interactive: token_endpoint         = " + token_endpoint);

  const std::string verifier = make_code_verifier();
  const std::string challenge = make_code_challenge_s256(verifier);
  const std::string state = random_state();
  zlog("interactive: PKCE code_verifier length = " +
       std::to_string(verifier.size()) +
       " (keep secret until exchange completes)");
  zlog("interactive: PKCE code_challenge method = S256 (length " +
       std::to_string(challenge.size()) + ")");

  int listen_port = 0;
  httplib::Server svr;
  std::promise<std::string> code_promise;
  std::future<std::string> code_future = code_promise.get_future();
  std::once_flag code_delivered;

  svr.Get("/callback", [&](const httplib::Request &req,
                           httplib::Response &res) {
    zlog(std::string("callback: request target = ") + req.target);
    const std::string q_state = req.get_param_value("state");
    const std::string code = req.get_param_value("code");
    const std::string oerr = req.get_param_value("error");
    const std::string desc = req.get_param_value("error_description");
    if (!oerr.empty()) {
      zlog(std::string("callback: OAuth error=") + oerr +
           " description=" + desc);
      std::call_once(code_delivered, [&code_promise] {
        code_promise.set_value(std::string());
      });
      res.set_content(
          "<html><body>Login failed. You can close this tab.</body></html>",
          "text/html");
      return;
    }
    if (q_state != state) {
      zlog("callback: state mismatch (got len " +
           std::to_string(q_state.size()) + ", expected len " +
           std::to_string(state.size()) + ")");
      res.set_content(
          "<html><body>Invalid or expired login state. Close this tab and "
          "continue in the latest sign-in tab.</body></html>",
          "text/html");
      return;
    }
    if (code.empty()) {
      zlog("callback: missing code param");
      std::call_once(code_delivered, [&code_promise] {
        code_promise.set_value(std::string());
      });
      res.set_content("<html><body>Missing code. Close this tab.</body></html>",
                      "text/html");
      return;
    }
    zlog("callback: received authorization code, len=" +
         std::to_string(code.size()));
    std::call_once(code_delivered,
                   [&code_promise, code] { code_promise.set_value(code); });
    res.set_content("<html><body>Sign-in complete. You can close this tab and "
                    "return to the terminal.</body></html>",
                    "text/html");
  });

  int port_first = st.login_oauth_port;
  if (port_first < 1024)
    port_first = 1024;
  if (port_first > 65535)
    port_first = 65535;
  const int port_last = port_first > 65535 - 50 ? 65535 : port_first + 50;

  std::thread listen_th;
  for (int p = port_first; p <= port_last; ++p) {
    if (svr.bind_to_port("127.0.0.1", p)) {
      listen_port = p;
      break;
    }
  }
  if (listen_port <= 0) {
    std::cerr << k_log_pfx
              << "Could not bind 127.0.0.1 for OAuth callback (tried ports "
              << port_first << "-" << port_last << ").\n";
    return 1;
  }
  const std::string redirect_uri =
      "http://127.0.0.1:" + std::to_string(listen_port) + "/callback";
  zlog(
      "interactive: "
      "----------------------------------------------------------------------");
  zlog("interactive: redirect_uri used on this run (must match ZITADEL app "
       "config byte-for-byte):");
  zlog("interactive:   " + redirect_uri);
  if (listen_port != port_first)
    zlog("interactive: NOTE - port " + std::to_string(port_first) +
         " was busy; a higher port was used. Add THIS URI to ZITADEL, or pass "
         "--oauth-port with a free port.");
  zlog("interactive: ZITADEL Console -> your Project -> Applications -> <this "
       "client> -> Redirect settings");
  zlog("interactive: Add Native / User Agent redirect URI exactly as above "
       "(use 127.0.0.1, not localhost).");
  zlog("interactive: If the authorize page shows invalid_request / "
       "redirect_uri missing, the URI above is not in that list.");
  zlog(
      "interactive: "
      "----------------------------------------------------------------------");

  listen_th = std::thread([&svr]() {
    zlog("listen: httplib thread starting (127.0.0.1 callback)");
    (void)svr.listen_after_bind();
  });
  svr.wait_until_ready();
  zlog("listen: server wait_until_ready() returned (callback listener up)");

  CURL *curl_tmp = curl_easy_init();
  if (!curl_tmp) {
    svr.stop();
    listen_th.join();
    std::cerr << k_log_pfx << "curl init failed\n";
    return 1;
  }
  std::string enc_client, enc_redirect, enc_scope, enc_state, enc_challenge;
  if (!url_encode_form_component(curl_tmp, client_id, enc_client, err) ||
      !url_encode_form_component(curl_tmp, redirect_uri, enc_redirect, err) ||
      !url_encode_form_component(
          curl_tmp, "openid profile email offline_access", enc_scope, err) ||
      !url_encode_form_component(curl_tmp, state, enc_state, err) ||
      !url_encode_form_component(curl_tmp, challenge, enc_challenge, err)) {
    curl_easy_cleanup(curl_tmp);
    svr.stop();
    listen_th.join();
    std::cerr << k_log_pfx << err << "\n";
    return 1;
  }
  curl_easy_cleanup(curl_tmp);

  const std::string auth_url =
      authorization_endpoint + "?response_type=code&client_id=" + enc_client +
      "&redirect_uri=" + enc_redirect + "&scope=" + enc_scope +
      "&state=" + enc_state + "&code_challenge=" + enc_challenge +
      "&code_challenge_method=S256";

  zlog("interactive: full authorize URL length = " +
       std::to_string(auth_url.size()));
  if (st.login_no_browser) {
    std::cerr << k_log_pfx << "--no-browser: open this URL manually:\n\n"
              << auth_url << "\n\n";
    std::cerr << k_log_pfx
              << "--no-browser: code_verifier (SECRET - paste only into a "
                 "secure scratchpad for dev):\n\n"
              << verifier << "\n\n";
  } else {
    zlog("interactive: launching system browser ...");
    if (!open_system_browser(auth_url, err)) {
      std::cerr << k_log_pfx << err << "\n";
      svr.stop();
      listen_th.join();
      return 1;
    }
  }

  zlog("interactive: waiting for browser redirect (timeout 300s) ...");
  const auto status = code_future.wait_for(std::chrono::seconds(300));
  if (status == std::future_status::timeout)
    zlog("interactive: wait_for returned timeout (will stop listener and "
         "abandon OAuth code wait).");
  svr.stop();
  if (listen_th.joinable())
    listen_th.join();
  if (code_future.wait_for(std::chrono::seconds(0)) !=
      std::future_status::ready) {
    try {
      code_promise.set_value(std::string());
    } catch (const std::future_error &) {
      /* callback already satisfied the promise */
    }
  }

  const std::string auth_code = code_future.get();
  if (auth_code.empty()) {
    if (status == std::future_status::timeout)
      std::cerr << k_log_pfx
                << "Timed out waiting for /callback (no redirect received).\n";
    else
      std::cerr << k_log_pfx << "Login aborted or error (no code).\n";
    return 1;
  }

  zlog("interactive: exchanging code at token_endpoint ...");
  CURL *curl_post = curl_easy_init();
  if (!curl_post) {
    std::cerr << k_log_pfx << "curl init failed\n";
    return 1;
  }
  std::string enc_code, enc_verifier;
  if (!url_encode_form_component(curl_post, auth_code, enc_code, err) ||
      !url_encode_form_component(curl_post, verifier, enc_verifier, err)) {
    curl_easy_cleanup(curl_post);
    std::cerr << k_log_pfx << err << "\n";
    return 1;
  }
  curl_easy_cleanup(curl_post);

  const std::string form = std::string("grant_type=authorization_code&code=") +
                           enc_code + "&redirect_uri=" + enc_redirect +
                           "&client_id=" + enc_client +
                           "&code_verifier=" + enc_verifier;

  long tok_http = 0;
  std::string tok_body;
  if (!http_post_form_curl(token_endpoint, form, tok_http, tok_body, err)) {
    std::cerr << k_log_pfx << err << "\n";
    return 1;
  }
  zlog(std::string("interactive: token response HTTP ") +
       std::to_string(tok_http) + ", bytes=" + std::to_string(tok_body.size()));
  if (tok_http < 200 || tok_http >= 300) {
    std::cerr << k_log_pfx << "token error body: " << tok_body << "\n";
    return 1;
  }

  nlohmann::json tokj = try_parse_json(tok_body);
  if (!tokj.is_object() || !tokj.contains("access_token")) {
    std::cerr << k_log_pfx << "token JSON missing access_token\n";
    return 1;
  }
  const std::string access_token = tokj.value("access_token", std::string());
  zlog("interactive: received access_token, len=" +
       std::to_string(access_token.size()));
  if (tokj.contains("refresh_token"))
    zlog("interactive: received refresh_token, len=" +
         std::to_string(tokj["refresh_token"].get<std::string>().size()));
  else
    zlog("interactive: no refresh_token in response (enable refresh tokens + "
         "offline_access on the ZITADEL app if needed).");

  nlohmann::json payload;
  if (decode_jwt_payload_part(access_token, payload, err))
    log_jwt_payload_verbose(payload);
  else
    zlog("interactive: could not base64-decode access_token JWT payload: " +
         err);

  std::filesystem::path store_path = pm_zitadel_oauth_json_path(err);
  if (store_path.empty()) {
    std::cerr << k_log_pfx << "store path: " << err << "\n";
    return 1;
  }
  std::error_code ec;
  std::filesystem::create_directories(store_path.parent_path(), ec);
  nlohmann::json out = {
      {"issuer", issuer},
      {"client_id", client_id},
      {"obtained_at_unix", static_cast<std::int64_t>(std::time(nullptr))},
      {"access_token", access_token},
      {"token_type", tokj.value("token_type", std::string("Bearer"))},
      {"expires_in",
       tokj.contains("expires_in") && tokj["expires_in"].is_number()
           ? tokj["expires_in"].get<int>()
           : 0},
  };
  if (tokj.contains("refresh_token"))
    out["refresh_token"] = tokj["refresh_token"];
  if (tokj.contains("id_token"))
    out["id_token"] = tokj["id_token"];

  /* Same as pm-pics fetchUserIdentity: GET /api/me/identity -> { id, sub, roles
   * } (app UUID vs Zitadel sub). */
  const std::string api_base = pm_resolve_service_server_base("");
  if (api_base.empty()) {
    zlog("interactive: SERVER_URL / VITE_SERVER_IMAGE_API_URL / CLIENT_URL "
         "unset — skip GET /api/me/identity "
         "(app_user_id not saved). Set SERVER_URL to persist resolved UUID.");
  } else {
    const std::string id_url = api_base + "/api/me/identity";
    long id_http = 0;
    std::string id_body;
    zlog("interactive: GET /api/me/identity for app UUID (see pm-pics "
         "client-user fetchUserIdentity) ...");
    if (!http_get_bearer_curl(id_url, access_token, id_http, id_body, err)) {
      zlog("interactive: identity request failed (non-fatal): " + err);
    } else if (id_http >= 200 && id_http < 300) {
      try {
        const nlohmann::json idj = nlohmann::json::parse(id_body);
        if (idj.contains("id") && idj["id"].is_string()) {
          const std::string uid = idj["id"].get<std::string>();
          out["app_user_id"] = uid;
          zlog("interactive: app_user_id (profiles.user_id UUID) len=" +
               std::to_string(uid.size()));
        }
        if (idj.contains("sub") && idj["sub"].is_string()) {
          const std::string sub = idj["sub"].get<std::string>();
          out["zitadel_sub"] = sub;
          zlog("interactive: zitadel_sub (token subject / numeric sub) len=" +
               std::to_string(sub.size()));
        }
        if (idj.contains("roles") && idj["roles"].is_array())
          out["roles"] = idj["roles"];
      } catch (const std::exception &e) {
        zlog(std::string(
                 "interactive: identity JSON parse failed (non-fatal): ") +
             e.what());
      }
    } else {
      zlog("interactive: identity HTTP " + std::to_string(id_http) +
           " body=" + id_body.substr(0, 240));
    }
  }

  std::ofstream ofs(store_path, std::ios::binary | std::ios::trunc);
  if (!ofs) {
    std::cerr << k_log_pfx << "failed to write " << store_path.string() << "\n";
    return 1;
  }
  ofs << out.dump(2);
  ofs.close();
#if !defined(_WIN32)
  chmod(store_path.c_str(), S_IRUSR | S_IWUSR);
#endif
  zlog("interactive: wrote token bundle to " + store_path.string());
  std::cout << "Signed in. Tokens saved to " << store_path.string() << "\n";

  // Option A: mirror access_token into settings.json providers["pixlwiz"]["api_key"]
  // so ProviderDlg reflects the login state and any code path reading settings.json
  // directly also gets the token without going through merge_provider_credentials.
  {
      std::string serr;
      media::runtime_settings::update_pixlwiz_api_key(access_token, serr);
      if (!serr.empty())
          zlog("interactive: update_pixlwiz_api_key (non-fatal): " + serr);
  }

  return 0;
}

void pm_image_register_login(CLI::App& app, PmImageCliState& s) {
    s.login_cmd = app.add_subcommand(
        "login",
        "ZITADEL OIDC: PKCE loopback sign-in (system browser), or --probe / --decode-jwt (see docs/zitadel.md). "
        "Issuer and client id are optional in env: when unset, defaults from src/constants.hpp are used (Win32 menu "
        "login uses the same path).");
    s.login_cmd->add_flag("--probe", s.login_probe,
                          "Fetch OIDC discovery + JWKS only; verbose stderr logs; no browser.");
    s.login_cmd->add_flag("--no-browser", s.login_no_browser,
                          "Print authorize URL and code_verifier to stderr instead of opening a browser.");
    s.login_decode_opt =
        s.login_cmd
            ->add_option(
                "--decode-jwt", s.login_decode_jwt_value,
                "Decode a JWT access token and explain `sub` vs app user id (optional value; if omitted, uses "
                "ZITADEL_TEST_ACCESS_TOKEN from the environment).")
            ->expected(0, 1);
    s.login_cmd->add_option("--issuer", s.login_issuer,
                            "Override ZITADEL_ISSUER / VITE_ZITADEL_AUTHORITY.");
    s.login_cmd->add_option("--client-id", s.login_client_id,
                            "Override ZITADEL_OIDC_CLIENT_ID / VITE_ZITADEL_CLIENT_ID.");
    s.login_cmd
        ->add_option(
            "--oauth-port", s.login_oauth_port,
            "First loopback port for http://127.0.0.1:<port>/callback (default 8844). "
            "Register that exact redirect URI on the ZITADEL application; if the port is busy, the next free port up to +50 is tried (each must be registered, or use a free port).")
        ->check(CLI::Range(1024, 65535));

    s.logout_cmd = app.add_subcommand(
        "logout",
        "Remove zitadel-oauth.json from the app profile (clears access/refresh tokens and cached identity).");
}
