#include "find.hpp"
#include "glob_paths.hpp"
#include "transform.hpp"

#include "logger/logger.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <curl/curl.h>
#include <vips/vips.h>

namespace media {
namespace fs = std::filesystem;
using nlohmann::json;

// ── Small helpers ─────────────────────────────────────────────────────────────

namespace {

// Pause/cancel between work units (not inside vips / curl / meta_extract).
inline void find_batch_gate(BatchControl* batch, const std::function<void()>& on_before_pause)
{
    if (!batch) return;
    if (batch->paused.load() && on_before_pause)
        on_before_pause();
    batch->check_pause();
}

inline bool find_batch_cancelled(BatchControl* batch) { return batch && batch->cancel.load(); }

bool ext_is_supported_image(const std::string& ext_with_dot) {
    static const char* k[] = {".jpg",  ".jpeg", ".png",  ".gif", ".bmp",
                               ".webp", ".tiff", ".tif",  ".jpe", ".jfif",
                               ".avif", ".arw",  ".heic", ".jf"};
    std::string e = ext_with_dot;
    for (char& c : e) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    for (const char* ref : k) if (e == ref) return true;
    return false;
}

void str_lower(std::string& s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
}

bool text_has_non_ws(const std::string& s) {
    for (unsigned char c : s) {
        if (!std::isspace(c)) return true;
    }
    return false;
}

std::uintmax_t file_size_if_regular(const fs::path& f) {
    std::error_code ec;
    if (!fs::exists(f, ec) || !fs::is_regular_file(f, ec)) return 0;
    return fs::file_size(f, ec);
}

bool contains_ci(std::string hay, std::string needle, bool case_insensitive) {
    if (needle.empty()) return false;
    if (case_insensitive) { str_lower(hay); str_lower(needle); }
    return hay.find(needle) != std::string::npos;
}

void collect_recursive(const fs::path& dir, std::set<std::string>& out) {
    std::error_code ec;
    fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
    if (ec) return;
    const fs::recursive_directory_iterator end;
    for (; it != end; it.increment(ec)) {
        if (ec) break;
        const auto& ent = *it;
        std::error_code fe;
        if (!ent.is_regular_file(fe) || fe) continue;
        const std::string ext = ent.path().extension().string();
        if (!ext_is_supported_image(ext)) continue;
        out.insert(fs::weakly_canonical(ent.path(), fe).string());
    }
}

void collect_one(const std::string& spec, bool recursive, std::set<std::string>& out) {
    std::error_code ec;
    fs::path p = fs::u8path(spec);
    if (fs::is_directory(p, ec)) {
        if (recursive) collect_recursive(p, out);
        else {
            for (auto& ent : fs::directory_iterator(p, ec)) {
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
    // Try treating as a glob — reuse the existing expander.
    std::string err;
    auto expanded = expand_input_paths(spec, err);
    for (auto& e : expanded) {
        fs::path q = fs::u8path(e);
        if (fs::is_regular_file(q, ec) && ext_is_supported_image(q.extension().string()))
            out.insert(fs::weakly_canonical(q, ec).string());
    }
}

std::string read_file_text(const fs::path& p) {
    std::ifstream ifs(p, std::ios::binary);
    if (!ifs) return {};
    std::ostringstream ss; ss << ifs.rdbuf();
    return ss.str();
}

// libvips EXIF as a flat "key: value\n" blob
std::string exif_text_for(const std::string& path) {
    VipsImage* img = vips_image_new_from_file(path.c_str(),
                                              "access", VIPS_ACCESS_SEQUENTIAL, nullptr);
    if (!img) { vips_error_clear(); return {}; }
    std::string out;
    out.reserve(512);
    out += "width: ";  out += std::to_string(vips_image_get_width(img));  out += '\n';
    out += "height: "; out += std::to_string(vips_image_get_height(img)); out += '\n';
    gchar** fields = vips_image_get_fields(img);
    if (fields) {
        for (gchar** p = fields; *p; ++p) {
            const std::string key(*p);
            if (key.rfind("exif-", 0) != 0) continue;
            char* val = nullptr;
            if (vips_image_get_as_string(img, key.c_str(), &val) != 0 || !val) continue;
            std::string v(val); g_free(val);
            auto paren = v.rfind(" (");
            if (paren != std::string::npos && v.back() == ')') v.erase(paren);
            out += key.substr(5);   // strip "exif-"
            out += ": ";
            out += v;
            out += '\n';
        }
        g_strfreev(fields);
    }
    g_object_unref(img);
    return out;
}

std::string sidecar_md(const std::string& image_path) {
    fs::path p(image_path);
    fs::path md = p; md.replace_extension(".md");
    std::error_code ec;
    if (fs::exists(md, ec)) return read_file_text(md);
    return {};
}
std::string sidecar_json(const std::string& image_path) {
    fs::path p(image_path);
    fs::path j = p; j.replace_extension(".json");
    std::error_code ec;
    if (fs::exists(j, ec)) return read_file_text(j);
    return {};
}
bool has_sidecars(const std::string& image_path, bool md, bool js) {
    const fs::path p(image_path);
    std::error_code ec;
    if (md) {
        fs::path m = p; m.replace_extension(".md");
        if (fs::exists(m, ec)) return true;
    }
    if (js) {
        fs::path j = p; j.replace_extension(".json");
        if (fs::exists(j, ec)) return true;
    }
    return false;
}

// ── libcurl plumbing for the judge call (kept local to find.cpp) ─────────────

size_t curl_write_string(void* ptr, size_t size, size_t nmemb, void* user) {
    auto* s = static_cast<std::string*>(user);
    s->append(static_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

// Minimal RFC 4648 base64 encoder (find.cpp keeps its own to avoid pulling
// transform.cpp's translation unit). Identical output.
std::string find_base64_encode(const unsigned char* data, std::size_t len) {
    static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.resize(((len + 2) / 3) * 4);
    std::size_t o = 0;
    for (std::size_t i = 0; i < len; i += 3) {
        unsigned a = data[i];
        unsigned b = (i + 1 < len) ? data[i + 1] : 0;
        unsigned c = (i + 2 < len) ? data[i + 2] : 0;
        out[o++] = tbl[a >> 2];
        out[o++] = tbl[((a & 3) << 4) | (b >> 4)];
        out[o++] = (i + 1 < len) ? tbl[((b & 0xF) << 2) | (c >> 6)] : '=';
        out[o++] = (i + 2 < len) ? tbl[c & 0x3F] : '=';
    }
    return out;
}

std::string mime_for_image_path(const std::string& path) {
    std::string e = std::filesystem::path(path).extension().string();
    for (char& c : e) c = (char)std::tolower((unsigned char)c);
    if (e == ".jpg" || e == ".jpeg" || e == ".jpe" || e == ".jfif") return "image/jpeg";
    if (e == ".png")  return "image/png";
    if (e == ".webp") return "image/webp";
    if (e == ".gif")  return "image/gif";
    if (e == ".bmp")  return "image/bmp";
    if (e == ".tif" || e == ".tiff") return "image/tiff";
    if (e == ".heic") return "image/heic";
    if (e == ".avif") return "image/avif";
    return "application/octet-stream";
}

bool call_judge_gemini(const MetaOptions& opts,
                       const std::string& prompt_text,
                       const std::vector<JudgeReference>& refs,
                       json& payload_out,
                       std::string& err,
                       const std::string& log_file = {})
{
    // Multimodal payload: judge prompt text first, then any reference images.
    json parts = json::array();
    parts.push_back({{"text", prompt_text}});
    for (const auto& r : refs) {
        if (r.bytes.empty()) continue;
        const std::string b64 = find_base64_encode(
            reinterpret_cast<const unsigned char*>(r.bytes.data()), r.bytes.size());
        parts.push_back({{"inlineData", {
            {"mimeType", r.mime.empty() ? "image/png" : r.mime},
            {"data",     b64}
        }}});
    }
    json req = {
        {"contents", json::array({ {{"parts", parts}} })},
        {"generationConfig", {
            {"responseMimeType", "application/json"}
        }}
    };

    const std::string url = resolve_gemini_generate_url(opts.base_url, opts.model);
    std::string body = req.dump();

    CURL* curl = curl_easy_init();
    if (!curl) { err = "curl_easy_init failed"; return false; }

    std::string response;
    struct curl_slist* hdrs = nullptr;
    hdrs = curl_slist_append(hdrs, "Content-Type: application/json");
    std::string auth = "x-goog-api-key: " + opts.api_key;
    hdrs = curl_slist_append(hdrs, auth.c_str());

    curl_easy_setopt(curl, CURLOPT_URL,            url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER,     hdrs);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS,     body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,  (long)body.size());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  curl_write_string);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,      &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,        45L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);

    const auto t0 = std::chrono::steady_clock::now();
    CURLcode cc = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0)
                        .count();
    {
        // LLM scores whether the *text corpus* (sidecar/EXIF) matches the search. This is NOT
        // `image_meta` / `Gemini[meta]` (that call describes the *image* and writes .md/.json).
        // No candidate image bytes here unless the find uses --reference image(s).
        std::string line = std::string("image_find[text match; NOT image_meta]: POST ") + url
                           + " model=" + opts.model + " HTTP " + std::to_string(http_code) + " in "
                           + std::to_string(ms) + " ms resp_bytes=" + std::to_string(response.size());
        if (!log_file.empty()) line += " file=" + log_file;
        logger::info(line);
    }
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);

    if (cc != CURLE_OK) { err = std::string("HTTP error: ") + curl_easy_strerror(cc); return false; }
    if (http_code != 200) {
        err = "API HTTP " + std::to_string(http_code);
        try {
            auto j = json::parse(response);
            if (j.contains("error") && j["error"].contains("message"))
                err += ": " + j["error"]["message"].get<std::string>();
        } catch (...) {
            if (response.size() < 800) err += ": " + response;
        }
        return false;
    }

    try {
        json resp = json::parse(response);
        std::string text;
        auto& parts = resp["candidates"][0]["content"]["parts"];
        for (auto& p : parts) {
            if (p.contains("text")) text += p["text"].get<std::string>();
        }
        if (text.empty()) { err = "empty model response"; return false; }
        payload_out = json::parse(text);
    } catch (const std::exception& e) {
        err = std::string("response parse error: ") + e.what();
        return false;
    }
    return true;
}

// Defensive accessors — judge responses can have wrong types.
double js_num(const json& j, const char* key, double dflt) {
    if (!j.contains(key) || j[key].is_null()) return dflt;
    const auto& v = j[key];
    if (v.is_number()) return v.get<double>();
    if (v.is_string()) {
        try { return std::stod(v.get<std::string>()); } catch (...) { return dflt; }
    }
    if (v.is_boolean()) return v.get<bool>() ? 1.0 : 0.0;
    return dflt;
}
bool js_bool(const json& j, const char* key, bool dflt) {
    if (!j.contains(key) || j[key].is_null()) return dflt;
    const auto& v = j[key];
    if (v.is_boolean()) return v.get<bool>();
    if (v.is_number())  return v.get<double>() != 0.0;
    if (v.is_string()) {
        std::string s = v.get<std::string>();
        for (char& c : s) c = (char)std::tolower((unsigned char)c);
        if (s == "true" || s == "yes" || s == "1") return true;
        if (s == "false"|| s == "no"  || s == "0") return false;
    }
    return dflt;
}
std::string js_str(const json& j, const char* key) {
    if (!j.contains(key) || j[key].is_null()) return {};
    const auto& v = j[key];
    if (v.is_string()) return v.get<std::string>();
    if (v.is_number()) return std::to_string(v.get<double>());
    if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
    return v.dump();
}

static std::string strip_trailing_punct_str(std::string w) {
    while (!w.empty() && (w.back() == '?' || w.back() == '!' || w.back() == '.')) w.pop_back();
    return w;
}

static bool is_stopword_for_find(const std::string& w) {
    static const char* s[] = {"a",  "an",  "the",  "in",  "on",  "at",  "of",  "and", "or",
                              "with", "to", "for", "is", "it",  "are", "be",  "as",  "by", "this",
                              "that",   "pics",  "pic",  "images",  "image",  "photo",  "photos",
                              "pictures", "picture", nullptr};
    for (int i = 0; s[i]; ++i)
        if (w == s[i]) return true;
    return false;
}

// Local match: filename + sidecar + EXIF text only; no LLM.
static bool local_corpus_matches_query(const std::string& corpus, const std::string& user_prompt) {
    if (user_prompt.empty()) return true;
    auto lo = [](const std::string& s) {
        std::string o;
        o.reserve(s.size());
        for (unsigned char u : s) o += (char)std::tolower(u);
        return o;
    };
    const std::string c = lo(corpus);
    const std::string p = lo(strip_trailing_punct_str(user_prompt));
    if (p.empty()) return true;
    if (c.find(p) != std::string::npos) return true;
    std::istringstream iss(user_prompt);
    std::string        w;
    int                need = 0;
    while (iss >> w) {
        w = lo(strip_trailing_punct_str(w));
        if (w.size() < 2) continue;
        if (is_stopword_for_find(w)) continue;
        ++need;
        if (c.find(w) == std::string::npos) return false;
    }
    if (need == 0) return c.find(p) != std::string::npos;
    return true;
}

static JudgeResult local_text_match_against_query(const std::string& corpus, const std::string& user_prompt) {
    JudgeResult r;
    r.ok     = true;
    r.error  = {};
    r.match  = local_corpus_matches_query(corpus, user_prompt);
    r.score  = r.match ? 0.95 : 0.0;
    r.reason = r.match ? "text match (filename + sidecar + exif, case-insensitive)"
                       : "no case-insensitive match for query in text";
    return r;
}

} // anonymous namespace

// ── Public: default judge prompt ─────────────────────────────────────────────

std::string default_find_judge_prompt() {
    return
"You are a strict image-search judge. You will receive METADATA describing a single\n"
"candidate image (its description, tags, scene, EXIF, the file `name:` line, etc.)\n"
"and a user SEARCH QUERY. Treat a match between the query and the file name or stem as\n"
"a valid match when the rest of the text does not contradict it.\n"
"You may also receive one or more REFERENCE IMAGES that the user wants the candidate\n"
"to resemble (style, subject, brand, layout). When references are present, treat them\n"
"as the primary signal and the user query as additional guidance.\n"
"Decide whether the candidate matches.\n"
"Reply ONLY with this JSON object (no prose, no markdown):\n"
"{\n"
"  \"match\": true|false,\n"
"  \"score\": 0.0..1.0,\n"
"  \"reason\": \"brief one-line justification grounded in the metadata or references\"\n"
"}\n"
"\"score\" is your confidence the candidate truly matches the query and references\n"
"(1.0 = certain match). Be conservative: when neither the metadata nor the references\n"
"support the query/subject, return false.\n";
}

// ── Public: standalone judge ─────────────────────────────────────────────────

JudgeResult match_metadata_against_prompt(const std::string& metadata_corpus,
                                          const std::string& user_prompt,
                                          const MetaOptions& meta_opts,
                                          const std::string& judge_prompt_override,
                                          const std::vector<JudgeReference>& refs,
                                          const std::string& candidate_path)
{
    JudgeResult r;
    if (user_prompt.empty() && refs.empty()) {
        r.error = "user_prompt empty (and no reference images)";
        return r;
    }
    if (meta_opts.api_key.empty()) { r.error = "API key required"; return r; }

    const std::string judge = judge_prompt_override.empty()
                              ? default_find_judge_prompt()
                              : judge_prompt_override;

    std::string full;
    full.reserve(metadata_corpus.size() + user_prompt.size() + judge.size() + 256);
    full += judge;
    full += "\n\n--- METADATA ---\n";
    full += metadata_corpus.empty() ? "(no metadata available)\n" : metadata_corpus;
    if (full.back() != '\n') full += '\n';
    full += "\n--- SEARCH QUERY ---\n";
    full += user_prompt.empty() ? "(no text query — match against the reference image(s))" : user_prompt;
    full += '\n';
    if (!refs.empty()) {
        full += "\nThe user attached " + std::to_string(refs.size())
              + " reference image(s) below. Match the candidate against them too.\n";
    }

    json payload;
    std::string err;
    if (!call_judge_gemini(meta_opts, full, refs, payload, err, candidate_path)) {
        r.error = err;
        return r;
    }

    r.ok     = true;
    r.match  = js_bool(payload, "match", false);
    r.score  = std::clamp(js_num(payload, "score", r.match ? 1.0 : 0.0), 0.0, 1.0);
    r.reason = js_str(payload, "reason");
    return r;
}

// ── Public: main pipeline ────────────────────────────────────────────────────

FindResult find_images(const std::vector<std::string>& inputs,
                        const FindOptions& opts,
                        FindProgressFn            progress,
                        BatchControl*             batch,
                        std::function<void()>     on_before_batch_pause)
{
    FindResult res;

    // LLM mode allows prompt OR references (or both). Name mode always needs
    // a textual prompt — there is no "by-image-name" search by definition.
    const bool has_refs = !opts.reference_images.empty()
                          && opts.mode == FindMode::Llm;
    if (opts.prompt.empty() && !has_refs) {
        res.error = (opts.mode == FindMode::Llm)
            ? "find (llm): a prompt or at least one --reference image is required"
            : "find (name): prompt is required";
        return res;
    }

    // Resolve candidates (sorted, de-duplicated).
    std::set<std::string> set;
    for (const auto& spec : inputs) collect_one(spec, opts.recursive, set);
    std::vector<std::string> candidates(set.begin(), set.end());
    res.scanned = (int)candidates.size();
    if (progress) progress("Scanned " + std::to_string(res.scanned) + " image(s)");

    find_batch_gate(batch, on_before_batch_pause);
    if (find_batch_cancelled(batch)) {
        res.error = "find: cancelled";
        return res;
    }

    // ── Name mode ───────────────────────────────────────────────────────────
    if (opts.mode == FindMode::Name) {
        for (const auto& path : candidates) {
            find_batch_gate(batch, on_before_batch_pause);
            if (find_batch_cancelled(batch)) {
                res.error = "find: cancelled";
                return res;
            }
            fs::path p(path);
            const std::string fname = p.filename().string();
            if (contains_ci(fname, opts.prompt, opts.case_insensitive)) {
                FindMatch m; m.path = path; m.score = 1.0; m.source = "name";
                m.reason = "filename contains \"" + opts.prompt + "\"";
                res.matches.push_back(std::move(m));
                continue;
            }
            if (opts.match_folders) {
                bool hit = false;
                for (auto it = p.parent_path(); !it.empty() && it != it.root_path(); it = it.parent_path()) {
                    if (contains_ci(it.filename().string(), opts.prompt, opts.case_insensitive)) {
                        hit = true; break;
                    }
                }
                if (hit) {
                    FindMatch m; m.path = path; m.score = 1.0; m.source = "folder";
                    m.reason = "parent folder contains \"" + opts.prompt + "\"";
                    res.matches.push_back(std::move(m));
                }
            }
        }
        if (opts.max_results > 0 && (int)res.matches.size() > opts.max_results)
            res.matches.resize(opts.max_results);
        res.ok = true;
        return res;
    }

    // ── LLM mode ────────────────────────────────────────────────────────────
    find_batch_gate(batch, on_before_batch_pause);
    if (find_batch_cancelled(batch)) {
        res.error = "find: cancelled";
        return res;
    }

    // Read reference images once (re-used for every candidate judge call).
    std::vector<JudgeReference> refs_buf;
    refs_buf.reserve(opts.reference_images.size());
    for (const auto& rp : opts.reference_images) {
        if (rp.empty()) continue;
        std::ifstream rf(rp, std::ios::binary);
        if (!rf) {
            res.error = "find (llm): cannot open reference image: " + rp;
            return res;
        }
        std::ostringstream rs;
        rs << rf.rdbuf();
        JudgeReference jr;
        jr.mime  = mime_for_image_path(rp);
        jr.bytes = rs.str();
        if (jr.bytes.empty()) {
            res.error = "find (llm): reference image is empty: " + rp;
            return res;
        }
        if (progress) progress("Reference loaded: " + rp + " (" + std::to_string(jr.bytes.size() / 1024) + " KB)");
        refs_buf.push_back(std::move(jr));
    }

    // References require multimodal find:judge; `find_semantic_judge: false` is a local-text path
    // only for prompt-only search.
    const bool use_llm_judge = opts.find_semantic_judge || !refs_buf.empty();

    if (!opts.dry_run && use_llm_judge && opts.meta.api_key.empty()) {
        res.error = "find (llm): API key required for LLM text match (set --api-key or provider key in app settings), "
                    "or `find --local-text`, or avoid `--reference` (visual match needs the model)";
        return res;
    }

    if (!opts.dry_run && !candidates.empty()) {
        std::string q = opts.prompt;
        if (q.size() > 100) q = q.substr(0, 97) + "...";
        if (use_llm_judge) {
            logger::info("find (llm): " + std::to_string(candidates.size()) + " image(s) — up to "
                         + std::to_string(candidates.size())
                         + " image_find[text match; NOT image_meta] / find:judge (Gemini) round(s) on text + refs. "
                         + (q.empty() ? std::string("query=(reference image(s) only)")
                                      : (std::string("query=\"") + q + "\"")));
        } else {
            logger::info("find (llm, local text match): " + std::to_string(candidates.size())
                         + " image(s) — no find:judge Gemini; substring/word match on filename, sidecar, exif. "
                         + (q.empty() ? std::string("(empty query)")
                                      : (std::string("query=\"") + q + "\"")));
        }
    }

    for (size_t i = 0; i < candidates.size(); ++i) {
        find_batch_gate(batch, on_before_batch_pause);
        if (find_batch_cancelled(batch)) {
            res.error = "find: cancelled";
            return res;
        }
        const auto& path = candidates[i];
        if (progress) {
            std::string note = "[" + std::to_string(i + 1) + "/"
                             + std::to_string(candidates.size()) + "] " + path;
            progress(note);
        }

        // 1. Sidecar lookup (skipped when bypass_cache is set so we always
        //    re-generate descriptive metadata in that path).
        //    Treat a non-empty .md or .json on disk as a cache hit even if the
        //    read returns empty/whitespace (locked file, etc.) to avoid re-running
        //    meta_extract and hammering Gemini.
        std::string corpus;
        bool        has_sidecar = false;
        if (!opts.bypass_cache) {
            const fs::path img_path(path);
            fs::path       md_p = img_path; md_p.replace_extension(".md");
            fs::path       js_p = img_path; js_p.replace_extension(".json");

            if (opts.use_md) {
                std::string md = sidecar_md(path);
                if (text_has_non_ws(md)) {
                    corpus += md;
                    corpus += '\n';
                    has_sidecar = true;
                } else if (file_size_if_regular(md_p) > 0) {
                    has_sidecar = true;
                }
            }
            if (opts.use_json) {
                std::string js = sidecar_json(path);
                if (text_has_non_ws(js)) {
                    corpus += js;
                    corpus += '\n';
                    has_sidecar = true;
                } else if (file_size_if_regular(js_p) > 0) {
                    has_sidecar = true;
                }
            }
        }
        if (has_sidecar) ++res.cache_hits;

        // 2. Generate descriptive metadata if no sidecar (or bypassing cache).
        //    EXIF alone is NOT enough to skip generation — it doesn't describe
        //    the scene, just camera fields.
        if ((opts.bypass_cache || !has_sidecar) && opts.generate && !opts.dry_run) {
            if (progress) progress(opts.bypass_cache
                                   ? "  re-generating .md/.json (--bypass-cache)"
                                   : "  generating .md/.json (no cache)");
            MetaOptions m = opts.meta;
            if (!m.out_md && !m.out_json && !m.update_exif) {
                m.out_md   = true;
                m.out_json = true;
            }
            auto mr = media::meta_extract(path, m);
            if (!mr.ok) {
                if (progress) progress("  meta failed: " + mr.error);
                continue;
            }
            ++res.generated;
            if (opts.bypass_cache) corpus.clear();   // fresh start
            corpus += mr.markdown;
            if (!mr.json_text.empty()) { corpus += '\n'; corpus += mr.json_text; }
        }

        // 3. Always append EXIF when allowed — it complements the description
        //    cheaply (camera, GPS, dimensions) and helps borderline judgements.
        if (opts.use_exif) {
            auto ex = exif_text_for(path);
            if (!ex.empty()) { corpus += '\n'; corpus += ex; }
        }

        // Sidecar prose often never repeats the filename (e.g. "chaise lounge" for
        // bench.jpg). The text judge only sees this corpus, so we always inject the
        // file name; otherwise a query "bench" misses despite the stem being obvious.
        {
            const fs::path p     = fs::u8path(path);
            const std::string head =
                std::string("--- file (name for search) ---\nname: ") + p.filename().string() + "\n\n";
            corpus = head + corpus;
        }

        if (corpus.empty()) {
            if (progress) progress("  no metadata available, skipping");
            continue;
        }

        ++res.considered;

        if (opts.dry_run) {
            FindMatch m; m.path = path; m.score = 0.0; m.source = use_llm_judge ? "llm" : "text";
            m.reason = std::string("(dry-run) ") + (use_llm_judge ? "llm" : "local")
                     + " corpus " + std::to_string(corpus.size()) + " bytes";
            res.matches.push_back(std::move(m));
            continue;
        }

        // Judge: LLM (semantic) vs local text match on the same corpus (not image_meta).
        JudgeResult jr;
        if (use_llm_judge) {
            jr = match_metadata_against_prompt(corpus, opts.prompt, opts.meta, opts.judge_prompt,
                                                refs_buf, path);
        } else {
            jr = local_text_match_against_query(corpus, opts.prompt);
        }
        if (!jr.ok) {
            if (progress) progress("  judge failed: " + jr.error);
            continue;
        }
        if (progress) {
            const char*   tag = use_llm_judge ? "LLM" : "text";
            std::string   n   = jr.match ? "  ✓ match " : "  ✗ no    ";
            n += "("; n += tag; n += " score " + std::to_string(jr.score).substr(0, 4) + ") ";
            n += jr.reason;
            progress(n);
        }
        if (jr.match) {
            FindMatch m; m.path = path; m.score = jr.score; m.source = use_llm_judge ? "llm" : "text";
            m.reason = jr.reason;
            res.matches.push_back(std::move(m));
        }
    }

    std::sort(res.matches.begin(), res.matches.end(),
              [](const FindMatch& a, const FindMatch& b) { return a.score > b.score; });
    if (opts.max_results > 0 && (int)res.matches.size() > opts.max_results)
        res.matches.resize(opts.max_results);

    if (opts.mode == FindMode::Llm && !opts.dry_run) {
        logger::info("find (llm): done — scanned=" + std::to_string(res.scanned)
                     + " sidecar_hits=" + std::to_string(res.cache_hits)
                     + " meta_generated=" + std::to_string(res.generated)
                     + " (Gemini[meta] only when generating missing sidecars; query match = "
                     + (use_llm_judge ? "LLM find:judge" : "local text") + ")");
    }

    res.ok = true;
    return res;
}

// ── JSON option mapping ──────────────────────────────────────────────────────

void apply_find_options_from_json(const json& j, FindOptions& opts) {
    auto str = [&](const char* k, std::string& dst) {
        if (j.contains(k) && j[k].is_string()) dst = j[k].get<std::string>();
    };
    auto integer = [&](const char* k, int& dst) {
        if (j.contains(k) && j[k].is_number_integer()) dst = j[k].get<int>();
    };
    auto boolean = [&](const char* k, bool& dst) {
        if (!j.contains(k) || j[k].is_null()) return;
        if (j[k].is_boolean())          dst = j[k].get<bool>();
        else if (j[k].is_number_integer()) dst = j[k].get<int>() != 0;
    };

    if (j.contains("mode") && j["mode"].is_string()) {
        std::string m = j["mode"].get<std::string>();
        for (char& c : m) c = (char)std::tolower((unsigned char)c);
        if (m == "llm") opts.mode = FindMode::Llm;
        else            opts.mode = FindMode::Name;
    }
    str    ("prompt",       opts.prompt);
    boolean("case_insensitive", opts.case_insensitive);
    boolean("match_folders",    opts.match_folders);
    boolean("recursive",        opts.recursive);
    boolean("bypass_cache",     opts.bypass_cache);
    boolean("generate",         opts.generate);
    boolean("use_md",           opts.use_md);
    boolean("use_json",         opts.use_json);
    boolean("use_exif",         opts.use_exif);
    str    ("judge_prompt",     opts.judge_prompt);
    integer("max_results",      opts.max_results);
    boolean("dry_run",          opts.dry_run);
    boolean("find_semantic_judge", opts.find_semantic_judge);

    json meta_j = json::object();
    if (j.contains("meta") && j["meta"].is_object())
        meta_j = j["meta"];
    apply_meta_options_from_json(meta_j, opts.meta);

    // Reference images: accept "reference_images", or aliases "references" /
    // "reference" (mirrors the transform JSON shape).
    auto take_refs = [&](const json& v) {
        if (v.is_string()) {
            opts.reference_images.push_back(v.get<std::string>());
        } else if (v.is_array()) {
            for (const auto& it : v) if (it.is_string())
                opts.reference_images.push_back(it.get<std::string>());
        }
    };
    if (j.contains("reference_images")) take_refs(j["reference_images"]);
    else if (j.contains("references"))  take_refs(j["references"]);
    else if (j.contains("reference"))   take_refs(j["reference"]);
}

} // namespace media
