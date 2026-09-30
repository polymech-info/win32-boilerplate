#include "search.hpp"
#include "cli_cancel.hpp"
#include "glob_paths.hpp"
#include "llm/llm_fs_guard.hpp"

#include "logger/logger.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace media {
namespace fs = std::filesystem;
using nlohmann::json;

// ─────────────────────────────────────────────────────────────────────────────
namespace {

// ── Defaults ──────────────────────────────────────────────────────────────────

const std::vector<std::string> k_default_exclude_dirs = {
    ".git", ".hg", ".svn",
    "node_modules", "dist", "build", "target",
    "bin", "obj", ".cache",
    ".next", ".nuxt", "vendor",
    "__pycache__", ".venv", "venv", ".tox",
};

// Max preview length when max_columns_limit is on (mirrors rg --max-columns 500).
static constexpr size_t k_max_columns = 500;

// ── Type-name → include-globs (mirrors rg --type) ────────────────────────────

static const std::vector<std::string>& type_to_globs(const std::string& type_name) {
    static const std::vector<std::string> empty;
    struct Entry { const char* name; std::vector<std::string> globs; };
    static const std::vector<Entry> k_map = {
        {"c",      {"*.c", "*.h"}},
        {"cpp",    {"*.cpp", "*.cxx", "*.cc", "*.c++", "*.h", "*.hpp", "*.hxx", "*.h++"}},
        {"cs",     {"*.cs"}},
        {"css",    {"*.css", "*.scss", "*.sass", "*.less"}},
        {"go",     {"*.go"}},
        {"html",   {"*.html", "*.htm", "*.xhtml"}},
        {"java",   {"*.java"}},
        {"js",     {"*.js", "*.mjs", "*.cjs"}},
        {"json",   {"*.json", "*.jsonc"}},
        {"kotlin", {"*.kt", "*.kts"}},
        {"md",     {"*.md", "*.markdown"}},
        {"py",     {"*.py", "*.pyi"}},
        {"rs",     {"*.rs"}},
        {"ruby",   {"*.rb", "*.rake"}},
        {"rust",   {"*.rs"}},
        {"sh",     {"*.sh", "*.bash", "*.zsh", "*.fish"}},
        {"swift",  {"*.swift"}},
        {"ts",     {"*.ts", "*.tsx", "*.mts", "*.cts"}},
        {"toml",   {"*.toml"}},
        {"txt",    {"*.txt"}},
        {"xml",    {"*.xml", "*.xsl", "*.xsd", "*.svg"}},
        {"yaml",   {"*.yaml", "*.yml"}},
    };
    for (const auto& e : k_map)
        if (type_name == e.name) return e.globs;
    return empty;
}

// ── String helpers ────────────────────────────────────────────────────────────

void str_lower_inplace(std::string& s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
}

std::string str_lower_copy(std::string s) {
    str_lower_inplace(s);
    return s;
}

std::string trim_line(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n'
                          || s.back() == ' ' || s.back() == '\t'))
        s.pop_back();
    return s;
}

// ── Image extension check (mirrors find.cpp) ──────────────────────────────────

bool ext_is_image(const std::string& ext) {
    static const char* k[] = {
        ".jpg",  ".jpeg", ".png",  ".gif",  ".bmp",
        ".webp", ".tiff", ".tif",  ".jpe",  ".jfif",
        ".avif", ".arw",  ".heic", ".jf",   ".cr2",
        ".cr3",  ".nef",  ".nrw",  ".dng",  ".orf",
        ".rw2",  ".raf",  ".pef",  ".srw",  ".x3f",
        ".3fr",  ".mef",  ".mrw",
        nullptr
    };
    const std::string e = str_lower_copy(ext);
    for (int i = 0; k[i]; ++i)
        if (e == k[i]) return true;
    return false;
}

// ── Binary probe: first 8 KB; true if NUL byte found ─────────────────────────

bool file_looks_binary(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    char buf[8192];
    const std::streamsize n = f.read(buf, sizeof(buf)).gcount();
    for (std::streamsize i = 0; i < n; ++i)
        if (buf[i] == '\0') return true;
    return false;
}

