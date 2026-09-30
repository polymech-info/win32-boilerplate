#include "glob_paths.hpp"

#include "output_path.hpp"
#include "url_fetch.hpp"

#include <glob/glob.h>

#include <algorithm>
#include <cctype>
#include <set>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

namespace media {

namespace {

bool trailing_sep(const std::string &s) {
    return !s.empty() && (s.back() == '/' || s.back() == '\\');
}

static void trim_in_place(std::string &s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.erase(0, 1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.pop_back();
}

} // anonymous namespace — closed early so expand_home_dir is in media:: scope

std::string expand_home_dir(std::string s) {
    if (s.empty() || s[0] != '~') return s;
    // Only expand bare `~` or `~/path` — leave `~user/path` alone.
    if (s.size() >= 2 && s[1] != '/' && s[1] != '\\') return s;

    std::string home;
#if defined(_WIN32)
    if (const char* h = std::getenv("USERPROFILE"); h && *h) {
        home = h;
    } else {
        const char* drv = std::getenv("HOMEDRIVE");
        const char* pth = std::getenv("HOMEPATH");
        if (drv && pth && *drv && *pth) home = std::string(drv) + pth;
    }
#else
    if (const char* h = std::getenv("HOME"); h && *h) home = h;
#endif
    if (home.empty()) return s;          // can't resolve — leave unchanged
    if (s.size() == 1) return home;      // bare `~`
    return home + s.substr(1);           // `~/path` → `<home>/path`
}

namespace {

void replace_all(std::string &s, const std::string &from, const std::string &to) {
    if (from.empty())
        return;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.length(), to);
        pos += to.length();
    }
}

/** Apply `${SRC_*}` and `&{SRC_*}` using absolute input path or URL parts. */
fs::path resolve_dst_template(const std::string &input_spec, const std::string &tmpl) {
    std::string dir;
    std::string stem;
    std::string ext;
    if (is_http_url(input_spec)) {
        url_template_variables(input_spec, dir, stem, ext);
    } else {
        const fs::path input_abs = fs::absolute(fs::path(input_spec));
        dir = input_abs.parent_path().generic_string();
        stem = input_abs.stem().string();
        ext = input_abs.extension().string();
    }

    std::string s = tmpl;
    replace_all(s, "${SRC_FILE_EXT}", ext);
    replace_all(s, "&{SRC_FILE_EXT}", ext);
    replace_all(s, "${SRC_NAME}", stem);
    replace_all(s, "&{SRC_NAME}", stem);
    replace_all(s, "${SRC_DIR}", dir);
    replace_all(s, "&{SRC_DIR}", dir);

    fs::path out(s);
    if (!out.is_absolute())
        out = fs::absolute(out);
    return out.lexically_normal();
}

} // namespace

bool has_dst_template(const std::string &output_spec) {
    return output_spec.find("${SRC_") != std::string::npos || output_spec.find("&{SRC_") != std::string::npos;
}

bool has_glob_tokens(const std::string &path) {
    return path.find('*') != std::string::npos || path.find('?') != std::string::npos;
}

namespace {

void split_path_segments(std::string_view s, std::vector<std::string_view> &out) {
    out.clear();
    std::size_t i = 0;
    while (i <= s.size()) {
        const std::size_t j = s.find('/', i);
        if (j == std::string_view::npos) {
            out.push_back(s.substr(i));
            break;
        }
        out.push_back(s.substr(i, j - i));
        i = j + 1;
    }
}

void collapse_adjacent_double_stars(std::vector<std::string_view> &tok) {
    std::vector<std::string_view> collapsed;
    collapsed.reserve(tok.size());
    for (std::string_view t : tok) {
        if (t == "**" && !collapsed.empty() && collapsed.back() == "**")
            continue;
        collapsed.push_back(t);
    }
    tok.swap(collapsed);
}

/** Shell-style * ? within one path component (no `/` in pattern). */
bool segment_matches(std::string_view seg, std::string_view pat) {
    std::size_t si = 0;
    std::size_t pi = 0;
    std::size_t star_si = std::string_view::npos;
    std::size_t star_pi = std::string_view::npos;

    while (si < seg.size()) {
        if (pi < pat.size() && (pat[pi] == '?' || pat[pi] == seg[si])) {
            ++pi;
            ++si;
            continue;
        }
        if (pi < pat.size() && pat[pi] == '*') {
            star_pi = pi++;
            star_si = si;
            continue;
        }
        if (star_si != std::string_view::npos) {
            pi = star_pi + 1;
            si = ++star_si;
            continue;
        }
        return false;
    }
    while (pi < pat.size() && pat[pi] == '*')
        ++pi;
    return pi == pat.size();
}

bool path_segments_match_glob(const std::vector<std::string_view> &path_segs, std::size_t pi,
                              const std::vector<std::string_view> &pat_segs, std::size_t ti) {
    if (ti == pat_segs.size())
        return pi == path_segs.size();
    if (pat_segs[ti] == "**") {
        if (ti + 1 == pat_segs.size())
            return true;
        if (path_segments_match_glob(path_segs, pi, pat_segs, ti + 1))
            return true;
        if (pi < path_segs.size() && path_segments_match_glob(path_segs, pi + 1, pat_segs, ti))
            return true;
        return false;
    }
    if (pi >= path_segs.size())
        return false;
    if (!segment_matches(path_segs[pi], pat_segs[ti]))
        return false;
    return path_segments_match_glob(path_segs, pi + 1, pat_segs, ti + 1);
}

} // namespace

