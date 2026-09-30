#define IMGUI_DEFINE_MATH_OPERATORS
#include <crude_json.h>
#include "group_block.h"
#include "../app.h"
#include "block.h"
#include "../utilities/node_renderer_base.h"
#include "NodeEx.h"
#include "constants.h"
#include "../Logging.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_node_editor.h>
#include <algorithm>
#include <map>

namespace ed = ax::NodeEditor;
using namespace ax::NodeRendering;
using namespace NodeConstants;

void GroupBlock::Build(Node& node, App* app)
{
    node.Type = m_Type;
    node.Color = m_Color;
    
    // Clear existing pin collections before rebuilding
    m_InputParams.clear();
    m_OutputParams.clear();
    m_Inputs.clear();
    m_Outputs.clear();
    
    // Rebuild pins from stored definitions, reusing existing pin IDs
    for (auto& pinDef : m_PinDefinitions)  // non-const to update PinId if needed
    {
        int pinId = pinDef.PinId;
        
        // Generate new ID if this pin definition doesn't have one yet
        if (pinId < 0)
        {
            pinId = app->GetNextId();
            pinDef.PinId = pinId;  
        }
       
        if (pinDef.Type == PinType::Flow)
        {
            // Flow pin - manually add to collections (don't call AddInput/AddOutput which generate IDs)
            if (pinDef.Kind == PinKind::Input)
            {
                m_Inputs.push_back(pinId);
                node.Inputs.emplace_back(pinId, pinDef.Name.c_str(), PinType::Flow);
            }
            else
            {
                m_Outputs.push_back(pinId);
                node.Outputs.emplace_back(pinId, pinDef.Name.c_str(), PinType::Flow);
            }
        }
        else
        {
            // Parameter pin - manually add to collections
            if (pinDef.Kind == PinKind::Input)
            {
                m_InputParams.push_back(pinId);
                node.Inputs.emplace_back(pinId, pinDef.Name.c_str(), pinDef.Type);
            }
            else
            {
                m_OutputParams.push_back(pinId);
                node.Outputs.emplace_back(pinId, pinDef.Name.c_str(), pinDef.Type);
            }
        }
    }
}

int GroupBlock::Run(Node& node, App* app)
{
    // Groups have no flow I/O by default, so nothing to execute
    return E_OK;
}

void GroupBlock::Render(Node& node, App* app, Pin* newLinkPin)
{
    // Switch rendering based on display mode
    switch (m_DisplayMode)
    {
        case GroupDisplayMode::Collapsed:
            RenderCollapsed(node, app, newLinkPin);
            break;
        case GroupDisplayMode::Expanded:
        default:
            RenderExpanded(node, app, newLinkPin);
            break;
    }
}

void GroupBlock::RenderCollapsed(Node& node, App* app, Pin* newLinkPin)
{
    // Use base class rendering like any other block
    ParameterizedBlock::Render(node, app, newLinkPin);
}