// ── Simple wildcard matcher (* and ?) ─────────────────────────────────────────

bool wildcard_match(const char* text, const char* pat) {
    const char* star = nullptr;
    const char* redo = text;
    while (*text) {
        if (*pat == '*') {
            star = pat++;
            redo = text;
        } else if (*pat == '?' || *pat == *text) {
            ++pat; ++text;
        } else if (star) {
            pat  = star + 1;
            text = ++redo;
        } else {
            return false;
        }
    }
    while (*pat == '*') ++pat;
    return !*pat;
}

bool glob_match_name(const std::string& name, const std::string& pat) {
#if defined(_WIN32)
    return wildcard_match(str_lower_copy(name).c_str(), str_lower_copy(pat).c_str());
#else
    return wildcard_match(name.c_str(), pat.c_str());
#endif
}

bool passes_include_globs(const std::string& fname,
                          const std::vector<std::string>& globs) {
    if (globs.empty()) return true;
    for (const auto& g : globs)
        if (glob_match_name(fname, g)) return true;
    return false;
}

bool passes_exclude_globs(const std::string& fname,
                          const std::vector<std::string>& globs) {
    for (const auto& g : globs)
        if (glob_match_name(fname, g)) return true;
    return false;
}

bool dir_is_excluded(const std::string& name,
                     const std::vector<std::string>& list) {
    for (const auto& d : list)
        if (name == d) return true;
    return false;
}

// ── Word boundary ─────────────────────────────────────────────────────────────

static bool is_word_char(char c) {
    return std::isalnum((unsigned char)c) || c == '_';
}

static bool is_whole_word(const std::string& s, size_t pos, size_t len) {
    if (pos > 0 && is_word_char(s[pos - 1])) return false;
    if (pos + len < s.size() && is_word_char(s[pos + len])) return false;
    return true;
}

// ── Candidate collection ──────────────────────────────────────────────────────

void collect_candidates(const std::string&               spec,
                        const SearchOptions&             opts,
                        const std::vector<std::string>&  eff_excl_dirs,
                        const std::vector<std::string>&  eff_include_globs,
                        std::set<std::string>&           out) {
    std::error_code ec;
    fs::path p = fs::u8path(expand_home_dir(spec));

    auto accept_file = [&](const fs::path& fp) {
        std::error_code fe;
        if (!fs::is_regular_file(fp, fe) || fe) return;
        if (!opts.include_hidden) {
            const std::string fn = fp.filename().string();
            if (!fn.empty() && fn[0] == '.') return;
        }
        if (opts.exclude_sensitive && media::llm::llm_is_sensitive_path(fp)) return;
        const std::string ext = fp.extension().string();
        if (opts.type == SearchType::Image && !ext_is_image(ext)) return;
        const std::string fname = fp.filename().string();
        // eff_include_globs merges opts.include_globs + type_filter expansion.
        if (!passes_include_globs(fname, eff_include_globs)) return;
        if (passes_exclude_globs(fname, opts.exclude_globs)) return;
        std::error_code wce;
        out.insert(fs::weakly_canonical(fp, wce).string());
    };

    if (fs::is_regular_file(p, ec)) {
        accept_file(p);
        return;
    }

    if (fs::is_directory(p, ec)) {
        if (opts.recursive) {
            fs::recursive_directory_iterator it(
                p, fs::directory_options::skip_permission_denied, ec);
            if (ec) return;
            for (; it != fs::recursive_directory_iterator(); it.increment(ec)) {
                if (ec) break;
                std::error_code ie;
                if (it->is_directory(ie) && !ie) {
                    const std::string dn = it->path().filename().string();
                    const bool hidden = !dn.empty() && dn[0] == '.';
                    if ((!opts.include_hidden && hidden)
                        || dir_is_excluded(dn, eff_excl_dirs)) {
                        it.disable_recursion_pending();
                    }
                } else {
                    accept_file(it->path());
                }
            }
        } else {
            fs::directory_iterator dit(p, ec);
            if (ec) return;
            for (const auto& ent : dit)
                accept_file(ent.path());
        }
        return;
    }

    // Glob spec — use the existing expander.
    std::string err;
    for (const auto& e : expand_input_paths(spec, err))
        accept_file(fs::u8path(e));
}

