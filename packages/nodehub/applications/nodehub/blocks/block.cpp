#define IMGUI_DEFINE_MATH_OPERATORS
#include <crude_json.h>

#include "block.h"
#include "../app.h"
#include "../utilities/node_renderer_base.h"
#include "../containers/container.h"
#include "NodeEx.h"
#include "constants.h"
#include <imgui_node_editor.h>
#include <imgui_internal.h>
#include <set>
#include "../Logging.h"

namespace ed = ax::NodeEditor;
using namespace ax::NodeRendering;
using namespace NodeConstants;

// Storage for parameter pin rendering context (to work around function pointer limitations)
struct ParameterPinContext {
    const Pin* pin;
    bool isLinked;
    App* app;
};
static ParameterPinContext s_CurrentParamPinContext = {nullptr, false, nullptr};

// Wrapper for parameter pins - uses NodeEx's RenderPinCircle
static void RenderParameterPin(ImDrawList* drawList, const ImVec2& center, const ImVec2& pinSize, 
                               ImU32 fillColor, ImU32 borderColor, ed::PinState state)
{
    // Use NodeEx's RenderPinCircle directly
    ed::RenderPinCircle(drawList, center, pinSize, fillColor, borderColor, state);
}

// Wrapper for flow pins - uses NodeEx's RenderPinBox
static void RenderFlowPin(ImDrawList* drawList, const ImVec2& center, const ImVec2& pinSize, 
                          ImU32 fillColor, ImU32 borderColor, ed::PinState state)
{
    // Use NodeEx's RenderPinBox directly
    ed::RenderPinBox(drawList, center, pinSize, fillColor, borderColor, state);
}