void GroupBlock::RenderExpanded(Node& node, App* app, Pin* newLinkPin)
{
    // Check if node is currently running (for red border visualization)
    float currentTime = ImGui::GetTime();
    bool isRunning = false;
    auto runningIt = app->m_RunningNodes.find(node.ID);
    if (runningIt != app->m_RunningNodes.end())
    {
        isRunning = (currentTime < runningIt->second);
    }
    
    // Get styles from StyleManager
    auto& styleManager = app->GetStyleManager();
    auto& groupStyle = styleManager.GroupStyle;
    
    ImColor borderColor = isRunning ? groupStyle.BorderColorRunning : groupStyle.BorderColor;
    float activeBorderWidth = isRunning ? groupStyle.BorderWidthRunning : groupStyle.BorderWidth;
    
    // Use NodeStyleScope for group appearance
    NodeStyleScope style(
        groupStyle.BgColor,
        borderColor,
        groupStyle.Rounding, activeBorderWidth,
        groupStyle.Padding,
        ImVec2(0.0f, 1.0f),
        ImVec2(0.0f, -1.0f)
    );
    
    ed::BeginNode(node.ID);
    ImGui::PushID(node.ID.AsPointer());
    
    ImGui::BeginVertical("collapsed_group");
    
    // MIDDLE: Header
    ImGui::BeginHorizontal("content");
    ImGui::Spring(1);
    
    ImGui::BeginVertical("header");
    ImGui::Spring(1);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    ImGui::TextUnformatted(node.Name.c_str());
    ImGui::PopStyleColor();
    ImGui::Spring(1);
    ImGui::EndVertical();
    
    ImGui::Spring(1);
    ImGui::EndHorizontal();
    
    // Resizable spacer area
    ImVec2 minSize(styleManager.MinGroupSize, styleManager.MinNodeHeight);
    ImVec2 targetSize = m_CollapsedSize;
    ImGui::Dummy(targetSize);
    
    ImGui::EndVertical();
    
    // Save cursor and get node bounds
    ImVec2 contentEndPos = ImGui::GetCursorScreenPos();
    ImVec2 nodePos = ed::GetNodePosition(node.ID);
    ImVec2 nodeSize = ed::GetNodeSize(node.ID);
    
    if (nodeSize.x <= 0 || nodeSize.y <= 0)
    {
        ImVec2 contentMin = ImGui::GetItemRectMin();
        ImVec2 contentMax = ImGui::GetItemRectMax();
        nodeSize = contentMax - contentMin;
    }
    
    ImRect nodeRect = ImRect(nodePos, nodePos + nodeSize);
    
    // Collect pins by type
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
    
    // Render input parameters at top edge using NodeEx
    if (!inputParams.empty())
    {
        float spacing = 1.0f / (inputParams.size() + 1);
        for (size_t i = 0; i < inputParams.size(); ++i)
        {
            Pin* pin = inputParams[i];
            float offset = spacing * (i + 1);
            float alpha = GetPinAlpha(pin, newLinkPin, app);
            
            ed::PinState state = (alpha < 1.0f) ? ed::PinState::Deactivated : ed::PinState::Normal;
            ImRect pinRect = ed::PinEx(pin->ID, ed::PinKind::Input, ed::PinEdge::Top, 
                                       offset, styleManager.ParameterPinEdgeOffset, nodeRect, state);
            
            pin->LastPivotPosition = ImVec2(pinRect.GetCenter().x, pinRect.Min.y);
            pin->LastRenderBounds = pinRect;
            pin->HasPositionData = true;
        }
    }
    
    // Render output parameters at bottom edge using NodeEx
    if (!outputParams.empty())
    {
        float spacing = 1.0f / (outputParams.size() + 1);
        for (size_t i = 0; i < outputParams.size(); ++i)
        {
            Pin* pin = outputParams[i];
            float offset = spacing * (i + 1);
            float alpha = GetPinAlpha(pin, newLinkPin, app);
            
            ed::PinState state = (alpha < 1.0f) ? ed::PinState::Deactivated : ed::PinState::Normal;
            ImRect pinRect = ed::PinEx(pin->ID, ed::PinKind::Output, ed::PinEdge::Bottom, 
                                       offset, styleManager.ParameterPinEdgeOffset, nodeRect, state);
            
            pin->LastPivotPosition = ImVec2(pinRect.GetCenter().x, pinRect.Max.y);
            pin->LastRenderBounds = pinRect;
            pin->HasPositionData = true;
        }
    }
    
    // Render flow inputs at left edge using NodeEx
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
                                       offset, styleManager.FlowPinEdgeOffset, nodeRect, state, nullptr, ed::RenderPinBox);
            
            pin->LastPivotPosition = ImVec2(pinRect.Min.x, pinRect.GetCenter().y);
            pin->LastRenderBounds = pinRect;
            pin->HasPositionData = true;
        }
    }
				
    // Render flow outputs at right edge using NodeEx
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
                                       offset, styleManager.FlowPinEdgeOffset, nodeRect, state, nullptr, ed::RenderPinBox);
            
            pin->LastPivotPosition = ImVec2(pinRect.Max.x, pinRect.GetCenter().y);
            pin->LastRenderBounds = pinRect;
            pin->HasPositionData = true;
        }
    }
    
    // Restore cursor
    ImGui::SetCursorScreenPos(contentEndPos);
    
    ImGui::PopID();
    ed::EndNode();
    
    // Draw resize grip AFTER EndNode using actual node bounds
    // Need to suspend editor to work in screen space
    ed::Suspend();
    
    // Reuse nodePos and nodeSize from above (already calculated for pin placement)
    if (nodeSize.x > 0 && nodeSize.y > 0)
    {
        // Convert node bounds to screen space
        ImVec2 nodeScreenMin = ed::CanvasToScreen(nodePos);
        ImVec2 nodeScreenMax = ed::CanvasToScreen(nodePos + nodeSize);
        
        // Position resize grip in bottom-right corner
        ImVec2 resizeGripSize(styleManager.GroupResizeGripSize, styleManager.GroupResizeGripSize);
        ImVec2 gripMin = nodeScreenMax - resizeGripSize;
        ImRect gripRect(gripMin, nodeScreenMax);
        
        // Draw resize grip visual (diagonal lines) - in screen space
        auto drawList = ImGui::GetWindowDrawList();
        ImU32 gripColor = styleManager.GroupResizeGripColor;
        for (int i = 0; i < 3; ++i)
        {
            float offset = i * styleManager.GroupResizeGripLineSpacing;
            ImVec2 p1 = ImVec2(gripRect.Min.x + offset, gripRect.Max.y);
            ImVec2 p2 = ImVec2(gripRect.Max.x, gripRect.Min.y + offset);
            drawList->AddLine(p1, p2, gripColor, 2.0f);
        }
        
        // Handle resize interaction - use ImGui button in screen space
        ImGui::PushID("##resize_grip");
        ImGui::SetCursorScreenPos(gripRect.Min);
        ImGui::InvisibleButton("##resize", gripRect.GetSize(), ImGuiButtonFlags_None);
        
        if (ImGui::IsItemHovered())
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
        }
        
        // Track resize state per node using static map keyed by node ID
        static std::map<int, bool> resizeActive;
        static std::map<int, ImVec2> resizeStartSize;
        static std::map<int, ImVec2> resizeStartMouseCanvas;
        
        const int nodeId = ToRuntimeId(node.ID);
        
        if (ImGui::IsItemActive())
        {
            if (ImGui::IsMouseDragging(0))
            {
                ImVec2 currentMouseScreen = ImGui::GetMousePos();
                ImVec2 currentMouseCanvas = ed::ScreenToCanvas(currentMouseScreen);
                
                if (resizeActive.find(nodeId) == resizeActive.end() || !resizeActive[nodeId])
                {
                    // Start of resize - store initial state
                    resizeActive[nodeId] = true;
                    resizeStartSize[nodeId] = m_CollapsedSize;
                    resizeStartMouseCanvas[nodeId] = currentMouseCanvas;
                }
                
                // Calculate delta from start position
                ImVec2 canvasDelta = currentMouseCanvas - resizeStartMouseCanvas[nodeId];
                
                // Apply delta to initial size
                ImVec2 newSize = resizeStartSize[nodeId] + canvasDelta;
                
                // Clamp to minimum size (in canvas coordinates)
                newSize.x = ImMax(newSize.x, minSize.x);
                newSize.y = ImMax(newSize.y, minSize.y);
                
                // Update size
                m_CollapsedSize = newSize;
            }
        }
        else
        {
            // Not active - clear resize state for this node
            resizeActive.erase(nodeId);
            resizeStartSize.erase(nodeId);
            resizeStartMouseCanvas.erase(nodeId);
        }
        
        ImGui::PopID();
    }
    
    ed::Resume();
}

