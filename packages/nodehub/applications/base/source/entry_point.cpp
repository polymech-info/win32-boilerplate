#define _CRT_SECURE_NO_WARNINGS

# include "base.h"
# include "platform.h"
# include <map>
# include <vector>
# include <iostream>
# include <cstdio>
# include <csignal>

#if defined(_WIN32)
#   include <io.h>
#   include <fcntl.h>
#   include <windows.h>
#endif

#define CLI11_DISABLE_EXTRA_VALIDATORS 1

# include "../../external/CLI11.hpp"

namespace {

class CommandLineParser
{
public:
    // Using unique_ptr as a C++14 alternative to optional.
    // A null return indicates the program should exit.
    std::unique_ptr<ArgsMap> Parse(std::vector<std::string> args)
    {
        CLI::App app{"NodeHub"};
        app.allow_extras();

        app.add_option("--file", "the source path")->capture_default_str();
        
        // Graph file path (absolute or relative)
        app.add_option("--graph", "Path to graph file (absolute or relative)")->capture_default_str();
        
        // CLI mode options
        app.add_flag("--headless", "Run without GUI (CLI mode)");
        app.add_option("--command", "Command to execute in headless mode (validate, export, execute, info)")
           ->capture_default_str();
        app.add_option("--output", "Output file path for export commands")->capture_default_str();
        app.add_option("--format", "Output format (json, xml, yaml, dot)")->capture_default_str();
        
        // Runtime execution options
        app.add_flag("--run", "Execute the graph runtime (used with --headless and --graph)");
        app.add_option("--log", "Path to log file (absolute or relative)")->capture_default_str();
        app.add_option("--log-level", "Minimum logging level (trace, debug, info, warn, error, critical, off)")->capture_default_str();

        // Manual pre-check for help flag because allow_extras() interferes with normal help handling
        for (const auto& arg : args) {
            if (arg == "--help" || arg == "-h") {
                PrintHelp(app);
                m_ExitCode = 0;
                return nullptr;
            }
        }

        try {
            app.parse(args);
        } catch (const CLI::ParseError& e) {
            HandleParseError(app, e);
            m_ExitCode = app.exit(e);
            return nullptr;
        }

        auto args_map = std::make_unique<ArgsMap>();

        for (const CLI::Option* option : app.get_options())
        {
            if(option->count() > 0 && !option->get_lnames().empty())
            {
                if (!option->results().empty())
                {
                    (*args_map)[option->get_lnames()[0]] = ParseValue(option->results()[0]);
                }
                else
                {
                    (*args_map)[option->get_lnames()[0]] = ArgValue(true);
                }
            }
        }

        ParseKeyValueArgs(app.remaining(), *args_map);

        m_ExitCode = 0;
        return args_map;
    }

    int GetExitCode() const { return m_ExitCode; }

protected:
    virtual void PrintHelp(CLI::App& app)
    {
        printf("%s\n", app.help().c_str());
        fflush(stdout);
    }

    virtual void HandleParseError(CLI::App& app, const CLI::ParseError& e)
    {
        // Default console implementation
        fprintf(stderr, "%s\n", app.help().c_str());
        fprintf(stderr, "%s: %s\n", app.get_name().c_str(), e.what());
        fflush(stderr);
    }

    int m_ExitCode = 0;

private:
    ArgValue ParseValue(const std::string& s)
    {
        if (s.empty())
            return ArgValue();

        if (s == "true" || s == "TRUE" || s == "True")
            return ArgValue(true);
        else if (s == "false" || s == "FALSE" || s == "False")
            return ArgValue(false);

        char* end = nullptr;
        const char* start = s.c_str();

        // Try parsing as an integer
        long long int_val = strtoll(start, &end, 10);
        if (end == start + s.length())
            return ArgValue(int_val);

        // Try parsing as a double
        end = nullptr;
        double double_val = strtod(start, &end);
        if (end == start + s.length())
            return ArgValue(double_val);

        return ArgValue(s);
    }