void ParameterizedBlock::Render(Node& node, App* app, Pin* newLinkPin)
{
    // Check if node is currently running (for red border visualization)
    double currentTime = (double)ImGui::GetTime();
    bool isRunning = false;
    auto runningIt = app->m_RunningNodes.find(node.ID);
    if (runningIt != app->m_RunningNodes.end())
    {
        isRunning = (currentTime < runningIt->second);
    }
    
    // Get styles from StyleManager
    auto& styleManager = app->GetStyleManager();
    auto& blockStyle = styleManager.BlockStyle;
    
    ImColor borderColor = isRunning ? blockStyle.BorderColorRunning : blockStyle.BorderColor;
    float activeBorderWidth = isRunning ? blockStyle.BorderWidthRunning : blockStyle.BorderWidth;
    
    // Use RAII style scope for automatic cleanup
    NodeStyleScope style(
        blockStyle.BgColor,                 // bg
        borderColor,                        // border (red if running)
        blockStyle.Rounding, activeBorderWidth,  // rounding, borderWidth (thicker if running)
        blockStyle.Padding,                 // padding
        ImVec2(0.0f, 1.0f),                 // source direction (down)
        ImVec2(0.0f, -1.0f)                 // target direction (up)
    );
    
    ed::BeginNode(node.ID);
    ImGui::PushID(node.ID.AsPointer());
    
    ImGui::BeginVertical("node");
    
    // Center - header (simple centered text)
    ImGui::BeginHorizontal("content");
    ImGui::Spring(1, 0.0f);
    
    ImGui::BeginVertical("center");
    ImGui::Dummy(ImVec2(styleManager.MinNodeWidth, 0));  // Minimum width
    ImGui::Spring(1);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    ImGui::TextUnformatted(node.Name.c_str());
    ImGui::PopStyleColor();
    ImGui::Spring(1);
    ImGui::EndVertical();
    
    ImGui::Spring(1, 0.0f);
    ImGui::EndHorizontal();
    
    ImGui::EndVertical();
    
    // Save cursor position before placing pins
    ImVec2 contentEndPos = ImGui::GetCursorScreenPos();
    
    // Get node bounds BEFORE EndNode (like in basic-interaction-example)
    ImVec2 nodePos = ed::GetNodePosition(node.ID);
    ImVec2 nodeSize = ed::GetNodeSize(node.ID);
    
    // If node size is not yet determined (first frame), calculate from content
    if (nodeSize.x <= 0 || nodeSize.y <= 0)
    {
        ImVec2 contentMin = ImGui::GetItemRectMin();
        ImVec2 contentMax = ImGui::GetItemRectMax();
        nodeSize = contentMax - contentMin;
    }
    
    // Calculate final node bounds
    ImRect nodeRect = ImRect(nodePos, nodePos + nodeSize);
    
    // Now place all pins using PinEx (BEFORE EndNode, using nodeRect)
    // Count pins for offset calculation
    std::vector<Pin*> inputParams;
    std::vector<Pin*> outputParams;
    std::vector<Pin*> flowInputs;
    std::vector<Pin*> flowOutputs;
    
    for (auto& pin : node.Inputs)
    {
        if (pin.Type == PinType::Flow)
            flowInputs.push_back(&pin);
        else
            inputParams.push_back(&pin);
    }
    
    for (auto& pin : node.Outputs)
    {
        if (pin.Type == PinType::Flow)
            flowOutputs.push_back(&pin);
        else
            outputParams.push_back(&pin);
    }
    
    // Render input parameters at top edge
    if (!inputParams.empty())
    {
        float spacing = 1.0f / (inputParams.size() + 1);
        for (size_t i = 0; i < inputParams.size(); ++i)
        {
            Pin* pin = inputParams[i];
            float offset = spacing * (i + 1);
            float alpha = GetPinAlpha(pin, newLinkPin, app);
            bool isLinked = app->IsPinLinked(pin->ID);
            
            // Set context for parameter pin renderer
            s_CurrentParamPinContext.pin = pin;
            s_CurrentParamPinContext.isLinked = isLinked;
            s_CurrentParamPinContext.app = app;
            
            ed::PinState state = (alpha < 1.0f) ? ed::PinState::Deactivated : ed::PinState::Normal;
            ImRect pinRect = ed::PinEx(pin->ID, ed::PinKind::Input, ed::PinEdge::Top, 
                                       offset, styleManager.ParameterPinEdgeOffset, nodeRect, state, nullptr, RenderParameterPin);
            
            // Store position data
            pin->LastPivotPosition = ImVec2(pinRect.GetCenter().x, pinRect.Min.y);
            pin->LastRenderBounds = pinRect;
            pin->HasPositionData = true;
        }
    }
    
    // Render output parameters at bottom edge
    if (!outputParams.empty())
    {
        float spacing = 1.0f / (outputParams.size() + 1);
        for (size_t i = 0; i < outputParams.size(); ++i)
        {
            Pin* pin = outputParams[i];
            float offset = spacing * (i + 1);
            float alpha = GetPinAlpha(pin, newLinkPin, app);
            bool isLinked = app->IsPinLinked(pin->ID);
            
            // Set context for parameter pin renderer
            s_CurrentParamPinContext.pin = pin;
            s_CurrentParamPinContext.isLinked = isLinked;
            s_CurrentParamPinContext.app = app;
            
            ed::PinState state = (alpha < 1.0f) ? ed::PinState::Deactivated : ed::PinState::Normal;
            ImRect pinRect = ed::PinEx(pin->ID, ed::PinKind::Output, ed::PinEdge::Bottom, 
                                       offset, styleManager.ParameterPinEdgeOffset, nodeRect, state, nullptr, RenderParameterPin);
            
            // Store position data
            pin->LastPivotPosition = ImVec2(pinRect.GetCenter().x, pinRect.Max.y);
            pin->LastRenderBounds = pinRect;
            pin->HasPositionData = true;
        }
    }
    
    // Render flow inputs at left edge
    if (!flowInputs.empty())
    {
        float spacing = 1.0f / (flowInputs.size() + 1);
        for (size_t i = 0; i < flowInputs.size(); ++i)
        {
            Pin* pin = flowInputs[i];
            float offset = spacing * (i + 1);
            float alpha = GetPinAlpha(pin, newLinkPin, app);
            
            ed::PinState state = (alpha < 1.0f) ? ed::PinState::Deactivated : ed::PinState::Normal;
            ImRect pinRect = ed::PinEx(pin->ID, ed::PinKind::Input, ed::PinEdge::Left, 
                                       offset, styleManager.FlowPinEdgeOffset, nodeRect, state, nullptr, RenderFlowPin);
            
            // Store position data
            pin->LastPivotPosition = ImVec2(pinRect.Min.x, pinRect.GetCenter().y);
            pin->LastRenderBounds = pinRect;
            pin->HasPositionData = true;
        }
    }
    
    // Render flow outputs at right edge
    if (!flowOutputs.empty())
    {
        float spacing = 1.0f / (flowOutputs.size() + 1);
        for (size_t i = 0; i < flowOutputs.size(); ++i)
        {
            Pin* pin = flowOutputs[i];
            float offset = spacing * (i + 1);
            float alpha = GetPinAlpha(pin, newLinkPin, app);
            
            ed::PinState state = (alpha < 1.0f) ? ed::PinState::Deactivated : ed::PinState::Normal;
            ImRect pinRect = ed::PinEx(pin->ID, ed::PinKind::Output, ed::PinEdge::Right, 
                                       offset, styleManager.FlowPinEdgeOffset, nodeRect, state, nullptr, RenderFlowPin);
            
            // Store position data
            pin->LastPivotPosition = ImVec2(pinRect.Max.x, pinRect.GetCenter().y);
            pin->LastRenderBounds = pinRect;
            pin->HasPositionData = true;
        }
    }
    
    // Restore cursor to end of content (pins don't affect layout)
    ImGui::SetCursorScreenPos(contentEndPos);
    
    ImGui::PopID();
    ed::EndNode();
    // NodeStyleScope destructor handles cleanup automatically
}

