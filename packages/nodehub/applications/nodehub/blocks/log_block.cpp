#include <crude_json.h>

#include "log_block.h"
#include "../app.h"
#include "../Logging.h"
#include <fstream>
#include <ctime>

#ifdef _WIN32
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#endif

void LogBlock::Build(Node& node, App* app)
{
    node.Type = m_Type;
    node.Color = m_Color;
    
    // Clear existing pin collections before rebuilding
    m_InputParams.clear();
    m_OutputParams.clear();
    m_Inputs.clear();
    m_Outputs.clear();
    
    // Flow input
    AddInput(app, node, "Execute");
    
    // Fixed parameter: log file path (String) - always first parameter
    AddInputParameter(app, node, "FilePath", PinType::String);
    
    // Variable parameters (built from definitions, reusing existing pin IDs)
    for (auto& paramDef : m_VariableParams)
    {
        int pinId = paramDef.pinId;
        
        // Generate new ID if this parameter doesn't have one yet
        if (pinId < 0)
        {
            pinId = app->GetNextId();
            paramDef.pinId = pinId;  // Store for next rebuild
            LOG_INFO("[LogBlock::Build] Generated new pin ID {} for '{}'", pinId, paramDef.name);
        }
        else
        {
            LOG_INFO("[LogBlock::Build] Reusing existing pin ID {} for '{}'", pinId, paramDef.name);
        }
        
        // Add to parameter collections manually (don't call AddInputParameter which generates IDs)
        m_InputParams.push_back(pinId);
        node.Inputs.emplace_back(pinId, paramDef.name.c_str(), paramDef.type);
    }
    
    // Flow output
    AddOutput(app, node, "Done");
}

