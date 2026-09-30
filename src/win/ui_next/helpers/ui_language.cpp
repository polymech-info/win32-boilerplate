#include "helpers/ui_language.hpp"
#include "win/settings_store.hpp"

#include <Windows.h>

#include <string>
#include <string_view>
#include <vector>

namespace pmui {
namespace {

std::string normalize_code(std::string s)
{
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
    static const char* k[] = {"en", "es", "de", "it", "fr"};
    for (const char* ok : k) {
        if (s == ok)
            return s;
    }
    return "en";
}

LANGID lang_id_for_code(std::string_view code)
{
    std::string s(code);
    s = normalize_code(std::move(s));
    if (s == "es")
        return MAKELANGID(LANG_SPANISH, SUBLANG_SPANISH_MODERN);
    if (s == "de")
        return MAKELANGID(LANG_GERMAN, SUBLANG_GERMAN);
    if (s == "it")
        return MAKELANGID(LANG_ITALIAN, SUBLANG_ITALIAN);
    if (s == "fr")
        return MAKELANGID(LANG_FRENCH, SUBLANG_FRENCH);
    return MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
}

} // namespace

void apply_display_language(std::string_view code)
{
    const LANGID id = lang_id_for_code(code);
    // Vista+: picks STRINGTABLE / MENU blocks that match this language in the PE MUI resource set.
    const LANGID prev = SetThreadUILanguage(id);
    (void)prev;
}

void apply_display_language_from_settings()
{
    media::settings::AppearanceSettings a;
    std::string err;
    if (!media::settings::load_appearance(a, err))
        apply_display_language("en");
    else
        apply_display_language(a.display_language);
}

bool restart_current_process()
{
    wchar_t path[MAX_PATH]{};
    const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (!n || n >= MAX_PATH)
        return false;

    std::wstring cmd = GetCommandLineW();
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(path, buf.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
        return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

} // namespace pmui