    void ParseKeyValueArgs(const std::vector<std::string>& args, ArgsMap& args_map)
    {
        for (size_t i = 0; i < args.size(); ++i) {
            std::string arg = args[i];
            if (arg.length() > 2 && arg.substr(0, 2) == "--") {
                std::string body = arg.substr(2);
                if (body.empty())
                    continue;
                auto equalPos = body.find('=');
                if (equalPos != std::string::npos) {
                    std::string key = body.substr(0, equalPos);
                    std::string value = body.substr(equalPos + 1);
                    if (!key.empty())
                        args_map[key] = ParseValue(value);
                    continue;
                }
                std::string key = body;
                if (i + 1 < args.size() && !args[i + 1].empty() && args[i + 1][0] != '-') {
                    args_map[key] = ParseValue(args[i + 1]);
                    ++i; // Skip the value
                } else {
                    args_map[key] = true;
                }
            }
        }
    }
};
}

Application* g_Application = nullptr;

namespace
{
    void SignalHandler(int signal)
    {
        if (g_Application)
            g_Application->Quit();
    }
}

# if defined(_WIN32) && !defined(_CONSOLE)

static void redirect_std_to_console() {
    // reopen stdout/stderr to the console
    FILE* fp;
    freopen_s(&fp, "CONOUT$", "w", stdout);
    freopen_s(&fp, "CONOUT$", "w", stderr);
    freopen_s(&fp, "CONIN$", "r", stdin);

    // make iostreams sync with C stdio
    std::ios::sync_with_stdio(true);
}

static bool ensure_console_attached() {
    // Try to attach if launched from a terminal
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        redirect_std_to_console();
        return true;
    }
    return false;
}

static void ensure_console_allocated() {
    if (AllocConsole()) {
        redirect_std_to_console();
    }
}

# if PLATFORM(WINDOWS)
#     define NOMINMAX
#     define WIN32_LEAN_AND_MEAN
#     include <windows.h>
#     include <stdlib.h> // __argc, argv
# endif

inline std::string WideToUtf8(const wchar_t* wstr) {
    if (!wstr) return {};
    // Ask for required size (includes NUL)
    const int n = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out;
    out.resize(n - 1); // we store without the trailing NUL

#if __cplusplus >= 201703L
    // C++17: string::data() is char*
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, out.data(), n, nullptr, nullptr);
#else
    // C++11/14: use &out[0] to get char*
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &out[0], n, nullptr, nullptr);
#endif

    // The call above wrote a NUL at the end because we passed length n.
    // Keep size at n-1 (already set via resize).
    return out;
}

// Build argc/argv as **mutable** C strings
struct ArgvUtf8 {
    int argc = 0;
    std::vector<std::unique_ptr<char[]>> storage; // owns the writable C strings
    std::vector<char*> argv;                      // pointers to the writable strings
};

static std::vector<std::string> GetUtf8Argv() {
    int wargc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    std::vector<std::string> args;
    if (wargv) {
        for (int i = 0; i < wargc; ++i)
            args.emplace_back(WideToUtf8(wargv[i]));
        LocalFree(wargv);
    }
    return args;
}
static std::vector<std::string> GetUtf8Args()
{
    int wargc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    std::vector<std::string> args;
    if (wargv) {
        for (int i = 1; i < wargc; ++i)  // skip executable path
            args.emplace_back(WideToUtf8(wargv[i]));
        LocalFree(wargv);
    }
    return args;
}

namespace {
class WindowsCommandLineParser : public CommandLineParser
{
protected:
    void HandleParseError(CLI::App& app, const CLI::ParseError& e) override
    {
        // Print to attached console first
        CommandLineParser::HandleParseError(app, e);

        // Then show a message box for the GUI user
        std::string msg = std::string("Argument parsing failed:\n\n") + e.what() +
            "\n\nTry '--help' for usage.";
        MessageBoxA(nullptr, msg.c_str(), "CLI Error", MB_ICONERROR | MB_OK);
    }
};
}

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd)
{
    signal(SIGINT, SignalHandler);

    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    if (!ensure_console_attached()) ensure_console_allocated();
    fflush(stdout);
    
    WindowsCommandLineParser parser;
    auto args = GetUtf8Args();
    auto args_map = parser.Parse(args);

    int exit_code;
    if (args_map)
    {
        exit_code = Main(*args_map);
    }
    else
    {
        exit_code = parser.GetExitCode();
    }

    return exit_code;
}
# else
int main(int argc, char** argv)
{
    signal(SIGINT, SignalHandler);

    CommandLineParser parser;
    
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i)
        args.push_back(argv[i]);
    
    auto args_map = parser.Parse(args);

    if (args_map)
        return Main(*args_map);
    else
        return parser.GetExitCode();
}
# endif