void ParameterizedBlock::AddInputParameter(App* app, Node& node, NH_CSTRING name, PinType type)
{
    int pinId = app->GetNextId();
    m_InputParams.push_back(pinId);
    // Add as input pin (will be rendered at top or connected from elsewhere)
    node.Inputs.emplace_back(pinId, name, type);
}

void ParameterizedBlock::AddOutputParameter(App* app, Node& node, NH_CSTRING name, PinType type)
{
    int pinId = app->GetNextId();
    m_OutputParams.push_back(pinId);
    // Add as output pin (will be rendered at bottom)
    node.Outputs.emplace_back(pinId, name, type);
}

void ParameterizedBlock::AddInput(App* app, Node& node, NH_CSTRING name)
{
    int pinId = app->GetNextId();
    m_Inputs.push_back(pinId);
    NH_CSTRING displayName = (name && *name) ? name : "";
    node.Inputs.emplace_back(pinId, displayName, PinType::Flow);
}

void ParameterizedBlock::AddOutput(App* app, Node& node, NH_CSTRING name)
{
    int pinId = app->GetNextId();
    m_Outputs.push_back(pinId);
    NH_CSTRING displayName = (name && *name) ? name : "";
    node.Outputs.emplace_back(pinId, displayName, PinType::Flow);
}

// Block activation API implementation
void Block::ActivateOutput(int pos, bool active)
{
    if (pos < 0) return;
    if (pos >= static_cast<int>(m_OutputActive.size()))
        m_OutputActive.resize(pos + 1, false);
    m_OutputActive[pos] = active;
}

bool Block::IsOutputActive(int pos) const
{
    if (pos < 0 || pos >= static_cast<int>(m_OutputActive.size()))
        return false;
    return m_OutputActive[pos];
}

void Block::ActivateInput(int pos, bool active)
{
    if (pos < 0) return;
    if (pos >= static_cast<int>(m_InputActive.size()))
        m_InputActive.resize(pos + 1, false);
    m_InputActive[pos] = active;
}

bool Block::IsInputActive(int pos) const
{
    if (pos < 0 || pos >= static_cast<int>(m_InputActive.size()))
        return false;
    return m_InputActive[pos];
}