void GroupBlock::ToggleDisplayMode()
{
    if (m_DisplayMode == GroupDisplayMode::Collapsed)
        m_DisplayMode = GroupDisplayMode::Expanded;
    else
        m_DisplayMode = GroupDisplayMode::Collapsed;
}

void GroupBlock::RebuildPins(Node& node, App* app)
{
    // CRITICAL: Preserve UUIDs before clearing (pins are about to be destroyed!)
    std::map<int, Uuid64> oldPinUuids;  // runtime pin ID -> UUID
    for (const auto& input : node.Inputs)
    {
        if (input.UUID.IsValid())
            oldPinUuids[ToRuntimeId(input.ID)] = input.UUID;
    }
    for (const auto& output : node.Outputs)
    {
        if (output.UUID.IsValid())
            oldPinUuids[ToRuntimeId(output.ID)] = output.UUID;
    }
    
    LOG_DEBUG("[GroupBlock::RebuildPins] Preserved {} pin UUIDs before rebuild", oldPinUuids.size());
    
    // Helper method to rebuild pins and update pointers
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
        const int pinId = ToRuntimeId(input.ID);
        
        // Try to restore old UUID if this pin ID existed before
        auto it = oldPinUuids.find(pinId);
        if (it != oldPinUuids.end())
        {
            input.UUID = it->second;  // Restore old UUID
            app->m_UuidIdManager.RegisterPin(input.UUID, pinId);
            LOG_DEBUG("[GroupBlock::RebuildPins] Restored UUID for input pin {}: 0x{:08X}{:08X}",
                      pinId, input.UUID.high, input.UUID.low);
        }
        else
        {
            // New pin - generate fresh UUID
            input.UUID = app->m_UuidIdManager.GenerateUuid();
            app->m_UuidIdManager.RegisterPin(input.UUID, pinId);
            LOG_DEBUG("[GroupBlock::RebuildPins] Generated UUID for NEW input pin {}: 0x{:08X}{:08X}",
                      pinId, input.UUID.high, input.UUID.low);
        }
        
        input.Node = &node;
        input.Kind = PinKind::Input;
    }
    
    for (auto& output : node.Outputs)
    {
        const int pinId = ToRuntimeId(output.ID);
        
        // Try to restore old UUID if this pin ID existed before
        auto it = oldPinUuids.find(pinId);
        if (it != oldPinUuids.end())
        {
            output.UUID = it->second;  // Restore old UUID
            app->m_UuidIdManager.RegisterPin(output.UUID, pinId);
            LOG_DEBUG("[GroupBlock::RebuildPins] Restored UUID for output pin {}: 0x{:08X}{:08X}",
                      pinId, output.UUID.high, output.UUID.low);
        }
        else
        {
            // New pin - generate fresh UUID
            output.UUID = app->m_UuidIdManager.GenerateUuid();
            app->m_UuidIdManager.RegisterPin(output.UUID, pinId);
            LOG_DEBUG("[GroupBlock::RebuildPins] Generated UUID for NEW output pin {}: 0x{:08X}{:08X}",
                      pinId, output.UUID.high, output.UUID.low);
        }
        
        output.Node = &node;
        output.Kind = PinKind::Output;
    }
}