// ── Format a relative path for aggregate output ───────────────────────────────

static std::string make_relative_path(const std::string& abs,
                                       const std::vector<std::string>& inputs) {
    // Try to make the path relative to one of the input roots (shortest rel wins).
    std::string best = abs;
    std::error_code ec;
    const fs::path pa(abs);
    for (const auto& inp : inputs) {
        if (inp.find('*') != std::string::npos || inp.find('?') != std::string::npos)
            continue;
        const fs::path base = fs::is_directory(fs::u8path(inp), ec)
                               ? fs::u8path(inp) : fs::u8path(inp).parent_path();
        std::error_code rec;
        const fs::path rel = pa.lexically_relative(base);
        const std::string rs = rel.generic_string();
        if (!rs.empty() && rs.rfind("..", 0) != 0
            && (best == abs || rs.size() < best.size()))
            best = rs;
    }
    return best;
}

// ── Matcher (name + grep shared) ─────────────────────────────────────────────

struct Matcher {
    bool        use_regex      = false;
    bool        case_sensitive = false;
    bool        whole_word     = false;
    std::string needle;   // lowercased when !case_sensitive, literal mode
    std::regex  re;
    bool        regex_valid = false;

    bool init(const SearchOptions& opts) {
        use_regex      = opts.is_regex;
        case_sensitive = opts.case_sensitive;
        whole_word     = opts.whole_word;
        if (use_regex) {
            auto flags = std::regex_constants::ECMAScript | std::regex_constants::optimize;
            if (!case_sensitive) flags |= std::regex_constants::icase;
            // multiline: libstdc++ only exposes this from GCC 11 onward.
            // MSVC: disable multiline mode - std::regex_constants::multiline
            // may not be available depending on SDK/language version.
#if defined(_MSC_VER)
            (void)opts.multiline;  // MSVC: multiline not used
#elif defined(__GLIBCXX__) && (!defined(_GLIBCXX_RELEASE) || _GLIBCXX_RELEASE < 11)
            (void)opts.multiline;  // Old libstdc++: multiline not available
#else
            if (opts.multiline) flags |= std::regex_constants::multiline;
#endif
            try {
                re = std::regex(opts.query, flags);
                regex_valid = true;
            } catch (const std::regex_error&) {
                regex_valid = false;
                return false;
            }
        } else {
            needle = opts.query;
            if (!case_sensitive) str_lower_inplace(needle);
        }
        return true;
    }

    // Returns true when the string contains the query.
    bool test(const std::string& text) const {
        if (use_regex) {
            if (!regex_valid) return false;
            return std::regex_search(text, re);
        }
        const std::string hay = case_sensitive ? text : str_lower_copy(text);
        if (needle.empty()) return true;
        if (!whole_word) return hay.find(needle) != std::string::npos;
        size_t pos = 0;
        while ((pos = hay.find(needle, pos)) != std::string::npos) {
            if (is_whole_word(hay, pos, needle.size())) return true;
            ++pos;
        }
        return false;
    }

    struct Hit { size_t pos = 0; size_t len = 0; };

    // Find the first hit in `line`; returns false when not found.
    bool find_in(const std::string& line, Hit& hit) const {
        if (use_regex) {
            if (!regex_valid) return false;
            std::smatch m;
            if (!std::regex_search(line, m, re)) return false;
            hit.pos = (size_t)m.position(0);
            hit.len = (size_t)m.length(0);
            return true;
        }
        const std::string hay = case_sensitive ? line : str_lower_copy(line);
        if (needle.empty()) { hit = {0, 0}; return true; }
        size_t pos = 0;
        while ((pos = hay.find(needle, pos)) != std::string::npos) {
            if (!whole_word || is_whole_word(hay, pos, needle.size())) {
                hit = {pos, needle.size()};
                return true;
            }
            ++pos;
        }
        return false;
    }
};

// ── Grep a single file ────────────────────────────────────────────────────────