void ParameterizedBlock::OnMenu(Node& node, App* app)
{
    ImGui::Separator();
    
    const char* modeStr = "Unknown";
    if (node.BlockDisplay == BlockDisplayMode::NameOnly) modeStr = "Name Only";
    else if (node.BlockDisplay == BlockDisplayMode::NameAndParameters) modeStr = "Name + Parameters";
    
    ImGui::Text("Display: %s", modeStr);
    
    if (ImGui::MenuItem("Cycle Display Mode (Space)"))
    {
        if (node.BlockDisplay == BlockDisplayMode::NameOnly)
            node.BlockDisplay = BlockDisplayMode::NameAndParameters;
        else
            node.BlockDisplay = BlockDisplayMode::NameOnly;
        
        // Notify editor that display mode changed (triggers link auto-adjustment)
        ed::NotifyBlockDisplayModeChanged(node.ID);
    }
    
    if (ImGui::MenuItem("Run (R)"))
    {
        int result = Run(node, app);
        // LOG_INFO("Block '{}' (ID: {}) Run() returned: {}", node.Name.c_str(), node.ID.Get(), result);
    }
}

// Parameter value helper implementations
int ParameterizedBlock::GetInputParamValueInt(const Pin& pin, Node& node, App* app, int defaultValue)
{
    // Check if connected to a parameter node or block
    auto* link = app->FindLinkConnectedToPin(pin.ID);
    if (link && link->EndPinID == pin.ID)
    {
        auto* sourcePin = app->FindPin(link->StartPinID);
        if (sourcePin && sourcePin->Node)
        {
            if (sourcePin->Node->Type == NodeType::Parameter)
            {
                // Run source parameter node first to get latest value
                sourcePin->Node->ParameterInstance->Run(*sourcePin->Node, app);
                return sourcePin->Node->IntValue;
            }
            else if (sourcePin->Node->IsBlockBased())
            {
                // Source is a block output - read from UnconnectedParamValues
                const int sourcePinId = ToRuntimeId(sourcePin->ID);
                auto& sourceParamValues = sourcePin->Node->UnconnectedParamValues;
                if (sourceParamValues.find(sourcePinId) != sourceParamValues.end())
                {
                    try {
                        return std::stoi(sourceParamValues[sourcePinId]);
                    } catch (...) {
                        return defaultValue;
                    }
                }
            }
        }
    }
    
    // Not connected, use default value from UnconnectedParamValues
    const int pinId = ToRuntimeId(pin.ID);
    auto& paramValues = node.UnconnectedParamValues;
    if (paramValues.find(pinId) != paramValues.end())
    {
        try {
            return std::stoi(paramValues[pinId]);
        } catch (...) {
            return defaultValue;
        }
    }
    
    return defaultValue;
}

float ParameterizedBlock::GetInputParamValueFloat(const Pin& pin, Node& node, App* app, float defaultValue)
{
    // Check if connected to a parameter node or block
    auto* link = app->FindLinkConnectedToPin(pin.ID);
    if (link && link->EndPinID == pin.ID)
    {
        auto* sourcePin = app->FindPin(link->StartPinID);
        if (sourcePin && sourcePin->Node)
        {
            if (sourcePin->Node->Type == NodeType::Parameter)
            {
                // Run source parameter node first to get latest value
                sourcePin->Node->ParameterInstance->Run(*sourcePin->Node, app);
                return sourcePin->Node->FloatValue;
            }
            else if (sourcePin->Node->IsBlockBased())
            {
                // Source is a block output - read from UnconnectedParamValues
                const int sourcePinId = ToRuntimeId(sourcePin->ID);
                auto& sourceParamValues = sourcePin->Node->UnconnectedParamValues;
                if (sourceParamValues.find(sourcePinId) != sourceParamValues.end())
                {
                    try {
                        return std::stof(sourceParamValues[sourcePinId]);
                    } catch (...) {
                        return defaultValue;
                    }
                }
            }
        }
    }
    
    // Not connected, use default value from UnconnectedParamValues
    const int pinId = ToRuntimeId(pin.ID);
    auto& paramValues = node.UnconnectedParamValues;
    if (paramValues.find(pinId) != paramValues.end())
    {
        try {
            return std::stof(paramValues[pinId]);
        } catch (...) {
            return defaultValue;
        }
    }
    
    return defaultValue;
}