void GroupBlock::AddPinDef(const std::string& name, PinType type, PinKind kind)
{
    // Note: Pin ID is generated lazily in Build() method
    // This allows for proper ID management when loading from file
    m_PinDefinitions.push_back(GroupPinDef(name, type, kind, -1));
}

void GroupBlock::AddFlowInput(const std::string& name)
{
    AddPinDef(name, PinType::Flow, PinKind::Input);
}

void GroupBlock::AddFlowOutput(const std::string& name)
{
    AddPinDef(name, PinType::Flow, PinKind::Output);
}

void GroupBlock::RemovePinDef(size_t index, PinKind kind)
{
    // Find and remove the pin definition by counting pins of the same kind
    size_t foundIndex = 0;
    for (auto it = m_PinDefinitions.begin(); it != m_PinDefinitions.end(); ++it)
    {
        if (it->Kind == kind)
        {
            if (foundIndex == index)
            {
                m_PinDefinitions.erase(it);
                break;
            }
            foundIndex++;
        }
    }
}

void GroupBlock::UpdatePinDefName(int pinId, const std::string& newName)
{
    // Match pin ID to pin definition by iterating through definitions
    // and counting how many pins of each type have been created
    // This matches the Build() order
    size_t flowInputIndex = 0, flowOutputIndex = 0;
    size_t paramInputIndex = 0, paramOutputIndex = 0;
    
    for (auto& pinDef : m_PinDefinitions)
    {
        if (pinDef.Type == PinType::Flow)
        {
            if (pinDef.Kind == PinKind::Input)
            {
                // Check if this flow input matches the pin ID
                if (flowInputIndex < m_Inputs.size() && m_Inputs[flowInputIndex] == pinId)
                {
                    pinDef.Name = newName;
                    return;
                }
                flowInputIndex++;
            }
            else
            {
                // Check if this flow output matches the pin ID
                if (flowOutputIndex < m_Outputs.size() && m_Outputs[flowOutputIndex] == pinId)
                {
                    pinDef.Name = newName;
                    return;
                }
                flowOutputIndex++;
            }
        }
        else
        {
            if (pinDef.Kind == PinKind::Input)
            {
                // Check if this param input matches the pin ID
                if (paramInputIndex < m_InputParams.size() && m_InputParams[paramInputIndex] == pinId)
                {
                    pinDef.Name = newName;
                    return;
                }
                paramInputIndex++;
            }
            else
            {
                // Check if this param output matches the pin ID
                if (paramOutputIndex < m_OutputParams.size() && m_OutputParams[paramOutputIndex] == pinId)
                {
                    pinDef.Name = newName;
                    return;
                }
                paramOutputIndex++;
            }
        }
    }
}