bool path_matches_path_glob(std::string_view path_norm, std::string_view pattern_norm) {
    std::string pat;
    pat.reserve(pattern_norm.size());
    for (char c : pattern_norm) {
        if (c == '\\')
            pat.push_back('/');
        else
            pat.push_back(c);
    }
    std::string_view pv(pat);
    while (pv.size() >= 2 && pv[0] == '.' && pv[1] == '/')
        pv.remove_prefix(2);

    std::vector<std::string_view> path_segs;
    std::vector<std::string_view> pat_segs;
    split_path_segments(path_norm, path_segs);
    split_path_segments(pv, pat_segs);
    collapse_adjacent_double_stars(pat_segs);
    if (pat_segs.empty())
        return path_segs.empty();
    return path_segments_match_glob(path_segs, 0, pat_segs, 0);
}

std::vector<std::string> expand_input_paths(const std::string &input_spec, std::string &err_out) {
    err_out.clear();
    if (input_spec.empty()) {
        err_out = "input is empty";
        return {};
    }

    const std::string spec = expand_home_dir(input_spec);

    if (is_http_url(spec))
        return {spec};

    /** Windows Explorer multi-select: semicolon-separated absolute paths ( ';' is invalid in filenames ). */
    if (spec.find(';') != std::string::npos && !has_glob_tokens(spec)) {
        std::vector<std::string> parts;
        std::string cur;
        for (char c : spec) {
            if (c == ';') {
                trim_in_place(cur);
                if (!cur.empty())
                    parts.push_back(std::move(cur));
                cur.clear();
            } else {
                cur.push_back(c);
            }
        }
        trim_in_place(cur);
        if (!cur.empty())
            parts.push_back(std::move(cur));
        if (parts.size() < 2) {
            err_out = "invalid semicolon-separated input list";
            return {};
        }
        std::vector<std::string> out;
        out.reserve(parts.size());
        for (const auto &p : parts) {
            fs::path fp(p);
            std::error_code ec;
            if (!fs::is_regular_file(fp, ec) || ec) {
                err_out = "not a file: " + p;
                return {};
            }
            fs::path canon = fs::weakly_canonical(fs::absolute(fp), ec);
            if (ec)
                canon = fs::absolute(fp);
            out.push_back(canon.generic_string());
        }
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    }

    if (!has_glob_tokens(spec) && spec.find("**") == std::string::npos) {
        fs::path p = fs::absolute(fs::path(spec));
        std::error_code ec;
        if (fs::is_regular_file(p, ec))
            return {fs::weakly_canonical(p, ec).generic_string()};
        err_out = "input file not found: " + p.generic_string();
        return {};
    }

    fs::path pat_path(spec);
    fs::path resolved = pat_path.is_absolute() ? pat_path : fs::absolute(pat_path);
    resolved = resolved.lexically_normal();
    const std::string pat = resolved.string();

    std::vector<fs::path> matched;
    try {
        if (spec.find("**") != std::string::npos) {
            matched = glob::rglob(pat);
        } else {
            matched = glob::glob(pat);
        }
    } catch (const std::exception &e) {
        err_out = std::string("glob failed: ") + e.what();
        return {};
    }

    std::vector<std::string> out;
    std::unordered_set<std::string> seen;
    for (auto &p : matched) {
        std::error_code ec;
        if (!fs::is_regular_file(p, ec) || ec)
            continue;
        fs::path canon = fs::weakly_canonical(p, ec);
        if (ec)
            canon = p;
        const std::string key = canon.generic_string();
        if (seen.insert(key).second)
            out.push_back(key);
    }
    std::sort(out.begin(), out.end());
    if (out.empty())
        err_out = "glob matched no files: " + input_spec;
    return out;
}

