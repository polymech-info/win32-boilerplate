#include "duplicates.hpp"
#include "constants.hpp"
#include "batch_queue.hpp"
#include "glob_paths.hpp"
#include "meta.hpp"
#include "app_image_provider.hpp"

#include <picosha2.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <cmath>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <map>
#include <sstream>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <vips/vips.h>

#include "kbot.h"
#include "llm_client.h"

namespace media {
namespace fs   = std::filesystem;
using nlohmann::json;

// ── vips bootstrap (same pattern as meta.cpp) ──────────────────────────────

namespace {
std::once_flag g_vips_init;
void           ensure_vips() {
    std::call_once(g_vips_init, []() {
        if (vips_init("duplicates")) std::abort();
    });
}
std::string vips_err() {
    const char* b = vips_error_buffer();
    std::string s = b ? b : "vips error";
    vips_error_clear();
    return s;
}
} // namespace

namespace {

// ── Path collection (mirrors find.cpp) ────────────────────────────────────

bool ext_is_supported_image(const std::string& ext_with_dot) {
    static const char* k[] = {".jpg",  ".jpeg", ".png",  ".gif", ".bmp",
                              ".webp", ".tiff", ".tif",  ".jpe", ".jfif",
                              ".avif", ".arw",  ".heic", ".jf"};
    std::string e = ext_with_dot;
    for (char& c : e)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    for (const char* ref : k)
        if (e == ref) return true;
    return false;
}

void collect_recursive(const fs::path& dir, std::set<std::string>& out) {
    std::error_code ec;
    auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
    if (ec) return;
    const auto end = fs::recursive_directory_iterator{};
    for (; it != end; it.increment(ec)) {
        if (ec) break;
        const auto& ent = *it;
        if (!ent.is_regular_file() || !ext_is_supported_image(ent.path().extension().string())) continue;
        out.insert(fs::weakly_canonical(ent.path(), ec).string());
    }
}

void collect_one(const std::string& spec, bool recursive, std::set<std::string>& out) {
    std::error_code ec;
    fs::path p = fs::u8path(spec);
    if (fs::is_directory(p, ec)) {
        if (recursive)
            collect_recursive(p, out);
        else {
            for (const auto& ent : fs::directory_iterator(p, ec)) {
                if (!ent.is_regular_file()) continue;
                if (ext_is_supported_image(ent.path().extension().string()))
                    out.insert(fs::weakly_canonical(ent.path(), ec).string());
            }
        }
        return;
    }
    if (fs::is_regular_file(p, ec)) {
        if (ext_is_supported_image(p.extension().string()))
            out.insert(fs::weakly_canonical(p, ec).string());
        return;
    }
    std::string err;
    for (const auto& e : expand_input_paths(spec, err)) {
        fs::path q  = fs::u8path(e);
        if (fs::is_regular_file(q, ec) && ext_is_supported_image(q.extension().string()))
            out.insert(fs::weakly_canonical(q, ec).string());
    }
}

// ── Small utils ───────────────────────────────────────────────────────────

std::string json_llm_log_basename(const std::string& utf8_path) {
    try {
        return pm::path_u8_str(fs::u8path(utf8_path).filename());
    } catch (...) {
        return utf8_path;
    }
}

std::string json_llm_truncate_log(const std::string& s, size_t max = 160) {
    if (s.size() <= max) return s;
    return s.substr(0, max - 3) + "...";
}

std::string trim_json_llm_whitespace(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    if (a >= b) return {};
    return s.substr(a, b - a);
}

// Strip optional ``` / ```json … ``` wrapper (models often return JSON inside markdown fences).
std::string strip_json_markdown_fence(const std::string& s) {
    std::string t = trim_json_llm_whitespace(s);
    if (t.size() < 3 || t.compare(0, 3, "```") != 0) return t;
    size_t i = 3;
    while (i < t.size() && t[i] != '\n' && t[i] != '\r') ++i;
    if (i < t.size() && t[i] == '\r') ++i;
    if (i < t.size() && t[i] == '\n') ++i;
    const size_t close = t.find("```", i);
    if (close == std::string::npos) return t;
    return trim_json_llm_whitespace(t.substr(i, close - i));
}

// Accept raw JSON or fenced markdown; on failure `err` is the last parse error message.
bool parse_llm_json_object_flexible(const std::string& raw, json& out, std::string& err) {
    err.clear();
    std::string last;
    auto one = [](const std::string& chunk, json& o, std::string& e) -> bool {
        e.clear();
        try {
            json j = json::parse(chunk);
            if (j.is_object()) {
                o = std::move(j);
                return true;
            }
            e = "JSON value is not an object";
        } catch (const std::exception& ex) {
            e = ex.what();
        } catch (...) {
            e = "unknown parse error";
        }
        return false;
    };
    if (one(trim_json_llm_whitespace(raw), out, last)) return true;
    const std::string unf = strip_json_markdown_fence(raw);
    if (one(unf, out, last)) return true;
    size_t a = unf.find('{');
    const size_t b = unf.rfind('}');
    if (a != std::string::npos && b != std::string::npos && b > a) {
        if (one(unf.substr(a, b - a + 1), out, last)) return true;
    }
    a = unf.find('{');
    if (a != std::string::npos) {
        int depth = 0;
        for (size_t j = a; j < unf.size(); ++j) {
            if (unf[j] == '{') ++depth;
            else if (unf[j] == '}') {
                --depth;
                if (depth == 0) {
                    if (one(unf.substr(a, j - a + 1), out, last)) return true;
                    break;
                }
            }
        }
    }
    err = last.empty() ? "could not parse a JSON object from model output" : last;
    return false;
}

json json_subset_for_llm_compare(const json& jfull) {
    json sub = json::object();
    for (const char* k : {"alt", "characters", "description", "estimated_location"}) {
        if (jfull.contains(k)) sub[k] = jfull[k];
    }
    return sub;
}

int hamming64(uint64_t a, uint64_t b) {
    uint64_t x = a ^ b;
    int     c  = 0;
    while (x) {
        x &= (x - 1);
        ++c;
    }
    return c;
}

std::string to_hex16(uint64_t h) {
    std::ostringstream o;
    o << std::hex << std::setfill('0') << std::setw(16) << h;
    return o.str();
}

std::string sha256_hex(const std::string& s) {
    std::string hex;
    picosha2::hash256_hex_string(s.begin(), s.end(), hex);
    return hex;
}

std::string read_file_text(const fs::path& p) {
    std::ifstream ifs(p, std::ios::binary);
    if (!ifs) return {};
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

std::string exif_text_for(const std::string& path) {
    VipsImage* img = vips_image_new_from_file(path.c_str(), "access", VIPS_ACCESS_SEQUENTIAL, nullptr);
    if (!img) {
        vips_error_clear();
        return {};
    }
    std::string out;
    out.reserve(512);
    out += "width: ";
    out += std::to_string(vips_image_get_width(img));
    out += '\n';
    out += "height: ";
    out += std::to_string(vips_image_get_height(img));
    out += '\n';
    gchar** fields = vips_image_get_fields(img);
    if (fields) {
        for (gchar** f = fields; *f; ++f) {
            const std::string key(*f);
            if (key.rfind("exif-", 0) != 0) continue;
            char* val = nullptr;
            if (vips_image_get_as_string(img, key.c_str(), &val) != 0 || !val) continue;
            std::string v(val);
            g_free(val);
            auto paren = v.rfind(" (");
            if (paren != std::string::npos && v.back() == ')') v.erase(paren);
            out += key.substr(5);
            out += ": ";
            out += v;
            out += '\n';
        }
        g_strfreev(fields);
    }
    g_object_unref(img);
    return out;
}

void normalize_corpus(std::string& s) {
    for (char& c : s)
        if (c == '\r') c = '\n';
    while (!s.empty() && (s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
    auto a = s.find_first_not_of(" \t\n");
    if (a == std::string::npos) {
        s.clear();
        return;
    }
    auto b = s.find_last_not_of(" \t\n");
    s = s.substr(a, b - a + 1);
}

bool dhash64_for_path(const std::string& path, uint64_t& h_out, std::string& err) {
    ensure_vips();
    VipsImage* in = vips_image_new_from_file(path.c_str(), "access", VIPS_ACCESS_SEQUENTIAL, nullptr);
    if (!in) {
        err = vips_err();
        return false;
    }
    VipsImage* g = nullptr;
    if (vips_colourspace(in, &g, VIPS_INTERPRETATION_B_W, nullptr)) {
        g_object_unref(in);
        err = vips_err();
        return false;
    }
    g_object_unref(in);
    in = g;

    const int W  = vips_image_get_width(in);
    const int H  = vips_image_get_height(in);
    VipsImage* sm = nullptr;
    if (vips_resize(in, &sm, 9.0 / (double)W, "vscale", 8.0 / (double)H, nullptr)) {
        g_object_unref(in);
        err = vips_err();
        return false;
    }
    g_object_unref(in);
    in = sm;
    if (vips_image_get_width(in) != 9 || vips_image_get_height(in) != 8
        || vips_image_get_bands(in) != 1) {
        g_object_unref(in);
        err = "dhash: unexpected 9x8x1 output";
        return false;
    }
    if (vips_image_get_format(in) != VIPS_FORMAT_UCHAR) {
        VipsImage* u8 = nullptr;
        if (vips_cast(in, &u8, VIPS_FORMAT_UCHAR, nullptr)) {
            g_object_unref(in);
            err = vips_err();
            return false;
        }
        g_object_unref(in);
        in = u8;
    }

    size_t    sz  = 0;
    void*     mem = vips_image_write_to_memory(in, &sz);
    g_object_unref(in);
    if (!mem) {
        err = "dhash: write_to_memory failed";
        return false;
    }
    if (sz < 9 * 8) {
        g_free(mem);
        err = "dhash: buffer too small";
        return false;
    }
    const uint8_t* p = static_cast<const uint8_t*>(mem);
    uint64_t       h  = 0;
    int            b  = 0;
    for (int row = 0; row < 8; ++row) {
        for (int col = 0; col < 8; ++col) {
            if (p[static_cast<size_t>(row) * 9 + static_cast<size_t>(col)]
                > p[static_cast<size_t>(row) * 9 + static_cast<size_t>(col) + 1]) {
                h |= (1ull << b);
            }
            ++b;
        }
    }
    g_free(mem);
    h_out = h;
    return true;
}

// Union-find
struct Uf {
    std::vector<int> p, r;
    explicit Uf(int n = 0) : p(static_cast<std::size_t>(n)), r(static_cast<std::size_t>(n), 0) {
        for (int i = 0; i < n; ++i) p[static_cast<std::size_t>(i)] = i;
    }
    int find(int i) {
        int& pi = p[static_cast<std::size_t>(i)];
        if (pi == i) return i;
        return (pi = find(pi));
    }
    void uni(int a, int b) {
        a = find(a);
        b = find(b);
        if (a == b) return;
        if (r[static_cast<std::size_t>(a)] < r[static_cast<std::size_t>(b)]) std::swap(a, b);
        p[static_cast<std::size_t>(b)]     = a;
        if (r[static_cast<std::size_t>(a)] == r[static_cast<std::size_t>(b)]) ++r[static_cast<std::size_t>(a)];
    }
};

} // namespace

namespace {

std::string iso8601_utc() {
    using namespace std::chrono;
    const std::time_t t = system_clock::to_time_t(system_clock::now());
    std::tm             tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return std::string(buf);
}

const char* mode_cstr(DuplicatesMode m) {
    switch (m) {
    case DuplicatesMode::Size: return "size";
    case DuplicatesMode::Fingerprint: return "fingerprint";
    case DuplicatesMode::Meta: return "meta";
    }
    return "unknown";
}

json options_to_json(const DuplicatesOptions& o) {
    return json{
        {"mode",                    mode_cstr(o.mode)},
        {"recursive",               o.recursive},
        {"min_group_size",          o.min_group_size},
        {"max_hamming",             o.max_hamming},
        {"fingerprint_same_size_only", o.fingerprint_same_size_only},
        {"use_md",                  o.use_md},
        {"use_json",                o.use_json},
        {"use_exif",                o.use_exif},
        {"meta_prompt",             o.meta_prompt},
        {"meta_json_llm_compare",   o.meta_json_llm_compare},
        {"meta_json_implicit_generate", o.meta_json_implicit_generate},
        {"meta_json_min_similarity", o.meta_json_min_similarity},
        {"meta_json_compare_prompt", o.meta_json_compare_prompt},
        {"llm_router",   o.llm_router},
        {"llm_model",    o.llm_model},
        {"llm_base_url", o.llm_base_url},
        {"llm_timeout_ms", o.llm_timeout_ms},
        {"llm_api_key_set", !o.llm_api_key.empty()},
        {"detailed_report",         o.detailed_report},
    };
}

void report_append_base(DuplicatesResult& res, const std::vector<std::string>& inputs,
                        const std::vector<std::string>& cands, const DuplicatesOptions& opts) {
    res.report["tool"]            = "duplicates";
    res.report["kind"]            = pm::brand::k_json_kind_duplicates_u8;
    res.report["format_version"]  = 2;
    res.report["action_log"]      = json::array();
    res.report["generated_utc"]   = iso8601_utc();
    res.report["options"]         = options_to_json(opts);
    res.report["inputs"]          = inputs;
    res.report["candidates"]      = cands;
    res.report["summary"]         = {{"ok", res.ok},
                                     {"scanned", res.scanned},
                                     {"skipped_fingerprint", res.skipped_fingerprint},
                                     {"skipped_meta", res.skipped_meta},
                                     {"meta_empty_files", res.meta_empty_files},
                                     {"n_groups", static_cast<int>(res.groups.size())}};
    if (!res.error.empty()) res.report["error"] = res.error;
    json garr = json::array();
    for (const auto& g : res.groups) {
        garr.push_back({{"method", g.method},
                        {"key",    g.key},
                        {"count",  static_cast<int>(g.paths.size())},
                        {"paths",  g.paths}});
    }
    res.report["groups"] = std::move(garr);
}

struct FpItemRow {
    std::string    path;
    std::uintmax_t fsize;
    std::uint64_t  hash;
};

void report_fingerprint_extras(DuplicatesResult& res, const std::vector<FpItemRow>& items) {
    json pf   = json::array();
    json uniq = json::array();
    std::unordered_map<std::string, uint64_t> path_to_hash;
    for (const auto& it : items) {
        path_to_hash[it.path] = it.hash;
        pf.push_back({{"path", it.path},
                      {"file_size", it.fsize},
                      {"dhash_hex", to_hex16(it.hash)}});
    }
    res.report["fingerprint"] = {{"per_file", pf}};

    std::set<uint64_t> distinct;
    for (const auto& it : items) distinct.insert(it.hash);
    for (uint64_t h : distinct) uniq.push_back(to_hex16(h));
    res.report["fingerprint"]["distinct_dhashes"] = std::move(uniq);

    const int T = res.report["options"].contains("max_hamming")
                    ? res.report["options"]["max_hamming"].get<int>()
                    : 0;
    res.report["fingerprint"]["max_hamming"] = T;
    if (res.report["options"].contains("fingerprint_same_size_only")) {
        res.report["fingerprint"]["fingerprint_same_size_only"] =
            res.report["options"]["fingerprint_same_size_only"].get<bool>();
    }

    json pairs = json::array();
    for (const auto& g : res.groups) {
        if (g.method != "fingerprint") continue;
        const auto& paths = g.paths;
        for (std::size_t i = 0; i < paths.size(); ++i) {
            for (std::size_t j = i + 1; j < paths.size(); ++j) {
                auto hi = path_to_hash.find(paths[i]);
                auto hj = path_to_hash.find(paths[j]);
                if (hi == path_to_hash.end() || hj == path_to_hash.end()) continue;
                int hdist = hamming64(hi->second, hj->second);
                pairs.push_back({{"path_a", paths[i]},
                                 {"dhash_a", to_hex16(hi->second)},
                                 {"path_b", paths[j]},
                                 {"dhash_b", to_hex16(hj->second)},
                                 {"hamming", hdist}});
            }
        }
    }
    res.report["fingerprint"]["pairwise_in_output_groups"] = std::move(pairs);
}

void report_size_extras(DuplicatesResult& res, const std::vector<std::string>& cands) {
    json  pf  = json::array();
    auto  szs = std::set<std::uintmax_t>();
    for (const auto& pth : cands) {
        std::error_code ec;
        if (!fs::is_regular_file(pth, ec) || ec) {
            json row;
            row["path"]      = pth;
            row["file_size"] = nullptr;
            row["readable"]  = false;
            pf.push_back(std::move(row));
            continue;
        }
        const auto s = fs::file_size(fs::u8path(pth), ec);
        if (ec) {
            json row;
            row["path"]      = pth;
            row["file_size"] = nullptr;
            row["readable"]  = false;
            pf.push_back(std::move(row));
            continue;
        }
        szs.insert(s);
        json row2;
        row2["path"]      = pth;
        row2["file_size"] = s;
        row2["readable"]  = true;
        pf.push_back(std::move(row2));
    }
    res.report["mode"] = "size";
    res.report["size"] = {{"per_file", std::move(pf)}};
    json dj = json::array();
    for (auto s : szs) dj.push_back(s);
    res.report["size"]["distinct_file_sizes"] = std::move(dj);
}

// Primary automation payload: every image in an *emitted* group → peers with pairwise
// “diff” fields for follow-up (move/delete). Keys are full paths, sorted in `by_path`.
json make_duplicate_map_size(const std::vector<DuplicateGroup>& groups) {
    std::map<std::string, json> m;
    for (const auto& g : groups) {
        if (g.method != "size") continue;
        std::uintmax_t sz = 0;
        try {
            sz = static_cast<std::uintmax_t>(std::stoull(g.key, nullptr, 10));
        } catch (...) {
        }
        for (const auto& p : g.paths) {
            json peers = json::array();
            for (const auto& q : g.paths) {
                if (q == p) continue;
                peers.push_back(
                    {{"path",   q},
                     {
                         "pairwise",
                         {{"file_size_bytes_self",  sz},
                          {"file_size_bytes_peer",  sz},
                          {"size_delta_bytes",      0},
                          {"byte_size_equal",        true},
                          {"match_basis", "same_file_byte_size"}},
                     }});
            }
            m[p] = {{"method", "size"},
                    {"group_key", g.key},
                    {"file_size", sz},
                    {"peer_count", static_cast<int>(peers.size())},
                    {"peers",      std::move(peers)}};
        }
    }
    json by_path;
    for (auto& e : m) by_path[std::move(e.first)] = std::move(e.second);
    return {{"version", 1}, {"mode", "size"}, {"by_path", std::move(by_path)}};
}

json make_duplicate_map_fingerprint(const std::vector<DuplicateGroup>&                      groups,
                                    const std::unordered_map<std::string, uint64_t>&         path_hash,
                                    const std::unordered_map<std::string, std::uintmax_t>&   path_fsize) {
    std::map<std::string, json> m;
    for (const auto& g : groups) {
        if (g.method != "fingerprint") continue;
        for (const auto& p : g.paths) {
            const uint64_t hp  = path_hash.count(p) ? path_hash.at(p) : 0u;
            const std::uintmax_t sp
                = path_fsize.count(p) ? path_fsize.at(p) : static_cast<std::uintmax_t>(0);
            json                 peers = json::array();
            for (const auto& q : g.paths) {
                if (q == p) continue;
                const uint64_t       hq  = path_hash.count(q) ? path_hash.at(q) : 0u;
                const std::uintmax_t sq
                    = path_fsize.count(q) ? path_fsize.at(q) : static_cast<std::uintmax_t>(0);
                const int     ham  = static_cast<int>(hamming64(hp, hq));
                const int64_t dsz = static_cast<int64_t>(sp) - static_cast<int64_t>(sq);
                json          pw;
                pw["dhash_self_hex"]        = to_hex16(hp);
                pw["dhash_peer_hex"]        = to_hex16(hq);
                pw["hamming"]               = ham;
                pw["file_size_bytes_self"]  = sp;
                pw["file_size_bytes_peer"]  = sq;
                pw["file_size_delta_bytes"] = dsz;
                pw["match_basis"]           = "perceptual_dhash_hamming";
                json peero;
                peero["path"]     = q;
                peero["pairwise"] = std::move(pw);
                peers.push_back(std::move(peero));
            }
            m[p] = {{"method",     "fingerprint"},
                    {"group_key",  g.key},
                    {"dhash_hex",   to_hex16(hp)},
                    {"file_size",  sp},
                    {"peer_count", static_cast<int>(peers.size())},
                    {"peers",      std::move(peers)}};
        }
    }
    json by_path;
    for (auto& e : m) by_path[std::move(e.first)] = std::move(e.second);
    return {{"version", 1}, {"mode", "fingerprint"}, {"by_path", std::move(by_path)}};
}

json make_duplicate_map_meta(const std::vector<DuplicateGroup>& groups) {
    std::map<std::string, json> m;
    for (const auto& g : groups) {
        if (g.method != "meta") continue;
        for (const auto& p : g.paths) {
            json peers = json::array();
            for (const auto& q : g.paths) {
                if (q == p) continue;
                peers.push_back({{"path",        q},
                                 {
                                     "pairwise",
                                     {{"shared_corpus_key", g.key},
                                      {"match_basis",    "sidecar_exif_hash_after_normalize"}},
                                 }});
            }
            m[p] = {{"method",     "meta"},
                    {"group_key",  g.key},
                    {"peer_count", static_cast<int>(peers.size())},
                    {"peers",      std::move(peers)}};
        }
    }
    json by_path;
    for (auto& e : m) by_path[std::move(e.first)] = std::move(e.second);
    return {{"version", 1}, {"mode", "meta"}, {"by_path", std::move(by_path)}};
}

const char* kDefaultMetaJsonCompareInstruction =
    "You compare two image catalog JSON objects (from the meta command) for the SAME photograph or "
    "the same real-world subject/scene. Fields are: alt, characters, description, estimated_location. "
    "LLM runs may differ in wording — treat close paraphrases as agreement. Missing or empty fields are "
    "common; trust description most. estimated_location is a weak signal.";

json make_duplicate_map_meta_llm(const std::vector<DuplicateGroup>&                      groups,
                                 const std::map<std::string, std::map<std::string, json>>& sym_pw) {
    std::map<std::string, json> m;
    for (const auto& g : groups) {
        if (g.method != "meta") continue;
        for (const auto& p : g.paths) {
            json peers = json::array();
            for (const auto& q : g.paths) {
                if (q == p) continue;
                json pairwise;
                auto itp = sym_pw.find(p);
                if (itp != sym_pw.end()) {
                    auto itq = itp->second.find(q);
                    if (itq != itp->second.end()) pairwise = itq->second;
                }
                if (pairwise.is_null() || pairwise.empty())
                    pairwise = {{"method", "meta cmp:json"}, {"match_basis", "llm_json_sidecar_compare"}};
                peers.push_back({{"path", q}, {"pairwise", std::move(pairwise)}});
            }
            m[p] = {{"method",     "meta"},
                    {"group_key",  g.key},
                    {"peer_count", static_cast<int>(peers.size())},
                    {"peers",      std::move(peers)}};
        }
    }
    json by_path;
    for (auto& e : m) by_path[std::move(e.first)] = std::move(e.second);
    return {{"version", 1}, {"mode", "meta"}, {"by_path", std::move(by_path)}};
}

} // namespace

// ── Public API ────────────────────────────────────────────────────────────

static void ensure_session_envelope(DuplicatesResult& res) {
    using nlohmann::json;
    json& r = res.report;
    if (!r.contains("kind") || !r["kind"].is_string() || r["kind"].get<std::string>().empty())
        r["kind"] = pm::brand::k_json_kind_duplicates_u8;
    if (!r.contains("format_version")) r["format_version"] = 2;
    if (!r.contains("action_log") || !r["action_log"].is_array()) r["action_log"] = json::array();
    if (!r.contains("tool")) r["tool"] = "duplicates";
    if (!r.contains("generated_utc") || !r["generated_utc"].is_string()) {
        const std::time_t t
            = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm             tm{};
#if defined(_WIN32)
        gmtime_s(&tm, &t);
#else
        gmtime_r(&t, &tm);
#endif
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
        r["generated_utc"] = std::string(buf);
    }
}

namespace {
// Cooperative UI batch: pause (blocks) / cancel between CPU-bound stages. Used by the WinUI worker
// (BatchControl) only; pass nullptr for CLI and tests.
bool dup_mark_cancelled(DuplicatesResult& res) {
    res.ok    = false;
    res.error = "duplicates: cancelled";
    ensure_session_envelope(res);
    return true;
}

// Returns true if find_duplicates must return immediately (user cancelled).
bool dup_batch_point(BatchControl* batch, DuplicatesResult& res, const std::function<void()>& on_before_pause)
{
    if (!batch) return false;
    if (batch->cancel.load(std::memory_order_acquire)) return dup_mark_cancelled(res);
    if (batch->paused.load(std::memory_order_acquire) && on_before_pause) on_before_pause();
    batch->check_pause();
    if (batch->cancel.load(std::memory_order_acquire)) return dup_mark_cancelled(res);
    return false;
}
} // namespace

DuplicatesResult find_duplicates(const std::vector<std::string>& inputs, const DuplicatesOptions& opts,
                                 DuplicatesProgressFn progress, BatchControl* batch,
                                 std::function<void()> on_before_pause) {
    DuplicatesResult res;
    if (opts.min_group_size < 2) {
        res.error = "duplicates: min_group_size must be at least 2";
        ensure_session_envelope(res);
        return res;
    }
    if (opts.max_hamming < 0 || opts.max_hamming > 64) {
        res.error = "duplicates: max_hamming must be 0..64";
        ensure_session_envelope(res);
        return res;
    }
    if (opts.mode == DuplicatesMode::Fingerprint
        || (opts.mode == DuplicatesMode::Meta && opts.use_exif))
        ensure_vips();
    if (dup_batch_point(batch, res, on_before_pause)) return res;

    std::set<std::string> cset;
    for (const auto& spec : inputs) collect_one(spec, opts.recursive, cset);
    std::vector<std::string> cands(cset.begin(), cset.end());
    res.scanned = static_cast<int>(cands.size());
    if (progress) progress("duplicates: scanned " + std::to_string(res.scanned) + " image(s)");
    if (dup_batch_point(batch, res, on_before_pause)) return res;

    if (opts.mode == DuplicatesMode::Size) {
        std::map<std::uintmax_t, std::vector<std::string>> by;
        for (const auto& pth : cands) {
            if (dup_batch_point(batch, res, on_before_pause)) return res;
            std::error_code ec;
            if (!fs::is_regular_file(pth, ec) || ec) continue;
            auto sz = fs::file_size(fs::u8path(pth), ec);
            if (ec) continue;
            by[sz].push_back(pth);
        }
        for (const auto& e : by) {
            if ((int)e.second.size() < opts.min_group_size) continue;
            DuplicateGroup g;
            g.method   = "size";
            g.key      = std::to_string(e.first);
            g.paths    = e.second;
            std::sort(g.paths.begin(), g.paths.end());
            res.groups.push_back(std::move(g));
        }
        res.ok = true;
        // Stable sort by group size (desc) then first path
        std::sort(res.groups.begin(), res.groups.end(), [](const DuplicateGroup& a, const DuplicateGroup& b) {
            if (a.paths.size() != b.paths.size()) return a.paths.size() > b.paths.size();
            if (a.paths.empty() || b.paths.empty()) return a.paths.size() > b.paths.size();
            return a.paths[0] < b.paths[0];
        });
        if (opts.detailed_report) {
            report_append_base(res, inputs, cands, opts);
            report_size_extras(res, cands);
        }
        res.report["duplicate_map"] = make_duplicate_map_size(res.groups);
        ensure_session_envelope(res);
        return res;
    }

    if (opts.mode == DuplicatesMode::Fingerprint) {
        struct Item {
            std::string    path;
            std::uintmax_t fsize  = 0;
            std::uint64_t  hash   = 0;
        };
        std::vector<Item> items;
        items.reserve(cands.size());
        for (const auto& pth : cands) {
            if (dup_batch_point(batch, res, on_before_pause)) return res;
            Item it;
            it.path = pth;
            if (std::error_code ec; !fs::is_regular_file(pth, ec) || ec) {
                res.skipped_fingerprint++;
                continue;
            } else {
                std::error_code ec_sz;
                it.fsize = fs::file_size(fs::u8path(pth), ec_sz);
            }
            std::string e;
            if (!dhash64_for_path(pth, it.hash, e)) {
                if (progress) progress("duplicates: skip fingerprint: " + pth + " — " + e);
                res.skipped_fingerprint++;
                continue;
            }
            items.push_back(std::move(it));
        }

        if (items.empty()) {
            res.ok = true;
            if (opts.detailed_report) {
                report_append_base(res, inputs, cands, opts);
                res.report["mode"] = "fingerprint";
                res.report["fingerprint"]        = json::object();
                res.report["fingerprint"]["per_file"] = json::array();
            }
            res.report["duplicate_map"] = make_duplicate_map_fingerprint(res.groups, {},
                                                                           {});
            ensure_session_envelope(res);
            return res;
        }

        if (opts.max_hamming == 0) {
            std::unordered_map<std::uint64_t, std::vector<std::string>> m;
            for (auto& t : items) m[t.hash].push_back(t.path);
            for (auto& e : m) {
                if ((int)e.second.size() < opts.min_group_size) continue;
                DuplicateGroup g;
                g.method   = "fingerprint";
                g.key      = to_hex16(e.first);
                g.paths    = std::move(e.second);
                std::sort(g.paths.begin(), g.paths.end());
                res.groups.push_back(std::move(g));
            }
        } else {
            auto const run_bucket = [&](std::vector<Item>& bucket) -> bool {
                const int  n     = static_cast<int>(bucket.size());
                Uf         uf(n);
                const int  T     = opts.max_hamming;
                long long  pairs = 0;
                for (int i = 0; i < n; ++i) {
                    for (int j = i + 1; j < n; ++j) {
                        if (batch && (++pairs % 32) == 0) {
                            if (dup_batch_point(batch, res, on_before_pause)) return true;
                        }
                        if (hamming64(bucket[static_cast<std::size_t>(i)].hash,
                                      bucket[static_cast<std::size_t>(j)].hash)
                            <= T)
                            uf.uni(i, j);
                    }
                }
                struct Cmp {
                    bool operator()(int a, int b) const { return a < b; }
                };
                std::map<int, std::vector<std::string>, Cmp> rep_to_paths;
                for (int i = 0; i < n; ++i) {
                    const int r = uf.find(i);
                    rep_to_paths[r].push_back(bucket[static_cast<std::size_t>(i)].path);
                }
                for (auto& e : rep_to_paths) {
                    if ((int)e.second.size() < opts.min_group_size) continue;
                    // Representative hash key = first path's hash
                    int rep = 0;
                    for (int i = 0; i < n; ++i) {
                        if (uf.find(i) == e.first) {
                            rep = i;
                            break;
                        }
                    }
                    DuplicateGroup g;
                    g.method   = "fingerprint";
                    g.key      = to_hex16(bucket[static_cast<std::size_t>(rep)].hash) + " (~hamming≤"
                                + std::to_string(T) + ")";
                    g.paths = std::move(e.second);
                    std::sort(g.paths.begin(), g.paths.end());
                    res.groups.push_back(std::move(g));
                }
                return false;
            };

            if (opts.fingerprint_same_size_only) {
                std::unordered_map<std::uintmax_t, std::vector<Item>> buckets;
                for (auto& t : items) buckets[t.fsize].push_back(std::move(t));
                for (auto& b : buckets) {
                    if (b.second.size() < static_cast<std::size_t>(opts.min_group_size)) continue;
                    if (b.second.size() == 1) continue;
                    if (run_bucket(b.second)) return res;
                }
            } else {
                if (run_bucket(items)) return res;
            }
        }

        std::sort(res.groups.begin(), res.groups.end(), [](const DuplicateGroup& a, const DuplicateGroup& b) {
            if (a.paths.size() != b.paths.size()) return a.paths.size() > b.paths.size();
            if (a.paths.empty() || b.paths.empty()) return a.paths.size() > b.paths.size();
            return a.paths[0] < b.paths[0];
        });
        res.ok = true;
        if (opts.detailed_report) {
            report_append_base(res, inputs, cands, opts);
            res.report["mode"] = "fingerprint";
            std::vector<FpItemRow> fpr;
            fpr.reserve(items.size());
            for (const auto& it : items) fpr.push_back({it.path, it.fsize, it.hash});
            report_fingerprint_extras(res, fpr);
        }
        std::unordered_map<std::string, uint64_t>       ph;
        std::unordered_map<std::string, std::uintmax_t> pz;
        for (const auto& it : items) {
            ph[it.path] = it.hash;
            pz[it.path] = it.fsize;
        }
        res.report["duplicate_map"] = make_duplicate_map_fingerprint(res.groups, ph, pz);
        ensure_session_envelope(res);
        return res;
    }

    // DuplicatesMode::Meta — LLM JSON sidecar compare (OpenRouter / chat provider via kbot)
    if (opts.mode == DuplicatesMode::Meta && opts.meta_json_llm_compare) {
        if (!opts.use_json) {
            res.error = "duplicates: --meta-compare-json-llm needs <stem>.json sidecars (omit --no-json)";
            ensure_session_envelope(res);
            return res;
        }
        if (opts.llm_api_key.empty()) {
            res.error
                = "duplicates: meta JSON compare: no API key (app Chat Provider settings, --api-key, or env)";
            ensure_session_envelope(res);
            return res;
        }

        struct MetaIdx {
            std::string path;
            json        subset;
        };
        std::vector<MetaIdx> metas;
        metas.reserve(cands.size());
        std::unordered_set<std::string> have_json;
        for (const auto& pth : cands) {
            if (dup_batch_point(batch, res, on_before_pause)) return res;
            fs::path p(pth);
            auto     jf = p;
            jf.replace_extension(".json");
            if (!fs::exists(jf)) continue;
            const std::string raw = read_file_text(jf);
            if (raw.empty()) continue;
            json jfull;
            try {
                jfull = json::parse(raw);
            } catch (...) {
                continue;
            }
            json sub = json_subset_for_llm_compare(jfull);
            if (sub.empty()) continue;
            metas.push_back({pth, std::move(sub)});
            have_json.insert(pth);
        }

        if (opts.meta_json_implicit_generate) {
            std::size_t need = 0;
            for (const auto& pth : cands)
                if (!have_json.count(pth)) ++need;
            if (need > 0) {
                if (progress) {
                    progress("duplicates: meta JSON LLM: " + std::to_string(need)
                             + " file(s) lack usable .json; running Meta (cataloguer) to generate sidecars "
                               "(resize + model from options.meta)…");
                }
                MetaOptions mgen = opts.meta;
                if (!mgen.out_json) mgen.out_json = true;
                if (!mgen.out_md && !mgen.out_json) {
                    mgen.out_md   = true;
                    mgen.out_json = true;
                }
                if (mgen.api_key.empty())
                    fill_image_provider_credentials_from_app(mgen.provider, false, mgen.api_key, mgen.base_url);
                if (mgen.api_key.empty()) {
                    res.error
                        = "duplicates: meta JSON implicit generate needs an API key "
                          "(configure providers in app settings; same as the Meta command).";
                    ensure_session_envelope(res);
                    return res;
                }
                for (const auto& pth : cands) {
                    if (have_json.count(pth)) continue;
                    if (dup_batch_point(batch, res, on_before_pause)) return res;
                    if (progress) {
                        progress("duplicates: meta JSON LLM: generate JSON for "
                                 + json_llm_log_basename(pth));
                    }
                    MetaResult mr = meta_extract(pth, mgen, progress);
                    if (!mr.ok) {
                        if (progress) progress("duplicates: meta JSON LLM:   failed: " + mr.error);
                        continue;
                    }
                    json jfull;
                    try {
                        if (!mr.json_text.empty()) jfull = json::parse(mr.json_text);
                    } catch (...) {
                        jfull = json::object();
                    }
                    if (jfull.empty()) {
                        fs::path p(pth);
                        auto     jf = p;
                        jf.replace_extension(".json");
                        if (fs::exists(jf)) {
                            const std::string raw = read_file_text(jf);
                            if (!raw.empty()) {
                                try {
                                    jfull = json::parse(raw);
                                } catch (...) {
                                }
                            }
                        }
                    }
                    json sub = json_subset_for_llm_compare(jfull);
                    if (sub.empty()) {
                        if (progress)
                            progress("duplicates: meta JSON LLM:   generated JSON still has no compare fields");
                        continue;
                    }
                    metas.push_back({pth, std::move(sub)});
                    have_json.insert(pth);
                }
            }
        }

        {
            std::unordered_set<std::string> have;
            for (const auto& m : metas) have.insert(m.path);
            res.meta_empty_files = 0;
            for (const auto& p : cands)
                if (!have.count(p)) ++res.meta_empty_files;
        }

        const int n = static_cast<int>(metas.size());
        if (n < 2) {
            if (progress) {
                progress("duplicates: meta JSON LLM: need at least 2 images with usable .json fields "
                         "(alt / characters / description / estimated_location); got "
                         + std::to_string(n) + "; skipped " + std::to_string(res.meta_empty_files)
                         + " file(s).");
            }
            res.ok = true;
            if (opts.detailed_report) {
                report_append_base(res, inputs, cands, opts);
                res.report["mode"] = "meta";
            }
            res.report["meta_json_compare"] = json::object();
            res.report["meta_json_compare"]["note"] = "fewer than 2 images with JSON compare fields";
            res.report["meta_json_compare"]["min_similarity"]  = opts.meta_json_min_similarity;
            res.report["meta_json_compare"]["router"]          = opts.llm_router;
            res.report["meta_json_compare"]["model"]           = opts.llm_model;
            res.report["duplicate_map"]
                = {{"version", 1}, {"mode", "meta"}, {"by_path", json::object()}};
            ensure_session_envelope(res);
            return res;
        }

        polymech::kbot::KBotOptions kopts;
        kopts.api_key  = opts.llm_api_key;
        kopts.model    = opts.llm_model;
        kopts.router   = opts.llm_router.empty() ? "openrouter" : opts.llm_router;
        kopts.base_url = opts.llm_base_url;
        kopts.llm_timeout_ms     = opts.llm_timeout_ms > 0 ? opts.llm_timeout_ms : 60'000;
        kopts.response_format_json = R"({"type":"json_object"})";
        polymech::kbot::LLMClient client(kopts);

        const int          T_min   = opts.meta_json_min_similarity;
        const long long    n_pairs = static_cast<long long>(n) * (n - 1) / 2;
        if (progress) {
            progress("duplicates: meta JSON LLM: " + std::to_string(n)
                     + " image(s) with JSON fields (alt, characters, description, or "
                       "estimated_location in the sidecar); "
                     + std::to_string(res.meta_empty_files)
                     + " file(s) skipped (missing .json, bad parse, or no usable fields).");
            {
                std::string base = "duplicates: meta JSON LLM: " + kopts.router;
                if (!kopts.model.empty()) base += " / " + kopts.model;
                if (!kopts.base_url.empty()) base += " | base_url set";
                base += " | min similarity to link a pair: " + std::to_string(T_min) + " (0-10, inclusive)";
                base += " | timeout " + std::to_string(kopts.llm_timeout_ms) + " ms per request";
                base += " | " + std::to_string(n_pairs) + " model request(s)";
                progress(base);
            }
        }

        std::map<std::string, std::map<std::string, json>> sym_pw;
        json                                               pair_log     = json::array();
        long long                                          pair_index   = 0;
        int                                                n_pairs_link = 0;

        for (int i = 0; i < n; ++i) {
            for (int j = i + 1; j < n; ++j) {
                if (dup_batch_point(batch, res, on_before_pause)) return res;
                ++pair_index;
                const std::string&   ai = metas[static_cast<std::size_t>(i)].path;
                const std::string&   bi = metas[static_cast<std::size_t>(j)].path;
                const std::string    fa = json_llm_log_basename(ai);
                const std::string    fb = json_llm_log_basename(bi);
                if (progress) {
                    progress("duplicates: meta JSON LLM: [" + std::to_string(pair_index) + "/"
                             + std::to_string(n_pairs) + "] " + fa + "  vs  " + fb);
                }
                std::ostringstream prompt;
                prompt << (opts.meta_json_compare_prompt.empty() ? kDefaultMetaJsonCompareInstruction
                                                                 : opts.meta_json_compare_prompt)
                       << "\n\n";
                prompt << "Image file A: " << metas[static_cast<std::size_t>(i)].path << "\nJSON_A: "
                       << metas[static_cast<std::size_t>(i)].subset.dump(2) << "\n\n";
                prompt << "Image file B: " << metas[static_cast<std::size_t>(j)].path << "\nJSON_B: "
                       << metas[static_cast<std::size_t>(j)].subset.dump(2) << "\n\n";
                prompt << "Return only a JSON object with keys: similarity_0_10 (integer 0-10) and "
                          "notes (short string). 10 = same scene or subject, 0 = unrelated.";
                const auto lr = client.execute_chat(prompt.str());
                if (!lr.success) {
                    if (progress)
                        progress("duplicates: meta JSON LLM:   API error: "
                                 + json_llm_truncate_log(lr.error, 220));
                    res.error = "duplicates: meta JSON LLM: " + lr.error;
                    ensure_session_envelope(res);
                    return res;
                }
                int         sim = -1;
                std::string notes;
                json        resp;
                {
                    std::string perr;
                    if (!parse_llm_json_object_flexible(lr.text, resp, perr)) {
                        if (progress)
                            progress("duplicates: meta JSON LLM:   response parse error: " + perr);
                        res.error = std::string("duplicates: LLM response JSON parse: ") + perr;
                        ensure_session_envelope(res);
                        return res;
                    }
                }
                try {
                    if (resp.contains("similarity_0_10") && resp["similarity_0_10"].is_number_integer())
                        sim = resp["similarity_0_10"].get<int>();
                    else if (resp.contains("similarity_0_10") && resp["similarity_0_10"].is_number()) {
                        const double d = resp["similarity_0_10"].get<double>();
                        sim            = static_cast<int>(std::lround(d));
                    }
                    if (resp.contains("notes") && resp["notes"].is_string())
                        notes = resp["notes"].get<std::string>();
                } catch (const std::exception& e) {
                    if (progress)
                        progress("duplicates: meta JSON LLM:   field read error: " + std::string(e.what()));
                    res.error = std::string("duplicates: LLM response field read: ") + e.what();
                    ensure_session_envelope(res);
                    return res;
                }
                if (sim < 0) {
                    if (progress) {
                        progress("duplicates: meta JSON LLM:   no valid similarity_0_10 in model output (first "
                                 "200 chars): "
                                 + json_llm_truncate_log(lr.text, 200));
                    }
                    res.error = "duplicates: LLM JSON missing valid similarity_0_10: " + lr.text.substr(0, 200);
                    ensure_session_envelope(res);
                    return res;
                }
                if (sim > 10) sim = 10;
                if (sim >= T_min) ++n_pairs_link;
                if (progress) {
                    std::string rline = "duplicates: meta JSON LLM:   result similarity " + std::to_string(sim)
                                        + "/10 (need " + std::to_string(T_min) + " to link)";
                    rline += (sim >= T_min) ? "  ->  linked" : "  ->  not linked";
                    if (!notes.empty())
                        rline += "  |  " + json_llm_truncate_log(notes);
                    progress(rline);
                }
                const std::string& a = metas[static_cast<std::size_t>(i)].path;
                const std::string& b = metas[static_cast<std::size_t>(j)].path;
                json                 pw
                    = {{"method", "meta cmp:json"},
                       {"match_basis", "llm_openrouter_json_sidecar_fields"},
                       {"similarity_0_10", sim},
                       {"notes",             notes}};
                sym_pw[a][b] = pw;
                sym_pw[b][a] = std::move(pw);
                pair_log.push_back(
                    {{"path_a", a}, {"path_b", b}, {"similarity_0_10", sim}, {"notes", notes}});
            }
        }

        if (progress) {
            progress("duplicates: meta JSON LLM: all " + std::to_string(n_pairs) + " pair result(s) received; "
                     + std::to_string(n_pairs_link) + " meet similarity >= " + std::to_string(T_min)
                     + " (used for grouping)");
        }

        Uf uf(n);
        for (int i = 0; i < n; ++i) {
            for (int j = i + 1; j < n; ++j) {
                const std::string& a = metas[static_cast<std::size_t>(i)].path;
                const std::string& b = metas[static_cast<std::size_t>(j)].path;
                int                 sim = 0;
                auto                ia  = sym_pw.find(a);
                if (ia != sym_pw.end()) {
                    auto ib = ia->second.find(b);
                    if (ib != ia->second.end() && ib->second.contains("similarity_0_10"))
                        sim = ib->second["similarity_0_10"].get<int>();
                }
                if (sim >= T_min) uf.uni(i, j);
            }
        }
        std::map<int, std::vector<int>> comp;
        for (int i = 0; i < n; ++i) comp[uf.find(i)].push_back(i);
        for (auto& e : comp) {
            if ((int)e.second.size() < opts.min_group_size) continue;
            DuplicateGroup g;
            g.method = "meta";
            for (int ix : e.second) g.paths.push_back(metas[static_cast<std::size_t>(ix)].path);
            std::sort(g.paths.begin(), g.paths.end());
            std::string acc;
            for (const auto& pth : g.paths) acc += pth;
            g.key  = "llm_json:" + sha256_hex(acc);
            res.groups.push_back(std::move(g));
        }
        std::sort(res.groups.begin(), res.groups.end(), [](const DuplicateGroup& a, const DuplicateGroup& b) {
            if (a.paths.size() != b.paths.size()) return a.paths.size() > b.paths.size();
            if (a.paths.empty() || b.paths.empty()) return a.paths.size() > b.paths.size();
            return a.paths[0] < b.paths[0];
        });
        res.ok = true;
        res.report["mode"] = "meta";
        res.report["meta_json_compare"]         = json::object();
        res.report["meta_json_compare"]["pairs"]  = std::move(pair_log);
        res.report["meta_json_compare"]["min_similarity"]  = T_min;
        res.report["meta_json_compare"]["router"]  = kopts.router;
        res.report["meta_json_compare"]["model"]   = kopts.model;
        if (opts.detailed_report) {
            report_append_base(res, inputs, cands, opts);
        }
        res.report["duplicate_map"] = make_duplicate_map_meta_llm(res.groups, sym_pw);
        if (progress) {
            std::string tail = "duplicates: meta JSON LLM: finished with " + std::to_string(res.groups.size())
                                + " group(s) (each has at least " + std::to_string(opts.min_group_size)
                                + " file(s); single-file and smaller groups are not listed).";
            progress(tail);
        }
        ensure_session_envelope(res);
        return res;
    }

    // DuplicatesMode::Meta (sidecar+EXIF hash)
    std::unordered_map<std::string, std::vector<std::string>> m;
    json                       meta_per_file = json::array();
    for (const auto& pth : cands) {
        if (dup_batch_point(batch, res, on_before_pause)) return res;
        std::string corpus;
        fs::path    p(pth);
        bool        had_md  = false;
        bool        had_json = false;
        if (opts.use_md) {
            auto md = p;
            md.replace_extension(".md");
            if (fs::exists(md)) {
                const auto t = read_file_text(md);
                had_md         = !t.empty();
                corpus += t + "\n";
            }
        }
        if (opts.use_json) {
            auto jf = p;
            jf.replace_extension(".json");
            if (fs::exists(jf)) {
                const auto t = read_file_text(jf);
                had_json       = !t.empty();
                corpus += t + "\n";
            }
        }
        if (opts.use_exif) corpus += exif_text_for(pth) + "\n";
        if (!opts.meta_prompt.empty()) {
            corpus = std::string("[duplicates meta-prompt]\n") + opts.meta_prompt + "\n\n" + corpus;
        }
        const int pre_norm_len = static_cast<int>(corpus.size());
        normalize_corpus(corpus);
        if (corpus.empty()) {
            ++res.meta_empty_files;
            if (opts.detailed_report) {
                json row = {{"path", pth},
                            {"empty_after_normalize", true},
                            {"meta_prompt_in_effect", !opts.meta_prompt.empty()},
                            {"corpus_len_before_norm", pre_norm_len},
                            {"had_md_sidecar", had_md},
                            {"had_json_sidecar", had_json},
                            {"use_exif", opts.use_exif}};
                meta_per_file.push_back(std::move(row));
            }
            if (progress) {
                // optional: one line is noisy for thousands of files; skip
            }
            continue;
        }
        const std::string   key  = sha256_hex(corpus);
        const std::string     prev = corpus.size() > 4000 ? corpus.substr(0, 4000) + "\n... [truncated]" : corpus;
        m[key].push_back(pth);
        if (opts.detailed_report) {
            json row = {{"path", pth},
                        {"empty_after_normalize", false},
                        {"sha256_corpus", key},
                        {"corpus_char_length", static_cast<int>(corpus.size())},
                        {"corpus_preview_markdown", prev},
                        {"meta_prompt_in_effect", !opts.meta_prompt.empty()},
                        {"had_md_sidecar", had_md},
                        {"had_json_sidecar", had_json},
                        {"use_exif", opts.use_exif}};
            meta_per_file.push_back(std::move(row));
        }
    }

    for (const auto& e : m) {
        if ((int)e.second.size() < opts.min_group_size) continue;
        DuplicateGroup g;
        g.method   = "meta";
        g.key      = e.first;
        g.paths    = e.second;
        std::sort(g.paths.begin(), g.paths.end());
        res.groups.push_back(std::move(g));
    }
    std::sort(res.groups.begin(), res.groups.end(), [](const DuplicateGroup& a, const DuplicateGroup& b) {
        if (a.paths.size() != b.paths.size()) return a.paths.size() > b.paths.size();
        if (a.paths.empty() || b.paths.empty()) return a.paths.size() > b.paths.size();
        return a.paths[0] < b.paths[0];
    });
    res.skipped_meta = 0; // reserved: files that had errors reading sidecars
    res.ok             = true;
    if (opts.detailed_report) {
        report_append_base(res, inputs, cands, opts);
        res.report["mode"]     = "meta";
        res.report["meta"]     = json::object();
        res.report["meta"]["per_file"] = std::move(meta_per_file);
        json dkeys             = json::array();
        for (const auto& e : m) dkeys.push_back(e.first);
        res.report["meta"]["distinct_corpus_keys_in_scan"] = std::move(dkeys);
    }
    res.report["duplicate_map"] = make_duplicate_map_meta(res.groups);
    ensure_session_envelope(res);
    return res;
}

std::string format_duplicates_markdown(const json& r) {
    std::ostringstream o;
    o << "# Duplicates report\n\n";
    if (r.contains("generated_utc")) o << "**Generated (UTC):** " << r["generated_utc"].get<std::string>() << "\n\n";
    o << "## Summary\n\n";
    if (r.contains("summary") && r["summary"].is_object()) {
        o << "| Field | Value |\n| --- | --- |\n";
        for (auto it = r["summary"].begin(); it != r["summary"].end(); ++it) {
            o << "| `" << it.key() << "` | " << it->dump() << " |\n";
        }
        o << "\n";
    }
    if (r.contains("error")) o << "## Error\n\n```\n" << r["error"].get<std::string>() << "\n```\n\n";
    o << "## Options (effective)\n\n```json\n" << (r.contains("options") ? r["options"].dump(2) : "{}") << "\n```\n\n";
    o << "## Input specs\n\n";
    if (r.contains("inputs")) {
        for (const auto& p : r["inputs"]) o << "- `" << p.get<std::string>() << "`\n";
    }
    o << "\n## Grouped result\n\n";
    if (r.contains("groups") && r["groups"].is_array()) {
        for (const auto& g : r["groups"]) {
            if (!g.is_object()) continue;
            const std::string meth = g.contains("method") && g["method"].is_string() ? g["method"].get<std::string>() : "?";
            const std::string ky   = g.contains("key") && g["key"].is_string() ? g["key"].get<std::string>() : "";
            const int         cnt  = g.contains("count") && g["count"].is_number_integer() ? g["count"].get<int>() : 0;
            o << "### " << meth << " — key `" << ky << "` (" << cnt << " paths)\n\n";
            if (g.contains("paths") && g["paths"].is_array()) {
                for (const auto& p : g["paths"])
                    o << "- `" << p.get<std::string>() << "`\n";
            }
            o << "\n";
        }
    }
    o << "## Per-image duplicate map (`duplicate_map`)\n\n";
    o << "For automation (delete, move, review): each `by_path` key is a file; `peers` is the list of other "
         "images in the same *emitted* group with `pairwise` size / dHash / Hamming details (see JSON).\n\n";
    if (r.contains("duplicate_map") && r["duplicate_map"].is_object() && r["duplicate_map"].contains("by_path")
        && r["duplicate_map"]["by_path"].is_object() && !r["duplicate_map"]["by_path"].empty()) {
        const auto&   bp  = r["duplicate_map"]["by_path"];
        std::string   mod = "unknown";
        if (r["duplicate_map"].contains("mode") && r["duplicate_map"]["mode"].is_string())
            mod = r["duplicate_map"]["mode"].get<std::string>();
        o << "Mode: `" << mod << "`\n\n";
        o << "| File | # peers | group_key (short) |\n| --- | ---: | --- |\n";
        for (auto it = bp.begin(); it != bp.end(); ++it) {
            const std::string path  = it.key();
            const auto&       ent   = it.value();
            const int         npc   = ent.contains("peer_count") && ent["peer_count"].is_number_integer()
                                          ? ent["peer_count"].get<int>()
                                            : 0;
            const std::string gk     = ent.contains("group_key") && ent["group_key"].is_string()
                                            ? ent["group_key"].get<std::string>()
                                            : std::string{};
            const std::string gk_s   = gk.size() > 24 ? (gk.substr(0, 21) + "…") : gk;
            o << "| `" << path << "` | " << npc << " | `" << gk_s << "` |\n";
        }
        o << "\n";
    } else {
        o << "*(no entries — `by_path` is empty.)*\n\n";
    }
    o << "## Mode-specific diagnostics\n\n";
    if (r.contains("size") && r["size"].is_object() && r["size"].contains("per_file")) {
        o << "### By size: per file\n\n";
        o << "| Path | file_size (bytes) |\n| --- | --- |\n";
        for (const auto& row : r["size"]["per_file"]) {
            o << "| `" << (row.contains("path") ? row["path"].get<std::string>() : std::string{}) << "` | ";
            if (row.contains("file_size") && !row["file_size"].is_null()) o << row["file_size"].dump();
            else
                o << "—";
            o << " |\n";
        }
        o << "\n";
    }
    if (r.contains("fingerprint") && r["fingerprint"].is_object()) {
        o << "### Fingerprint (dHash)\n\n";
        if (r["fingerprint"].contains("per_file")) {
            o << "| Path | bytes | dHash (64-bit hex) |\n| --- | ---: | --- |\n";
            for (const auto& row : r["fingerprint"]["per_file"]) {
                o << "| `" << row["path"].get<std::string>() << "` | " << row["file_size"].dump();
                o << " | `" << row["dhash_hex"].get<std::string>() << "` |\n";
            }
        }
        o << "\n";
        if (r["fingerprint"].contains("pairwise_in_output_groups")
            && r["fingerprint"]["pairwise_in_output_groups"].is_array()
            && !r["fingerprint"]["pairwise_in_output_groups"].empty()) {
            o << "#### Pairwise Hamming (within emitted groups)\n\n";
            o << "| A | B | Hamming |\n| --- | --- | ---: |\n";
            for (const auto& p : r["fingerprint"]["pairwise_in_output_groups"]) {
                o << "| `" << p["path_a"].get<std::string>() << "` | `" << p["path_b"].get<std::string>() << "` | "
                  << p["hamming"].get<int>() << " |\n";
            }
            o << "\n";
        }
    }
    if (r.contains("meta_json_compare") && r["meta_json_compare"].is_object()) {
        o << "### Meta JSON LLM compare (OpenRouter / chat provider)\n\n";
        const auto& mjc = r["meta_json_compare"];
        if (mjc.contains("pairs") && mjc["pairs"].is_array() && !mjc["pairs"].empty()) {
            o << "| A | B | similarity (0–10) | notes |\n| --- | --- | ---: | --- |\n";
            for (const auto& p : mjc["pairs"]) {
                o << "| `" << p["path_a"].get<std::string>() << "` | `" << p["path_b"].get<std::string>() << "` | "
                  << p["similarity_0_10"].dump() << " | "
                  << (p.contains("notes") && p["notes"].is_string() ? p["notes"].get<std::string>() : std::string{})
                  << " |\n";
            }
            o << "\n";
        }
    }
    if (r.contains("meta") && r["meta"].is_object() && r["meta"].contains("per_file")) {
        o << "### Meta (sidecar + optional EXIF + hash)\n\n";
        for (const auto& row : r["meta"]["per_file"]) {
            o << "#### `" << row["path"].get<std::string>() << "`\n\n";
            if (row.contains("empty_after_normalize") && row["empty_after_normalize"].is_boolean() && row["empty_after_normalize"].get<bool>()) {
                o << "- *Empty corpus after normalize*\n- `corpus_len_before_norm` = "
                  << (row.contains("corpus_len_before_norm") && row["corpus_len_before_norm"].is_number_integer() ? row["corpus_len_before_norm"].get<int>() : 0) << "\n\n";
            } else {
                o << "- `sha256_corpus`: `" << row["sha256_corpus"].get<std::string>() << "`\n";
                o << "- `corpus_char_length`: " << row["corpus_char_length"].get<int>() << "\n\n";
                o << "Corpus preview (after normalize, truncated in JSON to 4k + marker):\n\n";
                o << "~~~~\n"
                  << (row.contains("corpus_preview_markdown") ? row["corpus_preview_markdown"].get<std::string>() : std::string{}) << "\n~~~~\n\n";
            }
        }
    }
    o << "## Full JSON (machine-readable)\n\nThis section is the same structure as `--report-json` / `result.report`.\n\n";
    o << "```json\n" << r.dump(2) << "\n```\n";
    return o.str();
}

bool validate_duplicates_session_json(const json& j, std::string& err) {
    if (!j.is_object()) {
        err = "duplicates session: root must be a JSON object";
        return false;
    }
    const std::string kind = j.value("kind", "");
    const std::string tool = j.value("tool", "");
    if (!kind.empty() && kind != pm::brand::k_json_kind_duplicates_u8) {
        err = std::string("duplicates session: unknown kind (expected ") + pm::brand::k_json_kind_duplicates_u8 + ")";
        return false;
    }
    if (kind.empty() && tool != "duplicates") {
        err = "duplicates session: missing kind or tool:duplicates";
        return false;
    }
    int ver = 0;
    if (j.contains("format_version")
        && (j["format_version"].is_number_integer() || j["format_version"].is_number()))
        ver = j["format_version"].get<int>();
    if (ver < 1 || ver > 2) {
        err = "duplicates session: format_version must be 1 or 2";
        return false;
    }
    if (!j.contains("duplicate_map") && !j.contains("groups")) {
        err = "duplicates session: expected duplicate_map or groups";
        return false;
    }
    return true;
}

void apply_duplicates_options_from_json(const json& j, DuplicatesOptions& opts) {
    auto str = [&](const char* k, std::string& dst) {
        if (j.contains(k) && j[k].is_string()) dst = j[k].get<std::string>();
    };
    auto integer = [&](const char* k, int& dst) {
        if (j.contains(k) && (j[k].is_number_integer() || j[k].is_number_float()))
            dst = j[k].get<int>();
    };
    auto boolean = [&](const char* k, bool& dst) {
        if (!j.contains(k) || j[k].is_null()) return;
        if (j[k].is_boolean())
            dst = j[k].get<bool>();
        else if (j[k].is_number_integer()) dst = j[k].get<int>() != 0;
    };
    if (j.contains("mode") && j["mode"].is_string()) {
        std::string s = j["mode"].get<std::string>();
        for (char& c : s) c = (char)std::tolower((unsigned char)c);
        if (s == "size")
            opts.mode = DuplicatesMode::Size;
        else if (s == "fingerprint" || s == "fp")
            opts.mode = DuplicatesMode::Fingerprint;
        else if (s == "meta")
            opts.mode = DuplicatesMode::Meta;
    }
    boolean("recursive", opts.recursive);
    integer("min_group_size", opts.min_group_size);
    integer("max_hamming", opts.max_hamming);
    boolean("fingerprint_same_size_only", opts.fingerprint_same_size_only);
    boolean("use_md", opts.use_md);
    boolean("use_json", opts.use_json);
    boolean("use_exif", opts.use_exif);
    str("meta_prompt", opts.meta_prompt);
    boolean("meta_json_llm_compare", opts.meta_json_llm_compare);
    boolean("meta_json_implicit_generate", opts.meta_json_implicit_generate);
    integer("meta_json_min_similarity", opts.meta_json_min_similarity);
    str("meta_json_compare_prompt", opts.meta_json_compare_prompt);
    str("llm_router", opts.llm_router);
    str("llm_model", opts.llm_model);
    str("llm_base_url", opts.llm_base_url);
    str("llm_api_key", opts.llm_api_key);
    integer("llm_timeout_ms", opts.llm_timeout_ms);
    boolean("detailed_report", opts.detailed_report);
    if (j.contains("meta") && j["meta"].is_object()) {
        apply_meta_options_from_json(j["meta"], opts.meta);
    }
}

} // namespace media