void GroupBlock::UpdatePinDefType(int pinId, PinType newType)
{
    // Match pin ID to pin definition (same logic as UpdatePinDefName)
    size_t flowInputIndex = 0, flowOutputIndex = 0;
    size_t paramInputIndex = 0, paramOutputIndex = 0;
    
    for (auto& pinDef : m_PinDefinitions)
    {
        if (pinDef.Type == PinType::Flow)
        {
            if (pinDef.Kind == PinKind::Input)
            {
                if (flowInputIndex < m_Inputs.size() && m_Inputs[flowInputIndex] == pinId)
                {
                    pinDef.Type = newType;
                    return;
                }
                flowInputIndex++;
            }
            else
            {
                if (flowOutputIndex < m_Outputs.size() && m_Outputs[flowOutputIndex] == pinId)
                {
                    pinDef.Type = newType;
                    return;
                }
                flowOutputIndex++;
            }
        }
        else
        {
            if (pinDef.Kind == PinKind::Input)
            {
                if (paramInputIndex < m_InputParams.size() && m_InputParams[paramInputIndex] == pinId)
                {
                    pinDef.Type = newType;
                    return;
                }
                paramInputIndex++;
            }
            else
            {
                if (paramOutputIndex < m_OutputParams.size() && m_OutputParams[paramOutputIndex] == pinId)
                {
                    pinDef.Type = newType;
                    return;
                }
                paramOutputIndex++;
            }
        }
    }
}

