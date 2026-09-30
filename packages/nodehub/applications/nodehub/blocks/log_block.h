#pragma once
#include "block.h"
#include "../Logging.h"
#include <vector>
#include <utility>
#include <spdlog/common.h>

// Log block - logs input parameters to a JSON file
// First parameter is the log file path (fixed)
// Additional parameters can be added dynamically
// Appends entries as JSON array with timestamps
class LogBlock : public ParameterizedBlock
{
public:
    LogBlock(int id) : ParameterizedBlock(id, "Log") 
    {
        SetTypeName("Log");
        m_Type = NodeType::Blueprint;
        m_Color = ImColor(255, 200, 100);
        SetFlags(static_cast<NH_BEHAVIOR_FLAGS>(
            NHBEHAVIOR_SCRIPT |
            NHBEHAVIOR_VARIABLEINPUTS));
    }
    
    void Build(Node& node, App* app) override;
    int Run(Node& node, App* app) override;
    NH_CSTRING GetBlockType() const override { return "Log"; }
    
    // State save/load to persist variable parameters
    void SaveState(Node& node, crude_json::value& nodeData, const Container* container, App* app) override;
    void LoadState(Node& node, const crude_json::value& nodeData, Container* container, App* app) override;
    
    // Context menu - allow adding/removing variable parameters
    void OnMenu(Node& node, App* app) override;
    
    // Edit dialog UI - custom settings for Log block
    void RenderEditUI(Node& node, App* app) override;
    
    // Pin management (like GroupBlock)
    void RebuildPins(Node& node, App* app);
    void AddVariableParamDef(const std::string& name, PinType type, int pinId = -1);
    void RemoveVariableParamDef(size_t index);
    
private:
    struct VariableParamDef
    {
        std::string name;
        PinType type;
        int pinId;  // Stable pin ID (preserved across rebuilds)
        
        VariableParamDef(const std::string& n, PinType t, int id = -1)
            : name(n), type(t), pinId(id) {}
    };
    
    std::vector<VariableParamDef> m_VariableParams;
    
    // Log settings
    template <typename... Args>
    void LogWithConfiguredLevel(NH_CSTRING fmt, Args&&... args) const
    {
        if (m_LogLevel == spdlog::level::off || !g_logger)
            return;
        g_logger->log(m_LogLevel, fmt, std::forward<Args>(args)...);
    }

    bool m_OutputToConsole = false;  // Also output to console
    bool m_AppendToFile = true;      // Append to file (default) vs overwrite
    spdlog::level::level_enum m_LogLevel = spdlog::level::info;
};

