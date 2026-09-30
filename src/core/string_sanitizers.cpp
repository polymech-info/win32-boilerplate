#include "string_sanitizers.hpp"

#include <cctype>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

namespace media::str_san {

namespace {

void trim_in_place(std::string &s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) {
        s.clear();
        return;
    }
    const auto e = s.find_last_not_of(" \t\r\n");
    s = s.substr(b, e - b + 1);
}

} // namespace

std::string clean_path(std::string_view raw) {
    std::string s(raw);
    for (char &c : s) {
        if (c == '\\')
            c = '/';
    }
    for (size_t i = 0; i < s.size();) {
        if (s[i] == '/') {
            size_t j = i + 1;
            while (j < s.size() && s[j] == '/')
                ++j;
            if (j > i + 1) {
                s.erase(i + 1, j - (i + 1));
            } else
                ++i;
        } else
            ++i;
    }
    while (!s.empty() && s[0] == '/')
        s.erase(0, 1);
    while (!s.empty() && s.back() == '/')
        s.pop_back();
    return s;
}

std::vector<std::string> path_segments(std::string_view raw) {
    const std::string c = clean_path(raw);
    if (c.empty())
        return {};
    std::vector<std::string> out;
    size_t a = 0;
    for (size_t i = 0; i <= c.size(); ++i) {
        if (i == c.size() || c[i] == '/') {
            if (a < i)
                out.emplace_back(c, a, i - a);
            a = i + 1;
        }
    }
    return out;
}

std::string normalise_path(std::string_view raw) {
    const std::string c = clean_path(raw);
    if (c.empty())
        return "/";
    return "/" + c;
}

std::string clean_uuid(std::string_view raw, std::string &err) {
    err.clear();
    std::string id(raw);
    trim_in_place(id);
    for (char &c : id) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
    static const std::regex k_uuid(
        R"(^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$)",
        std::regex::icase);
    if (!std::regex_match(id, k_uuid)) {
        err = "Invalid UUID: '";
        err += id;
        err += "'";
        return {};
    }
    return id;
}

bool is_uuid(std::string_view raw) {
    std::string t(raw);
    trim_in_place(t);
    for (char &c : t) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
    static const std::regex k_uuid(
        R"(^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$)",
        std::regex::icase);
    return std::regex_match(t, k_uuid);
}

std::string clean_id(std::string_view raw, std::string &err) {
    err.clear();
    std::string t(raw);
    trim_in_place(t);
    for (char &c : t) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
    if (t == k_anonymous_user_id) return std::string(k_anonymous_user_id);
    if (t == k_authenticated_user_id) return std::string(k_authenticated_user_id);
    return clean_uuid(t, err);
}

std::string clean_group_name(std::string_view raw, std::string &err) {
    err.clear();
    std::string t(raw);
    trim_in_place(t);
    for (char &c : t) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
    if (t.empty()) {
        err = "Group name cannot be empty";
        return {};
    }
    if (t.find(':') != std::string::npos) {
        err = "Group name cannot contain ':': " + t;
        return {};
    }
    return t;
}

std::string clean_permission(std::string_view raw, std::string &err) {
    err.clear();
    std::string t(raw);
    trim_in_place(t);
    for (char &c : t) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
    if (t.empty()) {
        err = "Permission name cannot be empty";
        return {};
    }
    return t;
}

std::vector<std::string> clean_permissions(const std::vector<std::string> &raw, std::string &err) {
    err.clear();
    std::vector<std::string> out;
    out.reserve(raw.size());
    for (const auto &s : raw) {
        std::string t = clean_permission(s, err);
        if (!err.empty()) return {};
        out.push_back(std::move(t));
    }
    return out;
}

std::string assert_non_empty_str(std::string_view value, std::string_view label, std::string &err) {
    err.clear();
    for (const char c : value) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c != 0) return std::string(value);
    }
    err = std::string(label);
    err += " cannot be empty";
    return {};
}

static bool is_blank(std::string_view s) {
    for (const char c : s) {
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') return false;
    }
    return true;
}

bool assert_non_empty_list(const std::vector<std::string> &values, std::string_view label, std::string &err) {
    err.clear();
    for (const auto &s : values) {
        if (s.empty() || is_blank(s)) {
            err = std::string(label);
            err += " cannot be empty";
            return false;
        }
    }
    return true;
}

} // namespace media::str_san