void GroupBlock::OnMenu(Node& node, App* app)
{
    // Call base class menu first
    ParameterizedBlock::OnMenu(node, app);
    
    ImGui::Separator();
    ImGui::TextUnformatted("Group");
    
    // Display mode toggle
    const char* modeStr = (m_DisplayMode == GroupDisplayMode::Collapsed) ? "Collapsed" : "Expanded";
    bool isCollapsed = (m_DisplayMode == GroupDisplayMode::Collapsed);
    
    if (ImGui::MenuItem("Collapsed Mode", nullptr, isCollapsed))
    {
        m_DisplayMode = GroupDisplayMode::Collapsed;
        // Notify editor that display mode changed (triggers link auto-adjustment)
        ed::NotifyBlockDisplayModeChanged(node.ID);
    }
    if (ImGui::MenuItem("Expanded Mode", nullptr, !isCollapsed))
    {
        m_DisplayMode = GroupDisplayMode::Expanded;
        // Notify editor that display mode changed (triggers link auto-adjustment)
        ed::NotifyBlockDisplayModeChanged(node.ID);
    }
    ImGui::Separator();
    ImGui::TextDisabled("Current: %s", modeStr);
    
    ImGui::Separator();
    ImGui::TextUnformatted("Group I/O");
    
    // Add Flow Input
    if (ImGui::MenuItem("Add Flow Input"))
    {
        AddPinDef("Execute", PinType::Flow, PinKind::Input);
        RebuildPins(node, app);
    }
    
    // Add Flow Output
    if (ImGui::MenuItem("Add Flow Output"))
    {
        AddPinDef("Done", PinType::Flow, PinKind::Output);
        RebuildPins(node, app);
    }
    
    ImGui::Separator();
    
    // Add Parameter Input submenu
    if (ImGui::BeginMenu("Add Parameter Input"))
    {
        if (ImGui::MenuItem("Bool")) {
            AddPinDef("Bool", PinType::Bool, PinKind::Input);
            RebuildPins(node, app);
        }
        if (ImGui::MenuItem("Int")) {
            AddPinDef("Int", PinType::Int, PinKind::Input);
            RebuildPins(node, app);
        }
        if (ImGui::MenuItem("Float")) {
            AddPinDef("Float", PinType::Float, PinKind::Input);
            RebuildPins(node, app);
        }
        if (ImGui::MenuItem("String")) {
            AddPinDef("String", PinType::String, PinKind::Input);
            RebuildPins(node, app);
        }
        ImGui::EndMenu();
    }
    
    // Add Parameter Output submenu
    if (ImGui::BeginMenu("Add Parameter Output"))
    {
        if (ImGui::MenuItem("Bool")) {
            AddPinDef("Bool", PinType::Bool, PinKind::Output);
            RebuildPins(node, app);
        }
        if (ImGui::MenuItem("Int")) {
            AddPinDef("Int", PinType::Int, PinKind::Output);
            RebuildPins(node, app);
        }
        if (ImGui::MenuItem("Float")) {
            AddPinDef("Float", PinType::Float, PinKind::Output);
            RebuildPins(node, app);
        }
        if (ImGui::MenuItem("String")) {
            AddPinDef("String", PinType::String, PinKind::Output);
            RebuildPins(node, app);
        }
        ImGui::EndMenu();
    }
    
    ImGui::Separator();
    
    // Remove pins submenus
    size_t inputFlowCount = 0, inputParamCount = 0;
    size_t outputFlowCount = 0, outputParamCount = 0;
    
    for (const auto& pinDef : m_PinDefinitions)
    {
        if (pinDef.Kind == PinKind::Input)
        {
            if (pinDef.Type == PinType::Flow) inputFlowCount++;
            else inputParamCount++;
        }
        else
        {
            if (pinDef.Type == PinType::Flow) outputFlowCount++;
            else outputParamCount++;
        }
    }
    
    if (inputFlowCount > 0 || inputParamCount > 0)
    {
        if (ImGui::BeginMenu("Remove Input"))
        {
            size_t inputIndex = 0;
            for (size_t i = 0; i < m_PinDefinitions.size(); ++i)
            {
                const auto& pinDef = m_PinDefinitions[i];
                if (pinDef.Kind != PinKind::Input) continue;
                
                std::string label = pinDef.Name + " (" + 
                    (pinDef.Type == PinType::Flow ? "Flow" : 
                     pinDef.Type == PinType::Bool ? "Bool" :
                     pinDef.Type == PinType::Int ? "Int" :
                     pinDef.Type == PinType::Float ? "Float" :
                     pinDef.Type == PinType::String ? "String" : "Unknown") + ")";
                
                // Use a lambda to capture the current inputIndex
                size_t currentIndex = inputIndex;
                if (ImGui::MenuItem(label.c_str()))
                {
                    RemovePinDef(currentIndex, PinKind::Input);
                    RebuildPins(node, app);
                    break;
                }
                
                inputIndex++;
            }
            ImGui::EndMenu();
        }
    }
    
    if (outputFlowCount > 0 || outputParamCount > 0)
    {
        if (ImGui::BeginMenu("Remove Output"))
        {
            size_t outputIndex = 0;
            for (size_t i = 0; i < m_PinDefinitions.size(); ++i)
            {
                const auto& pinDef = m_PinDefinitions[i];
                if (pinDef.Kind != PinKind::Output) continue;
                
                std::string label = pinDef.Name + " (" + 
                    (pinDef.Type == PinType::Flow ? "Flow" : 
                     pinDef.Type == PinType::Bool ? "Bool" :
                     pinDef.Type == PinType::Int ? "Int" :
                     pinDef.Type == PinType::Float ? "Float" :
                     pinDef.Type == PinType::String ? "String" : "Unknown") + ")";
                
                // Use a lambda to capture the current outputIndex
                size_t currentIndex = outputIndex;
                if (ImGui::MenuItem(label.c_str()))
                {
                    RemovePinDef(currentIndex, PinKind::Output);
                    RebuildPins(node, app);
                    break;
                }
                
                outputIndex++;
            }
            ImGui::EndMenu();
        }
    }
}

