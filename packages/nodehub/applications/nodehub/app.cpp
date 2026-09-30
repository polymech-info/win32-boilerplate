#define IMGUI_DEFINE_MATH_OPERATORS
#include "app.h"
#include "containers/root_container.h"
#include "Logging.h"
#include <imgui_node_editor.h>
#include <imgui_node_editor_internal.h>

#include <app-types.h>

namespace ed = ax::NodeEditor;

ed::EditorContext* m_Editor = nullptr;

App::App(const char* name) : Application(name){
    m_Context = new NH_Context();    
    initLogger();
    // m_Context->SetParameterManager(new NH_ParameterManager(m_Context));
    
}

App::App(const char* name, const ArgsMap& args) : Application(name, args){
    m_Context = new NH_Context();
    initLogger();
}

App::App() : Application("Blueprints"){
    m_Context = new NH_Context();
    initLogger();
}

NH_Context* App::GetContext()
{
    return m_Context;
}

void App::initLogger()
{
    // Determine desired log level from CLI args
    std::string logLevelArg = "debug";
    auto logLevelIt = m_Args.find("log-level");
    if (logLevelIt != m_Args.end() && logLevelIt->second.Type == ArgValue::Type::String && !logLevelIt->second.String.empty())
    {
        logLevelArg = logLevelIt->second.String;
    }

    bool logLevelValid = true;
    auto parsedLogLevel = ParseLogLevel(logLevelArg, &logLevelValid);

    // Initialize spdlog logger system
    InitLogger("blueprints", true, true, "blueprints.log");
    SetLoggerLevel(parsedLogLevel);

    if (parsedLogLevel != spdlog::level::off)
    {
        spdlog::log(parsedLogLevel, "Logger level set to {}", spdlog::level::to_string_view(parsedLogLevel));
    }
    if (!logLevelValid)
    {
        LOG_WARN("Unknown log level '{}', defaulting to 'debug'", logLevelArg);
    }
    }
    
void App::OnStart()
{
    // Get graph filename from CLI args (--graph), default to BlueprintsGraph.json
    // Supports both relative and absolute paths
    auto it = m_Args.find("graph");
    if (it != m_Args.end() && it->second.Type == ArgValue::Type::String)
    {
        m_GraphFilename = it->second.String;
        LOG_INFO("Using graph file: {}", m_GraphFilename);
    }
    else
    {
        LOG_INFO("Using default graph file: {}", m_GraphFilename);
    }

    

    // Create default root container from graph filename
    AddRootContainer(m_GraphFilename);
    
    LOG_TRACE("[CHECKPOINT] OnStart: Root container created, active={:p}",
              static_cast<const void*>(GetActiveRootContainer()));
    
    ed::Config config;

    // Use custom settings callbacks instead of SettingsFile
    // Graph data (nodes, links, positions, control points) → specified file or BlueprintsGraph.json (handled in SaveGraph/LoadGraph)
    // View state only (scroll, zoom, selection) → Blueprints.json (handled via callbacks)
    config.SettingsFile = "Blueprints.json";
    config.UserPointer = this;
    
    config.LoadSettings = [](char* data, void* userPointer) -> size_t
    {
        auto self = static_cast<App*>(userPointer);
        return self->LoadViewSettings(data);
    };
    
    config.SaveSettings = [](const char* data, size_t size, ed::SaveReasonFlags reason, void* userPointer) -> bool
    {
        auto self = static_cast<App*>(userPointer);
        return self->SaveViewSettings(data, size);
    };
    
    // Disable per-node settings - we handle everything in BlueprintsGraph.json
    config.SaveNodeSettings = nullptr;
    config.LoadNodeSettings = nullptr;
    
    // Provide callbacks to access container nodes/links for BuildControl
    config.GetContainerNodeIds = [](void* userPointer, ed::NodeId* nodeIdsOut, int maxNodes) -> int
    {
        auto self = static_cast<App*>(userPointer);
        auto* container = self->GetActiveRootContainer();
        if (!container)
            return 0;
        
        // Get all container nodes
        auto nodes = container->GetNodes(self);
        
        if (nodeIdsOut == nullptr)
            return static_cast<int>(nodes.size());  // Return count only
        
        // Fill node IDs array
        int count = 0;
        for (auto* node : nodes)
        {
            if (node && count < maxNodes)
            {
                nodeIdsOut[count] = node->ID;
                count++;
            }
        }
        return count;
    };
    
    config.GetContainerLinkIds = [](void* userPointer, ed::LinkId* linkIdsOut, int maxLinks) -> int
    {
        auto self = static_cast<App*>(userPointer);
        auto* container = self->GetActiveRootContainer();
        if (!container)
            return 0;
        
        // Get all container links
        auto links = container->GetLinks(self);
        
        if (linkIdsOut == nullptr)
            return static_cast<int>(links.size());  // Return count only
        
        // Fill link IDs array
        int count = 0;
        for (auto* link : links)
        {
            if (link && count < maxLinks)
            {
                linkIdsOut[count] = link->ID;
                count++;
            }
        }
        return count;
    };
    
    // Provide callback to mark links as user-manipulated when user edits waypoints
    config.MarkLinkUserManipulated = [](ed::LinkId linkId, void* userPointer) -> void
    {
        auto self = static_cast<App*>(userPointer);
        self->MarkLinkUserManipulated(linkId);
    };
    
    // Provide callback when block display mode changes (triggers link auto-adjustment)
    config.OnBlockDisplayModeChanged = [](ed::NodeId nodeId, void* userPointer) -> void
    {
        auto self = static_cast<App*>(userPointer);
        // When display mode changes, the node size changes immediately
        // Call AutoAdjustLinkWaypoints immediately - it will detect the size change
        // by comparing current size with m_LastNodeSizes (which has the old size from last frame)
        self->AutoAdjustLinkWaypoints();
        // Update the last known size for this node after adjustment
        self->UpdateLastNodeSize(nodeId);
        LOG_TRACE("[CHECKPOINT] OnBlockDisplayModeChanged: AutoAdjustLinkWaypoints called");
    };

    LOG_TRACE("[CHECKPOINT] OnStart: About to create editor");
    
    m_Editor = ed::CreateEditor(&config);
    ed::SetCurrentEditor(m_Editor);
    // Load custom node styles from JSON
    if (m_StyleManager.LoadFromFile("styles.json"))
    {
        LOG_TRACE("[CHECKPOINT] OnStart: Custom styles loaded from styles.json");
    }
    else
    {
        
    }
    m_StyleManager.ApplyToEditorStyle(m_Editor);
    
    // Register runtime callback for block execution
    ed::Detail::EditorContext::RegisterRuntimeCallback(this, [](void* app) {
        static_cast<App*>(app)->ExecuteRuntimeStep();
    });
    
    LOG_TRACE("[CHECKPOINT] OnStart: Runtime callback registered");
    
    // Set global style - no arrows on any pins
    auto& style = ed::GetStyle();
    style.PinArrowSize = 0.0f;
    style.PinArrowWidth = 0.0f;

    LOG_TRACE("[CHECKPOINT] OnStart: About to load graph");
    
    // Load saved graph (nodes and links from BlueprintsGraph.json)
    auto* activeContainer = GetActiveRootContainer();
    if (activeContainer)
    {
        LoadGraph(m_GraphFilename, activeContainer);
    }
    
    LOG_TRACE("[CHECKPOINT] OnStart: Graph loaded, about to build nodes");
    
    BuildNodes();
    
    LOG_TRACE("[CHECKPOINT] OnStart: Nodes built, about to initialize context");

    m_Context->init(this);

    LOG_TRACE("[CHECKPOINT] OnStart: Context initialized, about to load textures");
    
    m_HeaderBackground = LoadTexture("data/BlueprintBackground.png");
    m_SaveIcon         = LoadTexture("data/ic_save_white_24dp.png");
    m_RestoreIcon      = LoadTexture("data/ic_restore_white_24dp.png");

    LOG_TRACE("[CHECKPOINT] OnStart: Textures loaded, about to navigate to content");
    
    // Only navigate to content if we don't have saved view settings
    // (m_NeedsInitialZoom is set to false in LoadViewSettings if Blueprints.json exists)
    if (m_NeedsInitialZoom)
    {
        LOG_TRACE("[CHECKPOINT] OnStart: No saved view state, navigating to content");
        ed::NavigateToContent(0.0f);
    }
    else
    {
        LOG_TRACE("[CHECKPOINT] OnStart: Saved view state will be restored, skipping initial zoom");
    }

    LOG_TRACE("[CHECKPOINT] OnStart: Complete");
}

