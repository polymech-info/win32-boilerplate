#include <string>
#include <chrono>
#include "app.h"
#include "core/graph_state.h"
#include "blocks/block.h"

#include "containers/root_container.h"
#include "crude_json.h"
#include <imgui.h>
#include <fstream>
#include "Logging.h"
#include "stats.h"

#if defined(_WIN32)
#endif
#define PRINT_STATS 1

// Helper to get string argument with default
std::string GetStringArg(const ArgsMap &args, const std::string &key, const std::string &defaultValue = "")
{
    auto it = args.find(key);
    if (it != args.end() && it->second.Type == ArgValue::Type::String)
        return it->second.String;
    return defaultValue;
}

// Helper to get bool argument with default
bool GetBoolArg(const ArgsMap &args, const std::string &key, bool defaultValue = false)
{
    auto it = args.find(key);
    if (it != args.end() && it->second.Type == ArgValue::Type::Bool)
        return it->second.Bool;
    return defaultValue;
}

int GetIntArg(const ArgsMap &args, const std::string &key, int defaultValue = 0)
{
    auto it = args.find(key);
    if (it == args.end())
        return defaultValue;

    if (it->second.Type == ArgValue::Type::Int)
        return static_cast<int>(it->second.Int);

    if (it->second.Type == ArgValue::Type::String && !it->second.String.empty())
    {
        try
        {
            return std::stoi(it->second.String);
        }
        catch (...)
        {
        }
    }

    return defaultValue;
}

