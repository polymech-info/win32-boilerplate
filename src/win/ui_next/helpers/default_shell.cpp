#include "stdafx.h"
#include "helpers/default_shell.hpp"
#include "helpers/text_conv.hpp"
#include "logger/logger.h"
#include "llm/llm_fs_guard.hpp"
#include <shellapi.h>
#include <algorithm>
#include <cctype>
#include <string_view>

namespace pmui::shell {

// ── internal helpers ──────────────────────────────────────────────────────────

static bool shell_exec(HWND owner,
                       LPCWSTR verb,
                       LPCWSTR file,
                       LPCWSTR params,
                       const char* tag)
{
    const HINSTANCE hi = ::ShellExecuteW(owner, verb, file, params, nullptr, SW_SHOWNORMAL);
    const INT_PTR   code = reinterpret_cast<INT_PTR>(hi);
    if (code <= 32) {
        logger::warn(std::string("[shell] ") + tag
            + " failed code=" + std::to_string(code)
            + " file='" + wide_to_utf8(file ? std::wstring(file) : std::wstring()) + "'");
        return false;
    }
    return true;
}

// Extensions that ShellExecute "open" would run rather than display.
// An LLM prompt-injection could produce a path pointing at one of these;
// blocking them here means the wrapper is safe regardless of call site.
// Note: we cannot detect misnamed files (e.g. evil.jpg that is really a .bat),
// but that requires magic-byte inspection which is out of scope for this layer.
static bool open_ext_is_executable(std::wstring_view path)
{
    // Find the last dot after the last separator.
    const auto sep = path.find_last_of(L"/\\");
    const auto dot = path.find_last_of(L'.');
    if (dot == std::wstring_view::npos) return false;
    if (sep != std::wstring_view::npos && dot < sep) return false;

    std::wstring ext(path.substr(dot));
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](wchar_t c){ return static_cast<wchar_t>(::towlower(c)); });

    static constexpr std::wstring_view k_blocked[] = {
        // Windows executables & launchers
        L".exe", L".com", L".pif", L".scr", L".cpl", L".msc",
        L".msi", L".msp", L".msix", L".appx", L".application",
        // shortcuts & jump targets
        L".lnk", L".url", L".scf",
        // scripts (Windows)
        L".bat", L".cmd", L".ps1", L".psm1", L".psd1",
        L".vbs", L".vbe", L".wsf", L".wsh", L".hta",
        L".reg", L".inf",
        // scripts (cross-platform)
        L".sh", L".bash", L".zsh", L".fish", L".ksh", L".csh",
        L".py", L".pyw", L".pl", L".rb", L".lua", L".php",
        L".js", L".jse", L".mjs", L".cjs", L".ts",
        // libraries & bytecode that shellexec can launch indirectly
        L".dll", L".jar",
    };
    for (std::wstring_view b : k_blocked) {
        if (ext == b) return true;
    }
    return false;
}

// Guard for file-system paths coming from LLM-generated content.
// Returns a non-empty denial reason string if the path should be blocked.
static std::string path_guard_deny_reason(std::wstring_view path)
{
    if (path.empty()) return "empty path";

    // Null byte injection.
    if (path.find(L'\0') != std::wstring_view::npos) return "null byte in path";

    // Executable extension — ShellExecute "open" would run it.
    if (open_ext_is_executable(path))
        return "refusing to open executable or script extension";

    // Sensitive-path and hidden-file check via the LLM FS guard.
    const std::filesystem::path fsp(path);
    const std::string reason = media::llm::llm_fs_guard_deny_reason(fsp);
    if (!reason.empty()) return reason;

    return {};
}

// ── public API ────────────────────────────────────────────────────────────────

bool open_path(HWND owner, std::wstring_view path)
{
    if (path.empty()) {
        logger::warn("[shell] open_path: empty path, skipping");
        return false;
    }
    const std::string deny = path_guard_deny_reason(path);
    if (!deny.empty()) {
        logger::warn("[shell] open_path blocked: " + deny
            + " path='" + wide_to_utf8(std::wstring(path)) + "'");
        return false;
    }
    const std::wstring p(path);
    return shell_exec(owner, L"open", p.c_str(), nullptr, "open_path");
}

// ── URL guards ────────────────────────────────────────────────────────────────

struct UrlAuthority {
    std::string host;
    bool        bracketed_ipv6 = false;
};

static bool is_dec_digit(char c)
{
    return c >= '0' && c <= '9';
}