namespace {

void ext_to_lower_ascii(std::string &e) {
    for (char &c : e) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
}

bool is_supported_image_extension(const std::string &ext_with_dot) {
    static const char *k[] = {".jpg",  ".jpeg", ".png",  ".gif", ".bmp", ".webp", ".tiff", ".tif",
                              ".jpe",  ".jfif", ".avif", ".heic", ".jf",
                              ".arw",  ".cr2",  ".cr3", ".nef", ".nrw", ".dng", ".orf", ".rw2",
                              ".raf",  ".pef",  ".srw", ".x3f", ".3fr", ".mef", ".mrw"};
    std::string e = ext_with_dot;
    ext_to_lower_ascii(e);
    for (const char *ref : k) {
        if (e == ref)
            return true;
    }
    return false;
}

void collect_images_recursive(const fs::path &dir, std::set<std::string> &out) {
    std::error_code ec;
    fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
    if (ec)
        return;
    const fs::recursive_directory_iterator end;
    for (; it != end; it.increment(ec)) {
        if (ec)
            break;
        const fs::directory_entry &ent = *it;
        std::error_code fe;
        if (!ent.is_regular_file(fe) || fe)
            continue;
        const std::string ext = ent.path().extension().string();
        if (!is_supported_image_extension(ext))
            continue;
        fs::path canon = fs::weakly_canonical(ent.path(), fe);
        if (fe)
            canon = fs::absolute(ent.path());
        out.insert(canon.generic_string());
    }
}

std::vector<std::string> split_semicolon_paths(const std::string &input_spec) {
    std::vector<std::string> parts;
    std::string cur;
    for (char c : input_spec) {
        if (c == ';') {
            trim_in_place(cur);
            if (!cur.empty())
                parts.push_back(std::move(cur));
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    trim_in_place(cur);
    if (!cur.empty())
        parts.push_back(std::move(cur));
    return parts;
}

/** True if the path's final component has a known image extension — explicit output file (not a folder name). */
bool path_leaf_looks_like_explicit_image_file(const fs::path &p) {
    std::string ext = p.extension().string();
    if (ext.empty())
        return false;
    ext_to_lower_ascii(ext);
    static const char *k[] = {".jpg",  ".jpeg", ".png",  ".gif", ".bmp", ".webp", ".tiff", ".tif",
                              ".jpe",  ".jfif", ".avif", ".heic", ".jf"};
    for (const char *ref : k) {
        if (ext == ref)
            return true;
    }
    return false;
}

std::vector<std::string> expand_one_cli_src_impl(const std::string &segment, std::string &err_out) {
    err_out.clear();
    std::string s = segment;
    trim_in_place(s);
    if (s.empty()) {
        err_out = "input is empty";
        return {};
    }
    s = expand_home_dir(s);
    if (is_http_url(s))
        return {s};

    if (has_glob_tokens(s) || s.find("**") != std::string::npos)
        return expand_input_paths(s, err_out);

    fs::path p(s);
    std::error_code ec;
    if (fs::is_directory(p, ec)) {
        std::set<std::string> collected;
        collect_images_recursive(p, collected);
        if (collected.empty()) {
            err_out = "no image files in directory: " + s;
            return {};
        }
        std::vector<std::string> out(collected.begin(), collected.end());
        std::sort(out.begin(), out.end());
        return out;
    }
    if (fs::is_regular_file(p, ec)) {
        fs::path canon = fs::weakly_canonical(fs::absolute(p), ec);
        if (ec)
            canon = fs::absolute(p);
        return {canon.generic_string()};
    }
    err_out = "not found: " + s;
    return {};
}

} // namespace

std::vector<std::string> expand_one_cli_src(const std::string &segment, std::string &err_out) {
    return expand_one_cli_src_impl(segment, err_out);
}

bool path_matches_gallery_glob(const fs::path &path, GalleryGlob glob) {
    switch (glob) {
    case GalleryGlob::IMAGES:
        return is_supported_image_extension(path.extension().string());
    }
    return false;
}

std::string expand_resize_ui_inputs(const std::string &input_spec, std::string &err_out) {
    err_out.clear();
    if (input_spec.empty()) {
        err_out = "input is empty";
        return {};
    }
    if (is_http_url(input_spec))
        return input_spec;
    if (has_glob_tokens(input_spec) || input_spec.find("**") != std::string::npos)
        return input_spec;

    std::vector<std::string> segments;
    if (input_spec.find(';') == std::string::npos)
        segments.push_back(input_spec);
    else
        segments = split_semicolon_paths(input_spec);

    std::set<std::string> collected;
    for (const auto &seg : segments) {
        std::string s = seg;
        trim_in_place(s);
        if (s.empty())
            continue;
        fs::path p(s);
        std::error_code ec;
        if (fs::is_directory(p, ec)) {
            collect_images_recursive(p, collected);
        } else if (fs::is_regular_file(p, ec)) {
            fs::path canon = fs::weakly_canonical(fs::absolute(p), ec);
            if (ec)
                canon = fs::absolute(p);
            collected.insert(canon.generic_string());
        } else {
            err_out = "not found: " + s;
            return {};
        }
    }

    if (collected.empty()) {
        err_out = "resize: no image files in selection";
        return {};
    }

    std::string joined;
    for (const auto &path : collected) {
        if (!joined.empty())
            joined.push_back(';');
        joined += path;
    }
    return joined;
}

std::vector<std::pair<std::string, fs::path>> pair_resize_paths(const std::string &input_spec,
                                                                const std::string &output_spec,
                                                                std::string &err_out,
                                                                const std::string *implicit_format_cli,
                                                                const std::string *implicit_stem_suffix) {
    err_out.clear();
    if (output_spec.empty()) {
        if (!implicit_format_cli) {
            err_out = "output is empty";
            return {};
        }
        std::vector<std::string> inputs = expand_input_paths(input_spec, err_out);
        if (!err_out.empty())
            return {};
        if (inputs.empty()) {
            err_out = "resize: no input files";
            return {};
        }
        const std::string empty_stem;
        const std::string &stem_ref = implicit_stem_suffix ? *implicit_stem_suffix : empty_stem;
        std::vector<std::pair<std::string, fs::path>> implicit_pairs;
        implicit_pairs.reserve(inputs.size());
        for (const auto &in : inputs) {
            std::string one_err;
            std::string out_str = default_output_path_for_one_input(in, *implicit_format_cli, one_err, stem_ref);
            if (!one_err.empty()) {
                err_out = one_err;
                return {};
            }
            if (out_str.empty()) {
                err_out = "resize: could not derive default output for: " + in;
                return {};
            }
            implicit_pairs.emplace_back(in, fs::path(out_str));
        }
        return implicit_pairs;
    }

    std::vector<std::string> inputs = expand_input_paths(input_spec, err_out);
    if (inputs.empty())
        return {};

    if (has_dst_template(output_spec)) {
        std::vector<std::pair<std::string, fs::path>> pairs;
        pairs.reserve(inputs.size());
        for (const auto &in : inputs)
            pairs.emplace_back(in, resolve_dst_template(in, output_spec));
        return pairs;
    }

    fs::path out_root(output_spec);
    std::error_code ec;

    if (inputs.size() == 1) {
        const std::string &in = inputs[0];
        bool out_is_dir = trailing_sep(output_spec);
        if (!out_is_dir) {
            if (fs::exists(out_root, ec)) {
                if (fs::is_directory(out_root, ec))
                    out_is_dir = true;
            } else {
                // Non-existent path: treat as output folder unless the leaf looks like "file.jpg", etc.
                if (!path_leaf_looks_like_explicit_image_file(out_root))
                    out_is_dir = true;
            }
        }
        if (out_is_dir) {
            fs::path dir = fs::absolute(out_root);
            if (is_http_url(in))
                return {{in, dir / url_suggested_filename(in)}};
            return {{in, dir / fs::path(in).filename()}};
        }
        return {{in, fs::absolute(out_root)}};
    }

    const bool force_dir = trailing_sep(output_spec);
    if (fs::exists(out_root, ec) && fs::is_regular_file(out_root, ec)) {
        err_out = "multiple inputs require output to be a directory, not a file: " + output_spec;
        return {};
    }
    if (!force_dir) {
        if (fs::exists(out_root, ec)) {
            if (!fs::is_directory(out_root, ec)) {
                err_out = "multiple inputs: output must be a directory: " + output_spec;
                return {};
            }
        } else {
            // New folder path without trailing sep: create later via create_directories (same idea as single-input).
            if (path_leaf_looks_like_explicit_image_file(out_root)) {
                err_out = "multiple inputs: output must be a directory, not a file path: " + output_spec;
                return {};
            }
        }
    }

    fs::path dir = fs::absolute(out_root);
    std::vector<std::pair<std::string, fs::path>> pairs;
    pairs.reserve(inputs.size());
    for (const auto &in : inputs) {
        if (is_http_url(in)) {
            err_out = "URL input cannot be combined with multiple outputs in one batch: " + in;
            return {};
        }
        pairs.emplace_back(in, dir / fs::path(in).filename());
    }
    return pairs;
}

} // namespace media
