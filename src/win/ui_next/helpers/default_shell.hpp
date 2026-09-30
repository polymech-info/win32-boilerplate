#pragma once
//
// Safe wrappers around ShellExecuteW.
//
// Every raw ::ShellExecuteW call in the codebase should go through one of
// these helpers so that:
//   • empty / null-byte paths are rejected before reaching the OS
//   • the return code is checked and logged on failure
//   • LLM prompt-injection guards are applied uniformly:
//       - open_path / reveal_in_explorer: llm_fs_guard_deny_reason (sensitive
//         dirs, dot files, hidden files) + executable-extension blocklist
//         (.exe, .bat, .lnk, .ps1, .cmd, .vbs, .hta, .scr, …)
//       - open_url: http/https scheme only; bracket-aware authority parser;
//         blocks private/loopback hosts (127.x, 10.x, 192.168.x,
//         172.16–31.x, 169.254.x, ::1, localhost), IPv4-mapped IPv6 private
//         hosts, percent-encoded/obfuscated numeric hosts, credential-embedded
//         URLs (user:pass@host), URLs > 2 KB
//       - explore_folder: llm_fs_guard_deny_reason + null-byte check
//
// Limitation: magic-byte / MIME-type inspection for misnamed files
// (e.g. evil.jpg that is actually a .bat) is not done at this layer.
//
#include <Windows.h>
#include <string_view>

namespace pmui::shell {

/// Open @p path with the OS-registered default handler ("open" verb).
/// Guards: executable-extension blocklist + llm_fs_guard_deny_reason.
/// Returns true if ShellExecuteW reported success (code > 32).
bool open_path(HWND owner, std::wstring_view path);

/// Open @p url in the default browser.
/// Guards: http/https scheme only; bracket-aware authority parser;
/// private/loopback host block (127.x, 10.x, 192.168.x, 172.16-31.x,
/// 169.254.x, ::1, localhost); IPv4-mapped IPv6 private hosts;
/// percent-encoded/obfuscated numeric host block; credential injection block
/// (user:pass@); null byte; max 2 KB length.
/// Returns true if ShellExecuteW reported success.
bool open_url(HWND owner, std::wstring_view url);

/// Open @p folder in Windows Explorer using the "explore" verb.
/// Guards: llm_fs_guard_deny_reason + null-byte check.
/// Returns true if ShellExecuteW reported success.
bool explore_folder(HWND owner, std::wstring_view folder);

/// Reveal (select) @p path in a Windows Explorer window via
/// `explorer.exe /select,"<path>"`.
/// Guards: llm_fs_guard_deny_reason + null-byte check.
void reveal_in_explorer(HWND owner, std::wstring_view path);

} // namespace pmui::shell