static bool is_hex_digit(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

// Extract the host from an http(s) URL authority.
// Returns false if the URL is malformed or uses syntax we do not allow.
static bool parse_url_authority(const std::string& url, UrlAuthority& out)
{
    // url is already confirmed to start with http:// or https://
    const size_t scheme_end = url.find("://");
    if (scheme_end == std::string::npos) return false;
    const size_t authority_start = scheme_end + 3;
    const size_t authority_end   = url.find_first_of("/?#", authority_start);
    const size_t end             = authority_end == std::string::npos ? url.size() : authority_end;
    if (authority_start >= end) return false;

    const std::string authority = url.substr(authority_start, end - authority_start);
    if (authority.find('@') != std::string::npos) return false;
    if (authority.find('%') != std::string::npos) return false; // no percent-encoded host obfuscation

    if (authority.front() == '[') {
        const size_t close = authority.find(']');
        if (close == std::string::npos || close == 1) return false;
        if (close + 1 < authority.size()) {
            if (authority[close + 1] != ':') return false;
            for (size_t i = close + 2; i < authority.size(); ++i) {
                if (!is_dec_digit(authority[i])) return false;
            }
        }
        out.host = authority.substr(1, close - 1);
        out.bracketed_ipv6 = true;
        return true;
    }

    const size_t colon = authority.find(':');
    if (colon != std::string::npos) {
        if (authority.find(':', colon + 1) != std::string::npos) return false; // IPv6 must be bracketed
        for (size_t i = colon + 1; i < authority.size(); ++i) {
            if (!is_dec_digit(authority[i])) return false;
        }
        out.host = authority.substr(0, colon);
    } else {
        out.host = authority;
    }
    if (out.host.empty()) return false;
    return true;
}

static bool parse_strict_ipv4(const std::string& host, int (&octets)[4])
{
    if (host.empty()) return false;
    int count = 0;
    int cur = 0;
    int digits = 0;
    for (char c : host) {
        if (c == '.') {
            if (digits == 0 || count >= 3) return false;
            octets[count++] = cur;
            cur = 0;
            digits = 0;
        } else if (c >= '0' && c <= '9') {
            if (++digits > 3) return false;
            cur = cur * 10 + (c - '0');
            if (cur > 255) return false;
        } else {
            return false;
        }
    }
    if (digits == 0) return false;
    octets[count++] = cur;
    return count == 4;
}

static bool looks_like_obfuscated_numeric_host(const std::string& host)
{
    if (host.empty() || !is_dec_digit(host.front())) return false;
    bool saw_hexish = false;
    for (char c : host) {
        if (is_hex_digit(c) || c == '.' || c == 'x') {
            saw_hexish = true;
            continue;
        }
        return false;
    }
    return saw_hexish;
}

static bool host_name_syntax_is_safe(const std::string& host)
{
    if (host.empty() || host.front() == '.' || host.back() == '.') return false;
    for (char c : host) {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.')
            continue;
        return false;
    }
    return true;
}

static bool ipv6_host_syntax_is_safe(const std::string& host)
{
    if (host.empty() || host.find(':') == std::string::npos) return false;
    for (char c : host) {
        if (is_hex_digit(c) || c == ':' || c == '.') continue;
        return false;
    }
    return true;
}

static bool ipv4_is_private(const int (&octets)[4])
{
    const int a = octets[0], b = octets[1];
    if (a == 127)                        return true; // 127.0.0.0/8 loopback
    if (a == 10)                         return true; // 10.0.0.0/8
    if (a == 192 && b == 168)            return true; // 192.168.0.0/16
    if (a == 172 && b >= 16 && b <= 31)  return true; // 172.16.0.0/12
    if (a == 169 && b == 254)            return true; // 169.254.0.0/16 link-local / AWS metadata
    if (a == 0)                          return true; // 0.x.x.x unspecified
    if (a == 100 && b >= 64 && b <= 127) return true; // 100.64.0.0/10 carrier-grade NAT
    return false;
}

static bool url_host_is_private_or_invalid(const UrlAuthority& auth, std::string& reason)
{
    const std::string& host = auth.host;
    if (host.empty()) {
        reason = "empty host";
        return true;
    }

    if (auth.bracketed_ipv6) {
        if (!ipv6_host_syntax_is_safe(host)) {
            reason = "invalid ipv6 host syntax";
            return true;
        }
        if (host == "::1" || host == "0:0:0:0:0:0:0:1") {
            reason = "ipv6 loopback";
            return true;
        }
        if (host.size() >= 4 && host.substr(0, 4) == "fe80") {
            reason = "ipv6 link-local";
            return true;
        }
        if (host.rfind("::ffff:", 0) == 0) {
            int octets[4] = {};
            const std::string tail = host.substr(7);
            if (!parse_strict_ipv4(tail, octets) || ipv4_is_private(octets)) {
                reason = "ipv4-mapped ipv6 private/invalid";
                return true;
            }
        }
        return false;
    }

    if (host == "localhost" || host == "localhost.localdomain") {
        reason = "localhost";
        return true;
    }

    int octets[4] = {};
    if (parse_strict_ipv4(host, octets)) {
        if (ipv4_is_private(octets)) {
            reason = "private ipv4";
            return true;
        }
        return false;
    }

    if (looks_like_obfuscated_numeric_host(host)) {
        reason = "obfuscated numeric host";
        return true;
    }

    if (!host_name_syntax_is_safe(host)) {
        reason = "invalid host syntax";
        return true;
    }

    return false;
}

bool open_url(HWND owner, std::wstring_view url)
{
    if (url.empty()) {
        logger::warn("[shell] open_url: empty url, skipping");
        return false;
    }

    const std::wstring u(url);
    const std::string  u8 = wide_to_utf8(u);

    // 1. Null byte.
    if (u8.find('\0') != std::string::npos) {
        logger::warn("[shell] open_url blocked: null byte in url");
        return false;
    }

    // 2. Reasonable length (browsers cap at ~2 KB; anything longer is suspicious).
    if (u8.size() > 2048) {
        logger::warn("[shell] open_url blocked: url exceeds 2048 chars");
        return false;
    }

    // 3. Scheme: only http / https.
    std::string u8_lc = u8;
    std::transform(u8_lc.begin(), u8_lc.end(), u8_lc.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    const bool is_http = u8_lc.substr(0, 7) == "http://" ||
                         u8_lc.substr(0, 8) == "https://";
    if (!is_http) {
        logger::warn("[shell] open_url blocked: non-http scheme '" + u8 + "'");
        return false;
    }

    // 4. Credential injection: presence of '@' before the path means userinfo
    //    is embedded — never legitimate for LLM-sourced URLs, and the @-part
    //    can contain arbitrary exfiltration data.
    const size_t scheme_end  = u8_lc.find("://") + 3;
    const size_t path_start  = u8_lc.find_first_of("/?#", scheme_end);
    const size_t at_pos      = u8_lc.find('@', scheme_end);
    if (at_pos != std::string::npos &&
        (path_start == std::string::npos || at_pos < path_start)) {
        logger::warn("[shell] open_url blocked: credentials/@ in url");
        return false;
    }

    // 5. Private / loopback hosts — SSRF into local services.
    UrlAuthority auth;
    std::string deny;
    if (!parse_url_authority(u8_lc, auth) || url_host_is_private_or_invalid(auth, deny)) {
        if (deny.empty()) deny = "malformed authority";
        logger::warn("[shell] open_url blocked: " + deny + " host='" + auth.host + "'");
        return false;
    }

    return shell_exec(owner, L"open", u.c_str(), nullptr, "open_url");
}

bool explore_folder(HWND owner, std::wstring_view folder)
{
    if (folder.empty()) {
        logger::warn("[shell] explore_folder: empty folder, skipping");
        return false;
    }
    // No executable-extension check needed for explore (it navigates, not opens).
    // Still block null bytes and sensitive paths.
    if (folder.find(L'\0') != std::wstring_view::npos) {
        logger::warn("[shell] explore_folder blocked: null byte in path");
        return false;
    }
    const std::string reason = media::llm::llm_fs_guard_deny_reason(
        std::filesystem::path(folder));
    if (!reason.empty()) {
        logger::warn("[shell] explore_folder blocked: " + reason);
        return false;
    }
    const std::wstring f(folder);
    return shell_exec(owner, L"explore", f.c_str(), nullptr, "explore_folder");
}

void reveal_in_explorer(HWND owner, std::wstring_view path)
{
    if (path.empty()) {
        logger::warn("[shell] reveal_in_explorer: empty path, skipping");
        return;
    }
    // reveal_in_explorer selects the item in an Explorer window — it does not
    // open/execute it, so only the sensitive-path and null-byte checks apply.
    if (path.find(L'\0') != std::wstring_view::npos) {
        logger::warn("[shell] reveal_in_explorer blocked: null byte in path");
        return;
    }
    const std::string reason = media::llm::llm_fs_guard_deny_reason(
        std::filesystem::path(path));
    if (!reason.empty()) {
        logger::warn("[shell] reveal_in_explorer blocked: " + reason);
        return;
    }
    std::wstring sel;
    sel.reserve(path.size() + 12);
    sel  = L"/select,\"";
    sel += path;
    sel += L"\"";
    shell_exec(owner, L"open", L"explorer.exe", sel.c_str(), "reveal_in_explorer");
}

} // namespace pmui::shell