std::vector<SearchMatch> grep_file(const std::string& path,
                                    const SearchOptions& opts,
                                    const Matcher& m,
                                    SearchStats& stats,
                                    const std::function<bool()>& is_cancelled = {}) {
    std::vector<SearchMatch> matches;

    std::error_code ec;
    const uint64_t fsize = (uint64_t)fs::file_size(fs::u8path(path), ec);
    if (ec || fsize > opts.max_file_size_bytes) {
        ++stats.files_skipped;
        return {};
    }
    if (opts.skip_binary && file_looks_binary(path)) {
        ++stats.files_binary;
        return {};
    }

    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) { ++stats.files_skipped; return {}; }
    ++stats.files_scanned;

    // Effective context windows — symmetric -C takes priority over -B / -A.
    const int ctx_before = (opts.context_lines > 0) ? opts.context_lines : opts.context_before_lines;
    const int ctx_after  = (opts.context_lines > 0) ? opts.context_lines : opts.context_after_lines;
    const bool need_context = (ctx_before > 0 || ctx_after > 0);

    auto clamp_preview = [&](const std::string& s) -> std::string {
        if (opts.max_columns_limit && s.size() > k_max_columns)
            return s.substr(0, k_max_columns) + " [...]";
        return s;
    };

    auto add_match = [&](uint64_t lnum, size_t col, const std::string& preview) {
        SearchMatch sm;
        sm.path    = path;
        sm.line    = lnum;
        sm.column  = col + 1;
        sm.preview = clamp_preview(preview);
        sm.source  = "content";
        sm.score   = 1.0;
        matches.push_back(std::move(sm));
        ++stats.matches;
    };

    // Always read all lines when context is needed.
    if (need_context) {
        std::vector<std::string> all_lines;
        std::string line;
        uint64_t rline = 0;
        while (std::getline(ifs, line)) {
            ++rline;
            if ((rline & 255) == 0 && is_cancelled && is_cancelled()) return {};
            all_lines.push_back(trim_line(line));
        }

        for (int li = 0; li < (int)all_lines.size(); ++li) {
            if ((li & 255) == 0 && is_cancelled && is_cancelled()) return {};
            Matcher::Hit hit{};
            if (!m.find_in(all_lines[li], hit)) continue;
            add_match((uint64_t)(li + 1), hit.pos, all_lines[li]);
            auto& sm = matches.back();
            for (int b = std::max(0, li - ctx_before); b < li; ++b)
                sm.context_before.push_back(clamp_preview(all_lines[b]));
            for (int a = li + 1; a <= std::min((int)all_lines.size() - 1, li + ctx_after); ++a)
                sm.context_after.push_back(clamp_preview(all_lines[a]));
            if ((uint32_t)matches.size() >= opts.max_matches_per_file) break;
            if (opts.names_only) break;
        }
    } else {
        uint64_t lnum = 0;
        std::string line;
        while (std::getline(ifs, line)) {
            ++lnum;
            if ((lnum & 255) == 0 && is_cancelled && is_cancelled()) return {};
            const std::string disp = trim_line(line);
            Matcher::Hit hit{};
            if (m.find_in(line, hit)) {
                add_match(lnum, hit.pos, disp);
                if ((uint32_t)matches.size() >= opts.max_matches_per_file) break;
                if (opts.names_only) break;
            }
        }
    }
    return matches;
}

// ── OS indexer placeholder ────────────────────────────────────────────────────

bool try_os_indexer(const std::vector<std::string>& /*inputs*/,
                    const SearchOptions& /*opts*/,
                    SearchResult& /*res*/) {
    // Not yet implemented. The OS indexer path is scaffolded here.
    // Windows:  ISearchManager2 / ISearchCatalogManager / ISearchQueryHelper (WDS/Indexing Service)
    // macOS:    mdfind -interpret "<query>" in each root
    // Linux:    locate / plocate / fd
    return false;
}

} // anonymous namespace
// ─────────────────────────────────────────────────────────────────────────────

// ── Public: default exclude dirs ─────────────────────────────────────────────

const std::vector<std::string>& default_search_exclude_dirs() {
    return k_default_exclude_dirs;
}