void GroupBlock::SaveState(Node& node, crude_json::value& nodeData, const Container* container, App* app)
{
    // Call base class to save parameter values
    ParameterizedBlock::SaveState(node, nodeData, container, app);
    
    // Save display mode
    nodeData["group_display_mode"] = (double)static_cast<int>(m_DisplayMode);
    
    // Save collapsed size if in collapsed mode
    if (m_DisplayMode == GroupDisplayMode::Collapsed)
    {
        nodeData["collapsed_size_x"] = m_CollapsedSize.x;
        nodeData["collapsed_size_y"] = m_CollapsedSize.y;
    }
    
    // Save pin definitions with stable pin IDs
    crude_json::value& pinDefs = nodeData["group_pin_definitions"];
    for (const auto& pinDef : m_PinDefinitions)
    {
        crude_json::value def;
        def["name"] = pinDef.Name;
        def["type"] = (double)static_cast<int>(pinDef.Type);
        def["kind"] = (double)static_cast<int>(pinDef.Kind);
        def["pin_id"] = (double)pinDef.PinId;  // Save stable pin ID
        pinDefs.push_back(def);
    }
}

void GroupBlock::LoadState(Node& node, const crude_json::value& nodeData, Container* container, App* app)
{
    // Load display mode
    if (nodeData.contains("group_display_mode"))
    {
        int modeValue = (int)nodeData["group_display_mode"].get<double>();
        m_DisplayMode = static_cast<GroupDisplayMode>(modeValue);
    }
    else
    {
        m_DisplayMode = GroupDisplayMode::Expanded;  // Default if not found
    }
    
    // Load collapsed size if available
    if (nodeData.contains("collapsed_size_x") && nodeData.contains("collapsed_size_y"))
    {
        m_CollapsedSize.x = (float)nodeData["collapsed_size_x"].get<double>();
        m_CollapsedSize.y = (float)nodeData["collapsed_size_y"].get<double>();
    }
    else
    {
        m_CollapsedSize = ImVec2(150.0f, 80.0f);  // Default collapsed size
    }
    
    // Load pin definitions FIRST (before base class, so we can rebuild after)
    if (nodeData.contains("group_pin_definitions"))
    {
        m_PinDefinitions.clear();
        const auto& pinDefs = nodeData["group_pin_definitions"];
        if (pinDefs.is_array())
        {
            for (const auto& def : pinDefs.get<crude_json::array>())
            {
                if (!def.is_object() || !def.contains("name") || !def.contains("type") || !def.contains("kind"))
                    continue;
                
                std::string name = def["name"].get<crude_json::string>();
                PinType type = static_cast<PinType>((int)def["type"].get<double>());
                PinKind kind = static_cast<PinKind>((int)def["kind"].get<double>());
                
                // Load stable pin ID (if available - for backward compatibility with old files)
                int pinId = -1;
                if (def.contains("pin_id"))
                {
                    pinId = (int)def["pin_id"].get<double>();
                    LOG_DEBUG("[GroupBlock::LoadState] Loaded pin '{}' with stable ID {}", 
                           name, pinId);
                }
                else
                {
                    LOG_DEBUG("[GroupBlock::LoadState] Pin '{}' has no saved ID (will generate new one)", 
                           name);
                }
                
                m_PinDefinitions.push_back(GroupPinDef(name, type, kind, pinId));
            }
        }
    }
    
    // Rebuild node structure with loaded pin definitions
    RebuildPins(node, app);
    
    LOG_DEBUG("[GroupBlock::LoadState] Rebuilt node {} with {} inputs, {} outputs",
              ToRuntimeId(node.ID), node.Inputs.size(), node.Outputs.size());
    
    // Call base class to load parameter values (after rebuilding structure)
    ParameterizedBlock::LoadState(node, nodeData, container, app);
}

// Register block
REGISTER_BLOCK(GroupBlock, "Group");