bool ParameterizedBlock::GetInputParamValueBool(const Pin& pin, Node& node, App* app, bool defaultValue)
{
    // Check if connected to a parameter node or block
    auto* link = app->FindLinkConnectedToPin(pin.ID);
    if (link && link->EndPinID == pin.ID)
    {
        auto* sourcePin = app->FindPin(link->StartPinID);
        if (sourcePin && sourcePin->Node)
        {
            if (sourcePin->Node->Type == NodeType::Parameter)
            {
                // Run source parameter node first to get latest value
                sourcePin->Node->ParameterInstance->Run(*sourcePin->Node, app);
                return sourcePin->Node->BoolValue;
            }
            else if (sourcePin->Node->IsBlockBased())
            {
                // Source is a block output - read from UnconnectedParamValues
                const int sourcePinId = ToRuntimeId(sourcePin->ID);
                auto& sourceParamValues = sourcePin->Node->UnconnectedParamValues;
                if (sourceParamValues.find(sourcePinId) != sourceParamValues.end())
                {
                    const std::string& valueStr = sourceParamValues[sourcePinId];
                    if (valueStr == "true" || valueStr == "1")
                        return true;
                    else if (valueStr == "false" || valueStr == "0")
                        return false;
                }
            }
        }
    }
    
    // Not connected, use default value from UnconnectedParamValues
    const int pinId = ToRuntimeId(pin.ID);
    auto& paramValues = node.UnconnectedParamValues;
    if (paramValues.find(pinId) != paramValues.end())
    {
        const std::string& valueStr = paramValues[pinId];
        if (valueStr == "true" || valueStr == "1")
            return true;
        else if (valueStr == "false" || valueStr == "0")
            return false;
    }
    
    return defaultValue;
}

std::string ParameterizedBlock::GetInputParamValueString(const Pin& pin, Node& node, App* app, const std::string& defaultValue)
{
    // Check if connected to a parameter node or block
    auto* link = app->FindLinkConnectedToPin(pin.ID);
    if (link && link->EndPinID == pin.ID)
    {
        auto* sourcePin = app->FindPin(link->StartPinID);
        if (sourcePin && sourcePin->Node)
        {
            if (sourcePin->Node->Type == NodeType::Parameter)
            {
                // Run source parameter node first to get latest value
                sourcePin->Node->ParameterInstance->Run(*sourcePin->Node, app);
                return sourcePin->Node->StringValue;
            }
            else if (sourcePin->Node->IsBlockBased())
            {
                // Source is a block output - read from UnconnectedParamValues
                const int sourcePinId = ToRuntimeId(sourcePin->ID);
                auto& sourceParamValues = sourcePin->Node->UnconnectedParamValues;
                auto it = sourceParamValues.find(sourcePinId);
                if (it != sourceParamValues.end())
                {
                    return it->second;
                }
            }
        }
    }
    
    // Not connected, use default value from UnconnectedParamValues
    const int pinId = ToRuntimeId(pin.ID);
    auto& paramValues = node.UnconnectedParamValues;
    auto it = paramValues.find(pinId);
    if (it != paramValues.end())
    {
        return it->second;
    }
    
    return defaultValue;
}

void ParameterizedBlock::SetOutputParamValueInt(const Pin& pin, Node& node, App* app, int value)
{
    // Store output value in node's UnconnectedParamValues (output pins can be read by connected nodes)
    const int pinId = ToRuntimeId(pin.ID);
    node.UnconnectedParamValues[pinId] = std::to_string(value);
    
    // Propagate to ALL connected parameter nodes (iterate through all links from active container)
    auto* rootContainer = app->GetActiveRootContainer();
    if (!rootContainer) return;
    auto links = rootContainer->GetAllLinks();
    for (Link* linkPtr : links)
    {
        if (!linkPtr) continue;
        const auto& link = *linkPtr;
        if (link.StartPinID == pin.ID) // We're the source (output pin)
        {
            auto* targetPin = app->FindPin(link.EndPinID);
            if (targetPin && targetPin->Node && targetPin->Node->Type == NodeType::Parameter)
            {
                // Update connected parameter node
                targetPin->Node->IntValue = value;
                if (targetPin->Node->ParameterInstance)
                    targetPin->Node->ParameterInstance->SetInt(value);
            }
        }
    }
}

