#include "path_sanitizer.hpp"

#include "url_fetch.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <regex>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace media::path {

namespace {

void trim_in_place(std::string &s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.erase(0, 1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.pop_back();
}

void decode_percent_escapes(std::string &s) {
    for (int p = 0; p < 4; ++p) {
        for (size_t i = 0; i + 2 < s.size(); ++i) {
            if (s[i] != '%') continue;
            const char a  = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i + 1])));
            const char b2 = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i + 2])));
            if (a == '2' && b2 == 'e') {
                s.replace(i, 3, 1, '.');
                i += 2;
            }
        }
        for (size_t i = 0; i + 2 < s.size(); ++i) {
            if (s[i] != '%') continue;
            const char a  = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i + 1])));
            const char b2 = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i + 2])));
            if (a == '2' && b2 == 'f') {
                s.replace(i, 3, 1, '/');
                i += 2;
            }
        }
        for (size_t i = 0; i + 2 < s.size(); ++i) {
            if (s[i] != '%') continue;
            const char a  = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i + 1])));
            const char b2 = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i + 2])));
            if (a == '5' && b2 == 'c') {
                s.replace(i, 3, 1, '\\');
                i += 2;
            }
        }
    }
}

void strip_not_allowed_for_path(std::string &s) {
    static const std::regex not_allowed(R"([:$!'"`+|=])");
    s = std::regex_replace(s, not_allowed, std::string());
}

void slash_unify(std::string &s) {
    for (char &c : s) {
        if (c == '\\') c = '/';
    }
}

bool has_double_percent_encoding(const std::string &s) {
    for (size_t i = 0; i + 2 < s.size(); ++i) {
        if (s[i] != '%') continue;
        if (std::tolower(static_cast<unsigned char>(s[i + 1])) == '2'
            && std::tolower(static_cast<unsigned char>(s[i + 2])) == '5')
            return true;
    }
    return false;
}

bool home_escape(const std::string &s) { return s == "~" || (s.size() >= 2 && s[0] == '~' && s[1] == '/'); }

void decode_strip_slashform(const std::string &in, std::string &out) {
    out = in;
    decode_percent_escapes(out);
    strip_not_allowed_for_path(out);
    slash_unify(out);
}

/** Ref `sanitizeSubpath` traversal block (path-sanitizer.ts 111–120). */
bool subpath_traversal_violation(const std::string &normalized, std::string &err) {
    err.clear();
    const std::string   probe  = normalized + std::string("/");
    static const std::regex mid(R"(([/\\])\.\.([/\\]))");
    if (std::regex_search(probe, mid)) {
        err = "EFORBIDDEN: path traversal";
        return true;
    }
    if (normalized.rfind("../", 0) == 0) {
        err = "EFORBIDDEN: path traversal";
        return true;
    }
    if (normalized == "..") {
        err = "EFORBIDDEN: path traversal";
        return true;
    }
    if (normalized.size() >= 3 && normalized.substr(normalized.size() - 3) == "/..") {
        err = "EFORBIDDEN: path traversal";
        return true;
    }
    const std::string t = std::string("/") + normalized + std::string("/");
    if (t.find("/../") != std::string::npos) {
        err = "EFORBIDDEN: path traversal";
        return true;
    }
    return false;
}

bool pre_checks_vfs(const std::string &input, std::string &err) {
    if (input.find('\0') != std::string::npos) {
        err = "EFORBIDDEN: null byte in path";
        return false;
    }
    if (has_double_percent_encoding(input)) {
        err = "EFORBIDDEN: double-encoded path segment";
        return false;
    }
    if (home_escape(input)) {
        err = "EFORBIDDEN: home directory access denied";
        return false;
    }
    return true;
}

void utf8_truncate_255(std::string &s) {
    if (s.size() <= 255) return;
    s.resize(255);
    while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xc0) == 0x80) s.pop_back();
}

static void trim_segs_edges(std::string &seg) {
    while (!seg.empty() && (seg.front() == ' ' || seg.back() == ' ')) {
        if (seg.front() == ' ') seg.erase(0, 1);
        if (!seg.empty() && seg.back() == ' ') seg.pop_back();
    }
}

static std::string clean_write_segment(const std::string &seg, bool is_last, bool all_dirs) {
    static const std::regex k_unsafe(R"([^a-zA-Z0-9_\- @])");
    std::string out;
    if (is_last && !all_dirs) {
        const size_t last_dot = seg.rfind('.');
        if (last_dot > 0 && last_dot < seg.size() - 1) {
            std::string name = std::regex_replace(seg.substr(0, last_dot), k_unsafe, std::string());
            name.erase(std::remove(name.begin(), name.end(), '.'), name.end());
            std::string ext = std::regex_replace(seg.substr(last_dot + 1), k_unsafe, std::string());
            out = ext.empty() ? name : (name + "." + ext);
        } else
            out = std::regex_replace(seg, k_unsafe, std::string());
    } else {
        out = std::regex_replace(seg, k_unsafe, std::string());
        out.erase(std::remove(out.begin(), out.end(), '.'), out.end());
    }
    trim_segs_edges(out);
    if (out.size() > 200) out = out.substr(0, 200);
    trim_segs_edges(out);
    for (const char c : out) {
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') return out;
    }
    return std::string();
}

} // namespace

std::string sanitize_subpath(const std::string &input, std::string &err) {
    err.clear();
    std::string t = input;
    trim_in_place(t);
    if (t.empty()) return std::string();

    if (!pre_checks_vfs(t, err)) return std::string();

    std::string normalized;
    decode_strip_slashform(t, normalized);
    if (subpath_traversal_violation(normalized, err)) return std::string();

    std::string cleaned = normalized;
    while (!cleaned.empty() && cleaned[0] == '/')
        cleaned.erase(0, 1);
    if (cleaned.size() >= 2) {
        const char c0 = static_cast<char>(std::tolower(static_cast<unsigned char>(cleaned[0])));
        if (c0 >= 'a' && c0 <= 'z' && cleaned[1] == ':') {
            err = "EFORBIDDEN: absolute path";
            return std::string();
        }
    }

    std::string sanitized = cleaned;
    for (size_t i = 0; i < sanitized.size();) {
        if (sanitized[i] == '/') {
            size_t j = i + 1;
            while (j < sanitized.size() && sanitized[j] == '/') ++j;
            if (j > i + 1)
                sanitized.erase(i + 1, j - (i + 1));
            else
                ++i;
        } else
            ++i;
    }
    {
        const fs::path p(sanitized);
        const fs::path n = p.lexically_normal();
        sanitized = n.generic_string();
    }
    slash_unify(sanitized);
    while (!sanitized.empty() && sanitized[0] == '/') sanitized.erase(0, 1);
    while (!sanitized.empty() && sanitized.back() == '/') sanitized.pop_back();
    if (sanitized == ".") sanitized.clear();

    size_t a = 0;
    for (size_t i = 0; i <= sanitized.size(); ++i) {
        if (i == sanitized.size() || sanitized[i] == '/') {
            if (a < i) {
                const std::string seg = sanitized.substr(a, i - a);
                if (seg == "..") {
                    err = "EFORBIDDEN: path traversal survived normalization";
                    return std::string();
                }
            }
            a = i + 1;
        }
    }
    return sanitized;
}

std::string sanitize_write_path(const std::string &subpath, std::string &err, bool is_directory) {
    (void)err;
    if (subpath.empty()) return std::string();
    const bool all_dirs = is_directory;
    std::string work   = subpath;
    for (char &c : work) {
        if (c == '\\') c = '/';
    }
    std::vector<std::string> segs;
    {
        size_t a = 0;
        for (size_t i = 0; i <= work.size(); ++i) {
            if (i == work.size() || work[i] == '/') {
                if (a < i) segs.push_back(work.substr(a, i - a));
                a = i + 1;
            }
        }
    }
    std::vector<std::string> cleaned;
    for (size_t i = 0; i < segs.size(); ++i) {
        if (segs[i].empty()) continue;
        const bool  is_last = (i + 1 == segs.size());
        std::string c       = clean_write_segment(segs[i], is_last, all_dirs);
        if (!c.empty()) cleaned.push_back(std::move(c));
    }
    std::string out;
    for (size_t i = 0; i < cleaned.size(); ++i) {
        if (i) out += '/';
        out += cleaned[i];
    }
    return out;
}

std::string sanitize_filename(const std::string &input, const std::string &replacement) {
    static const std::regex k_illegal(R"([/?<>\\:*|"])");
    static const std::regex k_ctrl(R"([\x00-\x1f\x80-\x9f])");
    static const std::regex k_dots(R"(^\.+$)");
    static const std::regex k_winres(R"(^(con|prn|aux|nul|com[0-9]|lpt[0-9])(\..*)?$)", std::regex::icase);
    static const std::regex k_trailds(R"([. ]+$)");
    std::string s = std::regex_replace(std::string(input), k_illegal, replacement);
    s = std::regex_replace(s, k_ctrl, replacement);
    s = std::regex_replace(s, k_dots, replacement);
    s = std::regex_replace(s, k_winres, replacement);
    s = std::regex_replace(s, k_trailds, replacement);
    utf8_truncate_255(s);
    return s;
}

bool validate_path_spec_for_input_selection(const std::string &raw, std::string &err) {
    err.clear();
    if (is_http_url(raw)) return validate_url_spec_for_input_selection(raw, err);
    if (raw.find('\0') != std::string::npos) {
        err = "EFORBIDDEN: null byte in path";
        return false;
    }
    if (has_double_percent_encoding(raw)) {
        err = "EFORBIDDEN: double-encoded path segment";
        return false;
    }
    if (home_escape(raw)) {
        err = "EFORBIDDEN: home directory access denied";
        return false;
    }
    std::string normalized;
    decode_strip_slashform(raw, normalized);
    return !subpath_traversal_violation(normalized, err);
}

bool validate_url_spec_for_input_selection(const std::string &raw, std::string &err) {
    err.clear();
    if (raw.find('\0') != std::string::npos) {
        err = "EFORBIDDEN: null byte in URL";
        return false;
    }
    if (!is_http_url(raw)) {
        err = "url spec: not an http(s) URL (future schemes may be allowed here)";
        return false;
    }
    return true;
}

} // namespace media::path