// ── Public: main pipeline ────────────────────────────────────────────────────

SearchResult search_files(const std::vector<std::string>& inputs,
                           const SearchOptions&            opts,
                           SearchProgressFn                progress,
                           BatchControl*                   batch) {
    SearchResult res;

    if (opts.query.empty()) {
        res.error = "search: --query is required";
        return res;
    }

    // OS indexer (placeholder — falls through to own walker when unavailable).
    if (opts.indexer == SearchIndexer::Os) {
        if (try_os_indexer(inputs, opts, res)) {
            return res;
        }
        const std::string warn =
#if defined(_WIN32)
            "search: OS indexer (Windows Search) not yet implemented";
#elif defined(__APPLE__)
            "search: OS indexer (Spotlight/mdfind) not yet implemented";
#else
            "search: OS indexer (locate/plocate) not yet implemented";
#endif
        if (progress) progress("warning: " + warn + " — falling back to own walker");
        logger::warn(warn + " — falling back to own walker");
    }

    // Build matcher (validates regex early).
    Matcher matcher;
    if (!matcher.init(opts)) {
        res.error = "search: invalid regex: " + opts.query;
        return res;
    }

    // Effective exclude dirs.
    const std::vector<std::string>& excl_dirs =
        opts.exclude_dirs.empty() ? k_default_exclude_dirs : opts.exclude_dirs;

    // Effective include globs: merge explicit include_globs with type_filter expansion.
    std::vector<std::string> eff_include_globs = opts.include_globs;
    if (!opts.type_filter.empty()) {
        const auto& type_globs = type_to_globs(opts.type_filter);
        if (eff_include_globs.empty()) {
            eff_include_globs = type_globs;
        } else {
            // Both sets must be satisfied: we keep include_globs as-is and warn.
            // (If the caller sets both, they own the filter; type_filter is ignored.)
            logger::info("search: both include_globs and type_filter set; type_filter ignored");
        }
    }

    // Unified cancellation predicate: honours both CLI Ctrl+C and UI BatchControl.
    // The same lambda is forwarded into grep_file so very large files are also
    // interrupted at the per-chunk boundary (every 256 lines).
    auto is_cancelled = [&]() noexcept -> bool {
        return media::cli::cancel_requested()
            || (batch && batch->cancel.load(std::memory_order_acquire));
    };

    // Collect candidates (sorted, de-duplicated).
    std::set<std::string> path_set;
    for (const auto& spec : inputs) {
        if (is_cancelled()) {
            res.error = "search: cancelled";
            return res;
        }
        media::cli::yield_to_interrupt();
        collect_candidates(spec, opts, excl_dirs, eff_include_globs, path_set);
    }

    const std::vector<std::string> candidates(path_set.begin(), path_set.end());
    res.stats.files_visited = (int)candidates.size();

    if (progress)
        progress("Visiting " + std::to_string(res.stats.files_visited) + " file(s)");

    if (opts.dry_run) {
        res.ok = true;
        return res;
    }

    const bool do_grep = opts.grep;

    for (const auto& path : candidates) {
        if (is_cancelled()) {
            res.error = "search: cancelled";
            return res;
        }

        if (!do_grep) {
            // ── Name / path search ────────────────────────────────────────────
            const fs::path fp = fs::u8path(path);
            const std::string fname = fp.filename().string();

            if (matcher.test(fname)) {
                SearchMatch sm;
                sm.path   = path;
                sm.source = "name";
                sm.score  = 1.0;
                ++res.stats.matches;
                res.matches.push_back(std::move(sm));
            } else {
                // Also match against parent folder components.
                for (auto it = fp.parent_path();
                     !it.empty() && it != it.root_path();
                     it = it.parent_path()) {
                    const std::string dn = it.filename().string();
                    if (matcher.test(dn)) {
                        SearchMatch sm;
                        sm.path   = path;
                        sm.source = "folder";
                        sm.score  = 1.0;
                        ++res.stats.matches;
                        res.matches.push_back(std::move(sm));
                        break;
                    }
                }
            }
        } else {
            // ── Grep / content search ─────────────────────────────────────────
            auto file_matches = grep_file(path, opts, matcher, res.stats, is_cancelled);
            if (opts.names_only && !file_matches.empty()) {
                // Collapse to a single filename hit.
                SearchMatch sm;
                sm.path   = path;
                sm.source = "content";
                sm.score  = 1.0;
                res.matches.push_back(std::move(sm));
            } else {
                for (auto& sm : file_matches)
                    res.matches.push_back(std::move(sm));
            }
        }

        if (opts.max_results > 0 && (int)res.matches.size() >= opts.max_results)
            break;
    }

    {
        const std::string& type_s  = (opts.type == SearchType::Image) ? "image" : "any";
        const std::string& mode_s  = do_grep ? "grep" : "name";
        logger::info("search: type=" + type_s + " mode=" + mode_s
                     + " visited=" + std::to_string(res.stats.files_visited)
                     + " scanned=" + std::to_string(res.stats.files_scanned)
                     + " binary_skipped=" + std::to_string(res.stats.files_binary)
                     + " matches=" + std::to_string(res.stats.matches));
    }

    res.ok          = true;
    res.output_mode = opts.output_mode;
    res.total_matches = (int)res.matches.size();

    // ── Build aggregate output (for LLM tools / REST) ─────────────────────────
    // Apply offset + head_limit, then format according to output_mode.
    {
        const int total    = (int)res.matches.size();
        const int off      = (opts.offset > 0) ? opts.offset : 0;
        const int lim      = opts.head_limit;

        // Slice: matches[off .. off+lim).
        const int slice_start = std::min(off, total);
        const int slice_end   = (lim > 0) ? std::min(slice_start + lim, total) : total;
        const bool truncated  = (lim > 0) && (total - off) > lim;

        res.applied_offset = (off > 0) ? off : 0;
        res.applied_limit  = truncated ? lim : 0;

        if (opts.output_mode == SearchOutputMode::FilesWithMatches) {
            // Unique file paths in match order.
            std::set<std::string> seen;
            for (int i = slice_start; i < slice_end; ++i) {
                const std::string rp = make_relative_path(res.matches[i].path, inputs);
                if (seen.insert(rp).second)
                    res.file_paths.push_back(rp);
            }
        } else if (opts.output_mode == SearchOutputMode::Count) {
            // count per file, preserve match order of first occurrence.
            std::vector<std::string> order;
            std::map<std::string, int> counts;
            for (int i = slice_start; i < slice_end; ++i) {
                const std::string rp = make_relative_path(res.matches[i].path, inputs);
                if (counts.find(rp) == counts.end()) order.push_back(rp);
                counts[rp]++;
            }
            std::ostringstream ss;
            for (const auto& f : order)
                ss << f << ":" << counts[f] << "\n";
            res.content = ss.str();
            if (!res.content.empty() && res.content.back() == '\n')
                res.content.pop_back();
        } else {
            // Content mode — formatted as "path:line:col: preview" with context.
            std::ostringstream ss;
            for (int i = slice_start; i < slice_end; ++i) {
                const auto& sm = res.matches[i];
                const std::string rp = make_relative_path(sm.path, inputs);
                // context_before lines (prefixed with "-").
                for (const auto& cb : sm.context_before)
                    ss << rp << "-" << cb << "\n";
                // match line.
                if (sm.line > 0)
                    ss << rp << ":" << sm.line << ":" << sm.column << ": " << sm.preview << "\n";
                else
                    ss << rp << "\n";
                // context_after lines.
                for (const auto& ca : sm.context_after)
                    ss << rp << "-" << ca << "\n";
            }
            res.content = ss.str();
            if (!res.content.empty() && res.content.back() == '\n')
                res.content.pop_back();
        }
    }

    return res;
}