// Headless execution mode - no GUI, just load and run the graph
int RunHeadless(const ArgsMap &args)
{
    std::string logLevelOption = GetStringArg(args, "log-level", "debug");
    bool logLevelValid = true;
    auto parsedLogLevel = ParseLogLevel(logLevelOption, &logLevelValid);

    std::string logFile = GetStringArg(args, "log", "blueprints.log");
    InitLogger("blueprints-headless", true, true, logFile.c_str());
    SetLoggerLevel(parsedLogLevel);
    if (!logLevelValid)
    {
        LOG_WARN("Unknown log level '{}', defaulting to 'debug'", logLevelOption);
    }

    struct LoggerScope
    {
        ~LoggerScope() { ShutdownLogger(); }
    } loggerScope;

#if defined(_WIN32)
    const MemoryUsageWin32 startMemory = CaptureMemoryUsageWin32();
    const CpuTimesWin32 startCpuTimes = CaptureCpuTimesWin32();
    const auto wallClockStart = std::chrono::steady_clock::now();
    LogMemoryUsageWin32("Before run", startMemory);
#endif

    struct RunTimer
    {
        std::chrono::steady_clock::time_point start{std::chrono::steady_clock::now()};
#if defined(_WIN32)
        MemoryUsageWin32 startMemory;
        CpuTimesWin32 startCpu;
        std::chrono::steady_clock::time_point wallStart;
        RunTimer(const MemoryUsageWin32 &memoryBaseline,
                 const CpuTimesWin32 &cpuBaseline,
                 std::chrono::steady_clock::time_point wallBaseline)
            : startMemory(memoryBaseline), startCpu(cpuBaseline), wallStart(wallBaseline)
        {
        }
#else
        RunTimer() = default;
#endif
        ~RunTimer()
        {
            auto elapsed = std::chrono::steady_clock::now() - start;
            double seconds = std::chrono::duration_cast<std::chrono::duration<double>>(elapsed).count();
            double deltaWorkingSetMb = 0.0;
            double deltaPrivateMb = 0.0;
            double cpuPercent = 0.0;
#if defined(_WIN32)
#if PRINT_STATS
            MemoryUsageWin32 endMemory = CaptureMemoryUsageWin32();
            LogMemoryUsageWin32("After run", endMemory);
            deltaWorkingSetMb = (static_cast<double>(endMemory.workingSet) - static_cast<double>(startMemory.workingSet)) / (1024.0 * 1024.0);
            deltaPrivateMb = (static_cast<double>(endMemory.privateBytes) - static_cast<double>(startMemory.privateBytes)) / (1024.0 * 1024.0);
            auto wallElapsed = std::chrono::steady_clock::now() - wallStart;
            double wallSeconds = std::chrono::duration_cast<std::chrono::duration<double>>(wallElapsed).count();
            CpuTimesWin32 endCpu = CaptureCpuTimesWin32();
            cpuPercent = ComputeCpuUsagePercentWin32(startCpu, endCpu, wallSeconds);
            LOG_INFO("Total execution time: {:.3f} s (ΔWorking Set: {:+.2f} MB, ΔPrivate: {:+.2f} MB, CPU Avg: {:.1f}%%)",
                     seconds, deltaWorkingSetMb, deltaPrivateMb, cpuPercent);
#endif
#endif
        }
    };

#if defined(_WIN32)
    RunTimer runTimer(startMemory, startCpuTimes, wallClockStart);
#else
    RunTimer runTimer;
#endif

    LOG_INFO("==============================================");
    LOG_INFO("HEADLESS MODE: Graph execution without GUI");
    LOG_INFO("==============================================");

    // Get graph file path (required)
    std::string graphFile = GetStringArg(args, "graph", "");
    if (graphFile.empty())
    {
        graphFile = GetStringArg(args, "file", "BlueprintsGraph.json");
    }

    if (graphFile.empty())
    {
        LOG_ERROR("ERROR: --graph or --file required in headless mode");
        LOG_ERROR("Usage: --headless --graph=<path> --run [--log=all|blocks|links|none]");
        return 1;
    }

    LOG_INFO("Graph file: {}", graphFile);
    // Check if we should execute (--run flag)
    bool shouldRun = GetBoolArg(args, "run", false);
    if (!shouldRun)
    {
        LOG_INFO("INFO: --run flag not specified, loading graph only (no execution)");
    }

    // Load JSON data
    std::ifstream file(graphFile);
    if (!file)
    {
        LOG_ERROR("ERROR: Failed to open graph file: {}", graphFile);
        return 1;
    }

    std::string jsonData((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    auto root = crude_json::value::parse(jsonData);
    if (root.is_discarded() || !root.is_object())
    {
        LOG_ERROR("ERROR: Failed to parse JSON or not an object");
        return 1;
    }
    // Count nodes and links for info
    int nodeCount = 0;
    int linkCount = 0;

    if (root.contains("app_nodes") && root["app_nodes"].is_array())
    {
        const auto &nodesArray = root["app_nodes"].get<crude_json::array>();
        nodeCount = static_cast<int>(nodesArray.size());
    }

    if (root.contains("app_links") && root["app_links"].is_array())
    {
        const auto &linksArray = root["app_links"].get<crude_json::array>();
        linkCount = static_cast<int>(linksArray.size());
    }

    LOG_INFO("Graph contains: {} nodes, {} links", nodeCount, linkCount);

    // If --run not specified, just show info and exit
    if (!shouldRun)
    {
        LOG_INFO("\nGraph info:\n  Nodes: {}\n  Links: {}\n\nUse --run flag to execute the graph", nodeCount, linkCount);
        return 0;
    }

    LOG_INFO("==============================================");
    LOG_INFO("EXECUTING GRAPH IN HEADLESS MODE");
    LOG_INFO("==============================================");

    // Create minimal ImGui context for state management (no window/rendering)
    ImGuiContext *context = ImGui::CreateContext();
    ImGui::SetCurrentContext(context);
    App *app = new App("Blueprints_Headless", args);

    // Initialize the app's graph state without creating window
    // OnStart() will load the graph from the file specified in args
    app->OnStart();

    RootContainer *rootContainer = app->GetActiveRootContainer();
    if (!rootContainer)
    {
        LOG_ERROR("ERROR: Failed to get root container");
        delete app;
        ImGui::DestroyContext(context);
        return 1;
    }
    std::vector<Node *> startBlocks;
    auto nodes = rootContainer->GetAllNodes();

    for (auto *node : nodes)
    {
        if (node && node->IsBlockBased() && node->BlockInstance)
        {
            // Check if this is a Start block (block type contains "Start")
            const char *typeName = node->BlockInstance->GetTypeName();
            if (typeName && strstr(typeName, "Start") != nullptr)
            {
                startBlocks.push_back(node);
            }
        }
    }
    if (startBlocks.empty())
    {
        LOG_WARN("WARNING: No Start blocks found in graph");
        delete app;
        ImGui::DestroyContext(context);
        return 0;
    }
    // Activate all Start blocks by setting their first output (flow) as active
    for (auto *startNode : startBlocks)
    {
        if (startNode->BlockInstance)
        {
            // Run the Start block to print its message
            startNode->BlockInstance->Run(*startNode, app);

            // Activate the first flow output (index 0)
            startNode->BlockInstance->ActivateOutput(0, true);
        }
    }

    LOG_INFO("\n==============================================");
    LOG_INFO("EXECUTING RUNTIME");
    LOG_INFO("==============================================");

#if PRINT_STATS
    const int maxRuntimeSteps = GetIntArg(args, "max-steps", 100);
    if (maxRuntimeSteps > 0)
    {
        LOG_INFO("Max runtime steps: {} (override with --max-steps)", maxRuntimeSteps);
    }
    else
    {
        LOG_INFO("Max runtime steps: unlimited (use --max-steps to cap)");
    }
#endif
#if PRINT_STATS
    // Execute the runtime to propagate execution through the graph
    // This will process all activated blocks and propagate through connections
    int stepsExecuted = 0;
    while (app->ExecuteRuntimeStep())
    {
        ++stepsExecuted;
        if (maxRuntimeSteps > 0 && stepsExecuted >= maxRuntimeSteps)
        {
            LOG_WARN("Reached max runtime steps ({}); stopping to prevent infinite loop", maxRuntimeSteps);
            break;
        }
    }
    double stepsPerSecond = stepsExecuted > 0 ? stepsExecuted / std::max(std::chrono::duration_cast<std::chrono::duration<double>>(std::chrono::steady_clock::now() - wallClockStart).count(), 1e-6) : 0.0;
    LOG_INFO("\n==============================================");
    LOG_INFO("EXECUTION COMPLETE");
    LOG_INFO("==============================================");
    LOG_INFO("Runtime iterations executed: {} (Steps/sec: {:.2f})", stepsExecuted, stepsPerSecond);
#endif

    delete app;
    ImGui::DestroyContext(context);

    return 0;
}

int Main(const ArgsMap &args)
{
    // Check for headless mode first to ensure logger is initialized correctly for the mode
    bool headless = GetBoolArg(args, "headless", false);
    if (headless)
    {
        return RunHeadless(args);
    }

    // --- GUI Mode ---
    if (!g_logger)
    {
        std::string logFile = GetStringArg(args, "log", "blueprints.log");
        InitLogger("blueprints", true, true, logFile.c_str());
    }

    LOG_TRACE("[CHECKPOINT] Main: Beginning");

    // GUI mode (existing code)
    App example("Blueprints", args);
    if (example.Create())
    {
        return example.Run();
    }
    return 0;
}
