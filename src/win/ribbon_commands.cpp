#include "win/ribbon_commands.hpp"

#if defined(_WIN32)

#include "constants.hpp"
#include "Resource.h"

#include <algorithm>
#include <cctype>

namespace pm::cli {
namespace {

std::string lower_compact(std::string s)
{
    s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char c) {
        return c == '_' || c == '-' || c == ' ';
    }), s.end());
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

struct LookupRow {
    const char* name;
    std::uint32_t id;
};

constexpr LookupRow kLookup[] = {
    {"resize", IDC_CMD_RESIZE},
    {"compress", IDC_CMD_COMPRESS},
    {"meta", IDC_CMD_META},
    {"transform", IDC_CMD_TRANSFORM},
    {"find", IDC_CMD_FIND},
    {"duplicates", IDC_CMD_DUPLICATES},
    {"chat", IDC_CMD_CHAT},
    {"run", IDC_CMD_RUN},
    {"pause", IDC_CMD_PAUSE},
    {"resume", IDC_CMD_RESUME},
    {"cancel", IDC_CMD_CANCEL},
    {"savesession", IDC_CMD_SAVE_SESSION},
    {"loadsession", IDC_CMD_LOAD_SESSION},
    {"filetree", IDC_CMD_VIEW_FILETREE},
    {"explorer", IDC_CMD_VIEW_FILETREE},
    {"terminal", IDC_CMD_VIEW_CONSOLE},
    {"console", IDC_CMD_VIEW_CONSOLE},
    {"log", IDC_CMD_VIEW_LOG},
    {"resetlayout", IDC_CMD_RESET_LAYOUT},
    {"settings", IDC_CMD_FILE_SETTINGS},
    {"appsettings", IDC_CMD_APP_SETTINGS},
    {"theme", IDC_CMD_TOGGLE_THEME},
    {"home", IDC_CMD_VIEW_HOME},
};

std::uint32_t lookup_id(const std::string& compact)
{
    for (const auto& row : kLookup) {
        if (compact == row.name)
            return row.id;
        std::string prefixed = "idccmd";
        prefixed += row.name;
        if (compact == prefixed)
            return row.id;
    }
    return 0;
}

} // namespace

std::vector<RegisteredRibbonCommandInfo> registered_ribbon_commands()
{
    std::vector<RegisteredRibbonCommandInfo> out;
    auto push = [&](const char* id, const char* label, bool available) {
        out.push_back({id, label, available});
    };

#if defined(FEATURE_HOME_PAGE) && FEATURE_HOME_PAGE && defined(FEATURE_BROWSER) && FEATURE_BROWSER
    push("home", "Home", true);
#else
    push("home", "Home", false);
#endif
    push("filetree", "Explorer", true);
#if defined(FEATURE_CONSOLE) && FEATURE_CONSOLE
    push("terminal", "Terminal", true);
#else
    push("terminal", "Terminal", false);
#endif
#if FEATURE_COMMAND_LOG_VIEW
#  if defined(FEATURE_CONSOLE) && FEATURE_CONSOLE
    push("log", "Log", false);
#  else
    push("log", "Log", true);
#  endif
#else
    push("log", "Log", false);
#endif
#if defined(FEATURE_CHAT_WEB) && FEATURE_CHAT_WEB
    push("chat", "Chat", true);
#else
    push("chat", "Chat", false);
#endif
    push("theme", "Theme", true);
#if FEATURE_COMMAND_RESIZE
    push("resize", "Resize", true);
#else
    push("resize", "Resize", false);
#endif
#if FEATURE_COMMAND_COMPRESS
    push("compress", "Compress", true);
#else
    push("compress", "Compress", false);
#endif
#if FEATURE_COMMAND_META
    push("meta", "Meta", true);
#else
    push("meta", "Meta", false);
#endif
#if FEATURE_COMMAND_TRANSFORM
    push("transform", "Transform", true);
#else
    push("transform", "Transform", false);
#endif
#if FEATURE_COMMAND_FIND
    push("find", "Find", true);
#else
    push("find", "Find", false);
#endif
#if FEATURE_COMMAND_DUPLICATES
    push("duplicates", "Duplicates", true);
#else
    push("duplicates", "Duplicates", false);
#endif
#if FEATURE_COMMAND_UI_COMMAND_CONTROL
    push("run", "Run", true);
    push("pause", "Pause", true);
    push("resume", "Resume", true);
    push("cancel", "Cancel", true);
#else
    push("run", "Run", false);
    push("pause", "Pause", false);
    push("resume", "Resume", false);
    push("cancel", "Cancel", false);
#endif
#if FEATURE_COMMAND_SESSION_PERSISTENCE
    push("saveSession", "Save session", true);
    push("loadSession", "Load session", true);
#else
    push("saveSession", "Save session", false);
    push("loadSession", "Load session", false);
#endif
    push("resetLayout", "Reset layout", true);
#if defined(FEATURE_BROWSER) && FEATURE_BROWSER
    push("settings", "Settings", true);
#else
    push("settings", "Settings", false);
#endif
    push("appSettings", "App settings", true);
    return out;
}

std::uint32_t ribbon_command_id_from_name(const std::string& name)
{
    if (name.empty())
        return 0;
    return lookup_id(lower_compact(name));
}

} // namespace pm::cli

#endif // _WIN32