// ── JSON option mapping ───────────────────────────────────────────────────────

void apply_search_options_from_json(const json& j, SearchOptions& opts) {
    auto str  = [&](const char* k, std::string& dst) {
        if (j.contains(k) && j[k].is_string()) dst = j[k].get<std::string>();
    };
    auto bl   = [&](const char* k, bool& dst) {
        if (!j.contains(k) || j[k].is_null()) return;
        if (j[k].is_boolean())           dst = j[k].get<bool>();
        else if (j[k].is_number_integer()) dst = j[k].get<int>() != 0;
    };
    auto uint32 = [&](const char* k, uint32_t& dst) {
        if (j.contains(k) && j[k].is_number_integer())
            dst = (uint32_t)j[k].get<int>();
    };
    auto uint64 = [&](const char* k, uint64_t& dst) {
        if (j.contains(k) && j[k].is_number_integer())
            dst = (uint64_t)j[k].get<int64_t>();
    };
    auto integer = [&](const char* k, int& dst) {
        if (j.contains(k) && j[k].is_number_integer()) dst = j[k].get<int>();
    };
    auto strvec = [&](const char* k, std::vector<std::string>& dst) {
        if (!j.contains(k)) return;
        const auto& v = j[k];
        if (v.is_string()) { dst.push_back(v.get<std::string>()); return; }
        if (v.is_array())
            for (const auto& e : v) if (e.is_string())
                dst.push_back(e.get<std::string>());
    };

    str("query",       opts.query);
    str("pattern",     opts.query); // GrepTool alias: pattern == query
    bl ("is_regex",    opts.is_regex);
    bl ("case_sensitive", opts.case_sensitive);
    bl ("whole_word",  opts.whole_word);
    bl ("grep",        opts.grep);
    bl ("names_only",  opts.names_only);
    bl ("recursive",   opts.recursive);
    bl ("include_hidden",  opts.include_hidden);
    bl ("follow_symlinks", opts.follow_symlinks);
    bl ("skip_binary", opts.skip_binary);
    bl ("dry_run",     opts.dry_run);
    bl ("multiline",   opts.multiline);
    bl ("max_columns_limit", opts.max_columns_limit);
    uint64("max_file_size_bytes", opts.max_file_size_bytes);
    uint32("max_matches_per_file", opts.max_matches_per_file);
    integer("context_lines",        opts.context_lines);
    integer("context_before_lines", opts.context_before_lines);
    integer("context_after_lines",  opts.context_after_lines);
    integer("max_results",          opts.max_results);
    integer("head_limit",           opts.head_limit);
    integer("offset",               opts.offset);
    // GrepTool-style context aliases.
    integer("-C",  opts.context_lines);
    integer("-B",  opts.context_before_lines);
    integer("-A",  opts.context_after_lines);
    // -i flag (case insensitive).
    if (j.contains("-i") && !j["-i"].is_null()) {
        if (j["-i"].is_boolean())            opts.case_sensitive = !j["-i"].get<bool>();
        else if (j["-i"].is_number_integer()) opts.case_sensitive = (j["-i"].get<int>() == 0);
    }
    strvec("include_globs",  opts.include_globs);
    strvec("exclude_globs",  opts.exclude_globs);
    strvec("exclude_dirs",   opts.exclude_dirs);
    str("type_filter",       opts.type_filter);
    // "type" in GrepTool schema is a type-name (js, cpp, …), not SearchType.
    // We store it in type_filter for the glob expansion; only override SearchType
    // when the value is explicitly "image".
    if (j.contains("type") && j["type"].is_string()) {
        const std::string t = j["type"].get<std::string>();
        if (t == "image") {
            opts.type = SearchType::Image;
        } else if (t == "any") {
            opts.type = SearchType::Any;
        } else {
            opts.type_filter = t;
        }
    }
    if (j.contains("indexer") && j["indexer"].is_string()) {
        const std::string idx = j["indexer"].get<std::string>();
        opts.indexer = (idx == "os") ? SearchIndexer::Os : SearchIndexer::Own;
    }
    if (j.contains("output_mode") && j["output_mode"].is_string()) {
        const std::string om = j["output_mode"].get<std::string>();
        if (om == "files_with_matches")
            opts.output_mode = SearchOutputMode::FilesWithMatches;
        else if (om == "count")
            opts.output_mode = SearchOutputMode::Count;
        else
            opts.output_mode = SearchOutputMode::Content;
    }
}

} // namespace media
