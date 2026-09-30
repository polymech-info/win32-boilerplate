#pragma once
// Chat context attachments: images (size cap), recognised text extensions (smaller cap + NUL sniff),
// and existing directory paths (folder path in context, not enumerated children).
// Used by Explorer selection, RefreshChatContext, and chat-web drag/drop (parity with file_extensions.hpp).

#include "file_extensions.hpp"

#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace pmui {

inline constexpr std::uint64_t k_chat_context_image_max_bytes = 3ull * 1024 * 1024;
inline constexpr std::uint64_t k_chat_context_text_max_bytes  = 512ull * 1024;
inline constexpr std::size_t   k_chat_context_text_sniff_max  = 8192;

/** Regular file only: image (≤3 MiB) or text extension (≤512 KiB, no NUL in first sniff bytes). */
inline bool chat_context_file_allowed(const std::wstring& path_w)
{
    if (path_w.empty()) return false;
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path fp(path_w);
    if (!fs::is_regular_file(fp, ec) || ec) return false;
    const std::uintmax_t sz = fs::file_size(fp, ec);
    if (ec) return false;

    std::wstring ext = fp.extension().wstring();
    for (auto& c : ext) c = static_cast<wchar_t>(std::towlower(static_cast<std::wint_t>(c)));

    if (is_image_ext(ext))
        return sz <= static_cast<std::uintmax_t>(k_chat_context_image_max_bytes);

    if (is_text_ext(ext)) {
        if (sz > static_cast<std::uintmax_t>(k_chat_context_text_max_bytes)) return false;
        const std::uintmax_t sniff_u = static_cast<std::uintmax_t>(k_chat_context_text_sniff_max);
        const std::uintmax_t nread     = std::min(sz, sniff_u);
        const auto           to_read = static_cast<std::size_t>(nread);
        if (to_read == 0) return true;
        std::vector<std::uint8_t> buf(to_read);
        std::ifstream in(fp, std::ios::binary);
        if (!in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(to_read))) return false;
        const std::size_t got = static_cast<std::size_t>(in.gcount());
        return !looks_like_binary(buf.data(), got);
    }
    return false;
}

/** Existing directory on disk (path only; no recursive file list). */
inline bool chat_context_directory_allowed(const std::wstring& path_w)
{
    if (path_w.empty()) return false;
    std::error_code ec;
    return std::filesystem::is_directory(std::filesystem::path(path_w), ec) && !ec;
}

/** Allowed chat context path: regular file per `chat_context_file_allowed`, or an existing folder path. */
inline bool chat_context_path_allowed(const std::wstring& path_w)
{
    return chat_context_file_allowed(path_w) || chat_context_directory_allowed(path_w);
}

} // namespace pmui
