#include "win/app_commands.hpp"
#include "constants.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <system_error>

namespace media::win::app_cmd {
namespace {

// Strip separators ('_', '-', ' ') and lowercase, so "take_screenshot",
// "Take-Screenshot" and "TAKE SCREENSHOT" all match "takescreenshot".
std::string normalise(std::string s)
{
    s.erase(std::remove_if(s.begin(), s.end(),
            [](unsigned char c) { return c == '_' || c == '-' || c == ' '; }),
            s.end());
    for (auto& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

Command parse(const std::string& nm)
{
    const std::string n = normalise(nm);
    if (n == "takescreenshot" || n == "screenshot")
        return Command::TakeScreenshot;
    if (n == "pausebatch" || n == "pause")
        return Command::PauseBatch;
    if (n == "resumebatch" || n == "resume")
        return Command::ResumeBatch;
    if (n == "cancelbatch" || n == "cancel")
        return Command::CancelBatch;
    if (n == "chat")
        return Command::OpenChat;
    if (n == "browse" || n == "reveal" || n == "syncfiletree")
        return Command::BrowseToPaths;
    if (n == "recordstart" || n == "startrecord" || n == "startsessionrecord")
        return Command::StartSessionRecord;
    if (n == "recordstop" || n == "stoprecord" || n == "stopsessionrecord")
        return Command::StopSessionRecord;
    if (n == "videorecordstart" || n == "videostart" || n == "startsessionvideo" || n == "sessionvideostart")
        return Command::StartSessionVideoRecord;
    if (n == "videorecordstop" || n == "videostop" || n == "stopsessionvideo" || n == "sessionvideostop")
        return Command::StopSessionVideoRecord;
    if (n == "videorecordpause" || n == "videopause" || n == "pausevideorecord" || n == "togglevideopause")
        return Command::ToggleSessionVideoPause;
    if (n == "replay" || n == "sessionreplay" || n == "applysession")
        return Command::SessionReplay;
    return Command::Unknown;
}

const char* name(Command cmd)
{
    switch (cmd) {
        case Command::TakeScreenshot: return "takescreenshot";
        case Command::PauseBatch:     return "pausebatch";
        case Command::ResumeBatch:    return "resumebatch";
        case Command::CancelBatch:    return "cancelbatch";
        case Command::OpenChat:             return "chat";
        case Command::BrowseToPaths:        return "browse";
        case Command::StartSessionRecord:   return "recordstart";
        case Command::StopSessionRecord:   return "recordstop";
        case Command::StartSessionVideoRecord: return "videorecordstart";
        case Command::StopSessionVideoRecord:  return "videorecordstop";
        case Command::ToggleSessionVideoPause: return "videorecordpause";
        case Command::SessionReplay:       return "replay";
        case Command::Unknown:             return "unknown";
    }
    return "unknown";
}

std::filesystem::path default_screenshots_dir()
{
    namespace fs = std::filesystem;
    // TODO(app-commands): when AppData layout is finalised, move this under
    // `%APPDATA%/<k_config_subpath>/screenshots` so screenshots survive
    // running from random working directories.
    fs::path dir = fs::current_path() / L"screenshots";
    std::error_code ec;
    fs::create_directories(dir, ec);   // best-effort; capture_window_to_png
                                       // surfaces "save failed" otherwise.
    return dir;
}

std::wstring make_screenshot_filename()
{
    using namespace std::chrono;

    const auto now = system_clock::now();
    const auto t   = system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_MSC_VER)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif

    std::wostringstream os;
    os << pm::brand::k_screenshot_filename_prefix_w
       << std::put_time(&tm, L"%Y%m%d-%H%M%S")
       << L".png";
    return os.str();
}

} // namespace media::win::app_cmd