void App::OnStop()
{
    // Save graph before shutdown
    auto* activeContainer = GetActiveRootContainer();
    if (activeContainer)
    {
        SaveGraph(m_GraphFilename, activeContainer);
    }
    
    auto releaseTexture = [this](ImTextureID& id)
    {
        if (id)
        {
            DestroyTexture(id);
            id = nullptr;
        }
    };

    releaseTexture(m_RestoreIcon);
    releaseTexture(m_SaveIcon);
    releaseTexture(m_HeaderBackground);

    if (m_Editor)
    {
        ed::DestroyEditor(m_Editor);
        m_Editor = nullptr;
    }
    
    // Shutdown spdlog logger
    ShutdownLogger();
}

// UUID Generation Methods (32-bit)
uint32_t App::GenerateRandomUuid()
{
    return m_UuidGenerator.GenerateRandom();
}

uint32_t App::GenerateSequentialUuid()
{
    return m_UuidGenerator.GenerateSequential();
}

std::string App::UuidToHexString(uint32_t uuid)
{
    return UuidGenerator::ToHexString(uuid);
}

uint32_t App::HexStringToUuid(const std::string& hexString)
{
    return UuidGenerator::FromHexString(hexString);
}

// UUID Generation Methods (64-bit using dual 32-bit words)
Uuid64 App::GenerateRandomUuid64()
{
    return m_UuidGenerator.GenerateRandom64();
}

Uuid64 App::GenerateSequentialUuid64()
{
    return m_UuidGenerator.GenerateSequential64();
}

std::string App::UuidToHexString64(const Uuid64& uuid)
{
    return UuidGenerator::ToHexString64(uuid);
}

Uuid64 App::HexStringToUuid64(const std::string& hexString)
{
    return UuidGenerator::FromHexString64(hexString);
}

// Standard UUID Format Conversion Methods
std::string App::UuidToStandardString(const Uuid64& uuid)
{
    return uuid.ToStandardUuidString();
}

Uuid64 App::StandardStringToUuid(const std::string& uuidStr, bool takeLast64)
{
    return Uuid64::FromStandardUuidString(uuidStr, takeLast64);
}