void ParameterizedBlock::SetOutputParamValueFloat(const Pin& pin, Node& node, App* app, float value)
{
    // Store output value in node's UnconnectedParamValues
    const int pinId = ToRuntimeId(pin.ID);
    char buf[32];
    snprintf(buf, sizeof(buf), "%.6g", value);
    node.UnconnectedParamValues[pinId] = buf;
    
    // Propagate to ALL connected parameter nodes (iterate through all links from active container)
    auto* rootContainer = app->GetActiveRootContainer();
    if (!rootContainer) return;
    auto links = rootContainer->GetAllLinks();
    for (Link* linkPtr : links)
    {
        if (!linkPtr) continue;
        const auto& link = *linkPtr;
        if (link.StartPinID == pin.ID)
        {
            auto* targetPin = app->FindPin(link.EndPinID);
            if (targetPin && targetPin->Node && targetPin->Node->Type == NodeType::Parameter)
            {
                targetPin->Node->FloatValue = value;
                if (targetPin->Node->ParameterInstance)
                    targetPin->Node->ParameterInstance->SetFloat(value);
            }
        }
    }
}

void ParameterizedBlock::SetOutputParamValueBool(const Pin& pin, Node& node, App* app, bool value)
{
    // Store output value in node's UnconnectedParamValues
    const int pinId = ToRuntimeId(pin.ID);
    node.UnconnectedParamValues[pinId] = value ? "true" : "false";
    
    // Propagate to ALL connected parameter nodes (iterate through all links from active container)
    auto* rootContainer = app->GetActiveRootContainer();
    if (!rootContainer) return;
    auto links = rootContainer->GetAllLinks();
    for (Link* linkPtr : links)
    {
        if (!linkPtr) continue;
        const auto& link = *linkPtr;
        if (link.StartPinID == pin.ID) // We're the source (output pin)
        {
            auto* targetPin = app->FindPin(link.EndPinID);
            if (targetPin && targetPin->Node && targetPin->Node->Type == NodeType::Parameter)
            {
                // Update connected parameter node
                targetPin->Node->BoolValue = value;
                if (targetPin->Node->ParameterInstance)
                    targetPin->Node->ParameterInstance->SetBool(value);
            }
        }
    }
}

void ParameterizedBlock::SetOutputParamValueString(const Pin& pin, Node& node, App* app, const std::string& value)
{
    // Store output value in node's UnconnectedParamValues
    int pinId = ToRuntimeId(pin.ID);
    node.UnconnectedParamValues[pinId] = value;
    
    // Propagate to ALL connected parameter nodes (iterate through all links from active container)
    auto* rootContainer = app->GetActiveRootContainer();
    if (!rootContainer) return;
    auto links = rootContainer->GetAllLinks();
    for (Link* linkPtr : links)
    {
        if (!linkPtr) continue;
        const auto& link = *linkPtr;
        if (link.StartPinID == pin.ID)
        {
            auto* targetPin = app->FindPin(link.EndPinID);
            if (targetPin && targetPin->Node && targetPin->Node->Type == NodeType::Parameter)
            {
                targetPin->Node->StringValue = value;
                if (targetPin->Node->ParameterInstance)
                    targetPin->Node->ParameterInstance->SetString(value);
            }
        }
    }
}