int LogBlock::Run(Node& node, App* app)
{
    LogWithConfiguredLevel("[Log] Log block (ID: {}) executing", GetID());
    
    // Get log file path from first input parameter
    std::string logFilePath;
    int paramIndex = 0;
    for (const auto& pin : node.Inputs)
    {
        if (pin.Type == PinType::Flow)
            continue;
        
        if (paramIndex == 0)
        {
            logFilePath = ParameterizedBlock::GetInputParamValueString(pin, node, app, "log_output.json");
            break;
        }
        paramIndex++;
    }
    
    if (logFilePath.empty())
    {
        logFilePath = "log_output.json";
    }
    
    LogWithConfiguredLevel("[Log] Writing to file: {}", logFilePath);
    
    // Create log entry with timestamp and all parameter values
    crude_json::value logEntry;
    
    // Add timestamp
    time_t now = time(nullptr);
    char timeBuffer[64];
    #ifdef _WIN32
    struct tm timeinfo;
    localtime_s(&timeinfo, &now);
    strftime(timeBuffer, sizeof(timeBuffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
    #else
    struct tm* timeinfo = localtime(&now);
    strftime(timeBuffer, sizeof(timeBuffer), "%Y-%m-%d %H:%M:%S", timeinfo);
    #endif
    logEntry["timestamp"] = std::string(timeBuffer);
    
    // Collect all input parameter values (skip FilePath and flow pins)
    crude_json::value parameters;
    paramIndex = 0;
    for (const auto& pin : node.Inputs)
    {
        if (pin.Type == PinType::Flow)
            continue;
        
        if (paramIndex == 0)  // Skip FilePath parameter
        {
            paramIndex++;
            continue;
        }
        
        // Get parameter value based on type
        crude_json::value paramValue;
        paramValue["name"] = pin.Name;
        
        switch (pin.Type)
        {
            case PinType::Bool:
            {
                bool value = ParameterizedBlock::GetInputParamValueBool(pin, node, app, false);
                paramValue["type"] = "bool";
                paramValue["value"] = value;
                break;
            }
            case PinType::Int:
            {
                int value = ParameterizedBlock::GetInputParamValueInt(pin, node, app, 0);
                paramValue["type"] = "int";
                paramValue["value"] = (double)value;
                break;
            }
            case PinType::Float:
            {
                float value = ParameterizedBlock::GetInputParamValueFloat(pin, node, app, 0.0f);
                paramValue["type"] = "float";
                paramValue["value"] = (double)value;
                break;
            }
            case PinType::String:
            {
                std::string value = ParameterizedBlock::GetInputParamValueString(pin, node, app, "");
                paramValue["type"] = "string";
                paramValue["value"] = value;
                break;
            }
            default:
                paramValue["type"] = "unknown";
                paramValue["value"] = "?";
                break;
        }
        
        parameters.push_back(paramValue);
        paramIndex++;
    }
    
    logEntry["parameters"] = parameters;
    
    // Output to console if enabled
    if (m_OutputToConsole)
    {
        LogWithConfiguredLevel("[Log Console] Timestamp: {}", logEntry["timestamp"].get<crude_json::string>());
        for (const auto& param : parameters.get<crude_json::array>())
        {
            std::string paramName = param["name"].get<crude_json::string>();
            std::string paramType = param["type"].get<crude_json::string>();
            std::string paramValueStr;
            
            // Format value based on type
            if (paramType == "bool")
            {
                paramValueStr = param["value"].get<bool>() ? "true" : "false";
            }
            else if (paramType == "int" || paramType == "float")
            {
                paramValueStr = spdlog::fmt_lib::format("{:.6g}", param["value"].get<double>());
            }
            else if (paramType == "string")
            {
                paramValueStr = "\"" + param["value"].get<crude_json::string>() + "\"";
            }
            else
            {
                paramValueStr = "?";
            }
            
            LogWithConfiguredLevel("[Log Console]   {} ({}) = {}", paramName, paramType, paramValueStr);
        }
    }
    
    // Build log array based on append mode
    crude_json::value logArray;
    
    if (m_AppendToFile)
    {
        // Append mode: read existing file and append new entry
        std::ifstream inFile(logFilePath);
        if (inFile)
        {
            std::string existingData((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
            inFile.close();
            
            if (!existingData.empty())
            {
                logArray = crude_json::value::parse(existingData);
                if (!logArray.is_array())
                {
                    LOG_WARN("[Log] Warning: Existing file is not a JSON array, creating new array");
                    logArray = crude_json::value(crude_json::type_t::array);
                }
            }
            else
            {
                logArray = crude_json::value(crude_json::type_t::array);
            }
        }
        else
        {
            // File doesn't exist, create new array
            logArray = crude_json::value(crude_json::type_t::array);
        }
        
        // Append new entry
        logArray.push_back(logEntry);
    }
    else
    {
        // Overwrite mode: create new array with just this entry
        logArray = crude_json::value(crude_json::type_t::array);
        logArray.push_back(logEntry);
    }
    
    // Write back to file with pretty formatting (4 space indent)
    std::ofstream outFile(logFilePath);
    if (outFile)
    {
        outFile << logArray.dump(4);
        outFile.close();
        LogWithConfiguredLevel("[Log] Successfully logged entry with {} parameter(s) to {} ({} mode)",
                               m_VariableParams.size(), logFilePath, m_AppendToFile ? "append" : "overwrite");
    }
    else
    {
        LOG_ERROR("[Log] ERROR: Failed to write to {}", logFilePath);
        return 1;  // Error
    }
    
    return E_OK;
}

void LogBlock::SaveState(Node& node, crude_json::value& nodeData, const Container* container, App* app)
{
    // Call parent to save unconnected parameter values
    ParameterizedBlock::SaveState(node, nodeData, container, app);
    
    // Save variable parameters configuration with stable pin IDs
    crude_json::value& varParams = nodeData["log_variable_params"];
    for (const auto& param : m_VariableParams)
    {
        crude_json::value paramData;
        paramData["name"] = param.name;
        paramData["type"] = (double)static_cast<int>(param.type);
        paramData["pin_id"] = (double)param.pinId;  // Save stable pin ID
        varParams.push_back(paramData);
    }
    
    // Save log settings
    nodeData["log_output_to_console"] = m_OutputToConsole;
    nodeData["log_append_to_file"] = m_AppendToFile;
    nodeData["log_level"] = static_cast<double>(m_LogLevel);
}

void LogBlock::LoadState(Node& node, const crude_json::value& nodeData, Container* container, App* app)
{
    // Load variable parameters configuration FIRST (before base class, so we can rebuild)
    if (nodeData.contains("log_variable_params") && nodeData["log_variable_params"].is_array())
    {
        m_VariableParams.clear();
        const auto& varParams = nodeData["log_variable_params"].get<crude_json::array>();
        
        for (const auto& paramData : varParams)
        {
            if (paramData.is_object() && paramData.contains("name") && paramData.contains("type"))
            {
                std::string name = paramData["name"].get<crude_json::string>();
                PinType type = static_cast<PinType>((int)paramData["type"].get<double>());
                
                // Load stable pin ID (if available)
                int pinId = -1;
                if (paramData.contains("pin_id"))
                {
                    pinId = (int)paramData["pin_id"].get<double>();
                    LOG_DEBUG("[LogBlock::LoadState] Loaded parameter '{}' with stable ID {}", name, pinId);
                }
                
                AddVariableParamDef(name, type, pinId);
            }
        }
    }
    
    // Rebuild node structure with loaded parameter definitions
    RebuildPins(node, app);
    
    LOG_DEBUG("[LogBlock::LoadState] Rebuilt node {} with {} variable parameters",
              node.ID.Get(), m_VariableParams.size());
    
    // Load log settings (with defaults if not present)
    if (nodeData.contains("log_output_to_console"))
        m_OutputToConsole = nodeData["log_output_to_console"].get<bool>();
    if (nodeData.contains("log_append_to_file"))
        m_AppendToFile = nodeData["log_append_to_file"].get<bool>();
    if (nodeData.contains("log_level"))
    {
        int levelValue = static_cast<int>(nodeData["log_level"].get<double>());
        if (levelValue >= static_cast<int>(spdlog::level::trace) &&
            levelValue <= static_cast<int>(spdlog::level::n_levels) - 1)
        {
            m_LogLevel = static_cast<spdlog::level::level_enum>(levelValue);
        }
    }
    
    // Call parent to load unconnected parameter values (after rebuilding structure)
    ParameterizedBlock::LoadState(node, nodeData, container, app);
}

void LogBlock::RenderEditUI(Node& node, App* app)
{
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextUnformatted("Log Settings");
    ImGui::Spacing();
    
    // Output to console checkbox
    ImGui::Checkbox("Output to Console", &m_OutputToConsole);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Also print log entries to console (stdout)");
    
    // Append to file checkbox
    ImGui::Checkbox("Append to File", &m_AppendToFile);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Append entries to existing file (if unchecked, file will be overwritten)");

    ImGui::Spacing();

    static const char* levelLabels[] = {"Trace", "Debug", "Info", "Warn", "Error", "Critical", "Off"};
    static const spdlog::level::level_enum levelValues[] = {
        spdlog::level::trace,
        spdlog::level::debug,
        spdlog::level::info,
        spdlog::level::warn,
        spdlog::level::err,
        spdlog::level::critical,
        spdlog::level::off
    };

    int currentLevelIndex = 0;
    for (int i = 0; i < IM_ARRAYSIZE(levelValues); ++i)
    {
        if (levelValues[i] == m_LogLevel)
        {
            currentLevelIndex = i;
            break;
        }
    }

    if (ImGui::Combo("Log Level", &currentLevelIndex, levelLabels, IM_ARRAYSIZE(levelLabels)))
    {
        m_LogLevel = levelValues[currentLevelIndex];
    }
}

void LogBlock::OnMenu(Node& node, App* app)
{
    // Call parent menu
    ParameterizedBlock::OnMenu(node, app);
    
    ImGui::Separator();
    ImGui::TextUnformatted("Variable Parameters");
    
    if (ImGui::BeginMenu("Add Parameter"))
    {
        if (ImGui::MenuItem("Bool"))
        {
            std::string paramName = "Param" + std::to_string(m_VariableParams.size() + 1);
            AddVariableParamDef(paramName, PinType::Bool);
            RebuildPins(node, app);
        }
        if (ImGui::MenuItem("Int"))
        {
            std::string paramName = "Param" + std::to_string(m_VariableParams.size() + 1);
            AddVariableParamDef(paramName, PinType::Int);
            RebuildPins(node, app);
        }
        if (ImGui::MenuItem("Float"))
        {
            std::string paramName = "Param" + std::to_string(m_VariableParams.size() + 1);
            AddVariableParamDef(paramName, PinType::Float);
            RebuildPins(node, app);
        }
        if (ImGui::MenuItem("String"))
        {
            std::string paramName = "Param" + std::to_string(m_VariableParams.size() + 1);
            AddVariableParamDef(paramName, PinType::String);
            RebuildPins(node, app);
        }
        ImGui::EndMenu();
    }
    
    if (!m_VariableParams.empty())
    {
        if (ImGui::BeginMenu("Remove Parameter"))
        {
            for (size_t i = 0; i < m_VariableParams.size(); ++i)
            {
                const auto& param = m_VariableParams[i];
                std::string label = param.name + " (" + 
                    (param.type == PinType::Bool ? "Bool" :
                     param.type == PinType::Int ? "Int" :
                     param.type == PinType::Float ? "Float" :
                     param.type == PinType::String ? "String" : "Unknown") + ")";
                
                if (ImGui::MenuItem(label.c_str()))
                {
                    RemoveVariableParamDef(i);
                    RebuildPins(node, app);
                    break;
                }
            }
            ImGui::EndMenu();
        }
    }
}

void LogBlock::RebuildPins(Node& node, App* app)
{
    // CRITICAL: Preserve UUIDs before clearing (pins are about to be destroyed!)
    std::map<int, Uuid64> oldPinUuids;  // runtime pin ID -> UUID
    for (const auto& input : node.Inputs)
    {
        if (input.UUID.IsValid())
            oldPinUuids[input.ID.Get()] = input.UUID;
    }
    for (const auto& output : node.Outputs)
    {
        if (output.UUID.IsValid())
            oldPinUuids[output.ID.Get()] = output.UUID;
    }
    
    LOG_DEBUG("[LogBlock::RebuildPins] Preserved {} pin UUIDs before rebuild", oldPinUuids.size());
    
    // Rebuild node structure
    node.Inputs.clear();
    node.Outputs.clear();
    m_InputParams.clear();
    m_OutputParams.clear();
    m_Inputs.clear();
    m_Outputs.clear();
    Build(node, app);
    
    // Restore or generate UUIDs for pins
    for (auto& input : node.Inputs)
    {
        int pinId = input.ID.Get();
        
        // Try to restore old UUID if this pin ID existed before
        auto it = oldPinUuids.find(pinId);
        if (it != oldPinUuids.end())
        {
            input.UUID = it->second;  // Restore old UUID
            app->m_UuidIdManager.RegisterPin(input.UUID, pinId);
            LOG_DEBUG("[LogBlock::RebuildPins] Restored UUID for input pin {}: 0x{:08X}{:08X}",
                      pinId, input.UUID.high, input.UUID.low);
        }
        else
        {
            // New pin - generate fresh UUID
            input.UUID = app->m_UuidIdManager.GenerateUuid();
            app->m_UuidIdManager.RegisterPin(input.UUID, pinId);
            LOG_DEBUG("[LogBlock::RebuildPins] Generated UUID for NEW input pin {}: 0x{:08X}{:08X}",
                      pinId, input.UUID.high, input.UUID.low);
        }
        
        input.Node = &node;
        input.Kind = PinKind::Input;
    }
    
    for (auto& output : node.Outputs)
    {
        int pinId = output.ID.Get();
        
        // Try to restore old UUID if this pin ID existed before
        auto it = oldPinUuids.find(pinId);
        if (it != oldPinUuids.end())
        {
            output.UUID = it->second;  // Restore old UUID
            app->m_UuidIdManager.RegisterPin(output.UUID, pinId);
            LOG_DEBUG("[LogBlock::RebuildPins] Restored UUID for output pin {}: 0x{:08X}{:08X}",
                      pinId, output.UUID.high, output.UUID.low);
        }
        else
        {
            // New pin - generate fresh UUID
            output.UUID = app->m_UuidIdManager.GenerateUuid();
            app->m_UuidIdManager.RegisterPin(output.UUID, pinId);
            LOG_DEBUG("[LogBlock::RebuildPins] Generated UUID for NEW output pin {}: 0x{:08X}{:08X}",
                      pinId, output.UUID.high, output.UUID.low);
        }
        
        output.Node = &node;
        output.Kind = PinKind::Output;
    }
}

void LogBlock::AddVariableParamDef(const std::string& name, PinType type, int pinId)
{
    m_VariableParams.push_back(VariableParamDef(name, type, pinId));
}

void LogBlock::RemoveVariableParamDef(size_t index)
{
    if (index < m_VariableParams.size())
    {
        m_VariableParams.erase(m_VariableParams.begin() + index);
    }
}

// Register the Log block
REGISTER_BLOCK(LogBlock, "Log");