void ParameterizedBlock::SaveState(Node& node, crude_json::value& nodeData, const Container* container, App* app)
{
    // Save all input and output parameter values with their types, regardless of connection status
    if (!app || !container)
        return;
    
    crude_json::value& inputs = nodeData["inputs"];
    
    // Save all input parameter values (connected or not)
    for (auto& pin : node.Inputs)
    {
        // Skip flow pins - only save parameter input pins
        if (pin.Type == PinType::Flow)
            continue;
        
        const int pinId = ToRuntimeId(pin.ID);
        auto& paramValues = node.UnconnectedParamValues;
        auto it = paramValues.find(pinId);
        
        // Save input value and type if it exists (save regardless of connection status)
        if (it != paramValues.end() && !it->second.empty())
        {
            // Save pin ID, value as string, and type
            crude_json::value paramEntry;
            paramEntry["pin_id"] = (double)pinId;
            paramEntry["value"] = it->second;
            paramEntry["type"] = (double)static_cast<int>(pin.Type);
            inputs.push_back(paramEntry);
        }
        else
        {
            // Even if no value stored, save the pin with its type (for structure preservation)
            crude_json::value paramEntry;
            paramEntry["pin_id"] = (double)pinId;
            paramEntry["value"] = "";
            paramEntry["type"] = (double)static_cast<int>(pin.Type);
            inputs.push_back(paramEntry);
        }
    }
    
    // Save all output parameter values (connected or not)
    crude_json::value& outputs = nodeData["outputs"];
    
    for (auto& pin : node.Outputs)
    {
        // Skip flow pins - only save parameter output pins
        if (pin.Type == PinType::Flow)
            continue;
        
        const int pinId = ToRuntimeId(pin.ID);
        auto& paramValues = node.UnconnectedParamValues;
        auto it = paramValues.find(pinId);
        
        // Save output value and type if it exists (save regardless of connection status)
        if (it != paramValues.end() && !it->second.empty())
        {
            // Save pin ID, value as string, and type
            crude_json::value paramEntry;
            paramEntry["pin_id"] = (double)pinId;
            paramEntry["value"] = it->second;
            paramEntry["type"] = (double)static_cast<int>(pin.Type);
            outputs.push_back(paramEntry);
        }
        else
        {
            // Even if no value stored, save the pin with its type (for structure preservation)
            crude_json::value paramEntry;
            paramEntry["pin_id"] = (double)pinId;
            paramEntry["value"] = "";
            paramEntry["type"] = (double)static_cast<int>(pin.Type);
            outputs.push_back(paramEntry);
        }
    }
}

void ParameterizedBlock::LoadState(Node& node, const crude_json::value& nodeData, Container* container, App* app)
{
    // Load unconnected parameter input values
    if (nodeData.contains("inputs"))
    {
        const auto& inputs = nodeData["inputs"];
        if (inputs.is_array())
        {
            // Restore values to UnconnectedParamValues map
            auto& paramValues = node.UnconnectedParamValues;
            
            for (const auto& paramEntry : inputs.get<crude_json::array>())
            {
                if (!paramEntry.is_object() || !paramEntry.contains("pin_id") || !paramEntry.contains("value"))
                    continue;
                
                int pinId = (int)paramEntry["pin_id"].get<double>();
                std::string value = paramEntry["value"].get<crude_json::string>();
                
                // Verify this pin ID exists in the node's inputs
                // This handles cases where pin IDs might have changed (though they shouldn't)
                bool pinExists = false;
                for (const auto& pin : node.Inputs)
                {
                    if (pin.ID.Get() == pinId)
                    {
                        pinExists = true;
                        // Store the value - pin type validation happens when reading
                        paramValues[pinId] = value;
                        break;
                    }
                }
                
                // Note: We store even if pin not found yet (in case node structure changes)
                // The GetInputParamValue* functions will handle missing pins gracefully
                if (!pinExists)
                {
                    paramValues[pinId] = value;
                }
            }
        }
    }
    
    // Load output parameter values (connected or not)
    if (nodeData.contains("outputs"))
    {
        const auto& outputs = nodeData["outputs"];
        if (outputs.is_array())
        {
            // Restore values to UnconnectedParamValues map
            auto& paramValues = node.UnconnectedParamValues;
            
            for (const auto& paramEntry : outputs.get<crude_json::array>())
            {
                if (!paramEntry.is_object() || !paramEntry.contains("pin_id") || !paramEntry.contains("value"))
                    continue;
                
                int pinId = (int)paramEntry["pin_id"].get<double>();
                std::string value = paramEntry["value"].get<crude_json::string>();
                
                // Verify this pin ID exists in the node's outputs
                bool pinExists = false;
                for (const auto& pin : node.Outputs)
                {
                    if (pin.ID.Get() == pinId)
                    {
                        pinExists = true;
                        // Store the value - pin type validation happens when reading
                        paramValues[pinId] = value;
                        break;
                    }
                }
                
                // Note: We store even if pin not found yet (in case node structure changes)
                if (!pinExists)
                {
                    paramValues[pinId] = value;
                }
            }
        }
    }
}

