#define IMGUI_DEFINE_MATH_OPERATORS
#include <crude_json.h>

#include "../all.h"

#include "parameter_node.h"
#include "../app.h"
#include "block.h" 
#include "../utilities/node_renderer_base.h"
#include "../containers/container.h"
#include "NodeEx.h"
#include "constants.h"
#include <imgui_internal.h>
#include <imgui_node_editor.h>

namespace ed = ax::NodeEditor;
using namespace ax::NodeRendering;
using namespace NodeConstants;

void ParameterNode::InitializeDefaultValue()
{
    switch (m_Type)
    {
        case PinType::Bool:   m_BoolValue = false; break;
        case PinType::Int:    m_IntValue = 0; break;
        case PinType::Float:  m_FloatValue = 0.0f; break;
        case PinType::String: m_StringValue = ""; break;
        default: m_IntValue = 0; break;
    }
}

void ParameterNode::CycleDisplayMode()
{
    switch (m_DisplayMode)
    {
        case ParameterDisplayMode::NameOnly:
            m_DisplayMode = ParameterDisplayMode::NameAndValue;
            break;
        case ParameterDisplayMode::NameAndValue:
            m_DisplayMode = ParameterDisplayMode::SmallBox;
            break;
        case ParameterDisplayMode::SmallBox:
            m_DisplayMode = ParameterDisplayMode::Minimal;
            break;
        case ParameterDisplayMode::Minimal:
            m_DisplayMode = ParameterDisplayMode::MinimalLinks;
            break;
        case ParameterDisplayMode::MinimalLinks:
            m_DisplayMode = ParameterDisplayMode::NameOnly;
            break;
    }
}

std::string ParameterNode::GetValueString() const
{
    char buffer[256];
    switch (m_Type)
    {
        case PinType::Bool:   return m_BoolValue ? "true" : "false";
        case PinType::Int:    snprintf(buffer, sizeof(buffer), "%d", m_IntValue); return buffer;
        case PinType::Float:  snprintf(buffer, sizeof(buffer), "%.2f", m_FloatValue); return buffer;
        case PinType::String: return m_StringValue;
        default: return "";
    }
}

void ParameterNode::SyncValueToSource(App* app)
{
    // Only sync if this is a shortcut (has source ID)
    if (!app || m_SourceID == 0)
        return;
    
    // Find the source node
    Node* sourceNode = app->FindNode(ed::NodeId(m_SourceID));
    if (!sourceNode || !sourceNode->ParameterInstance)
    {
        // Source doesn't exist - orphaned shortcut, clear reference
        printf("[SYNC] Parameter node: Source node %d not found, clearing shortcut reference\n", m_SourceID);
        m_SourceID = 0;
        m_IsSource = false;
        return;
    }
    
    // Update source's value based on this shortcut's value
    switch (m_Type)
    {
        case PinType::Bool:
            sourceNode->ParameterInstance->SetBool(m_BoolValue);
            sourceNode->BoolValue = m_BoolValue;
            break;
        case PinType::Int:
            sourceNode->ParameterInstance->SetInt(m_IntValue);
            sourceNode->IntValue = m_IntValue;
            break;
        case PinType::Float:
            sourceNode->ParameterInstance->SetFloat(m_FloatValue);
            sourceNode->FloatValue = m_FloatValue;
            break;
        case PinType::String:
            sourceNode->ParameterInstance->SetString(m_StringValue);
            sourceNode->StringValue = m_StringValue;
            break;
        default:
            break;
    }
}

void ParameterNode::SyncValueToAllShortcuts(Node& node, App* app)
{
    // Only sync if this is a source node
    if (!app || !m_IsSource)
        return;
    
    // Get all nodes from active root container
    auto* container = app->GetActiveRootContainer();
    if (!container)
        return;
    
    auto allNodes = container->GetNodes(app);
    
    // Find all shortcuts that reference this source
    for (Node* otherNode : allNodes)
    {
        if (!otherNode || otherNode->ID == node.ID)
            continue;
        
        if (otherNode->Type == NodeType::Parameter && otherNode->ParameterInstance)
        {
            // Check if this node is a shortcut referencing us
            if (otherNode->ParameterInstance->GetSourceID() == m_ID)
            {
                // Update shortcut's value from source
                switch (m_Type)
                {
                    case PinType::Bool:
                        otherNode->ParameterInstance->SetBool(m_BoolValue);
                        otherNode->BoolValue = m_BoolValue;
                        break;
                    case PinType::Int:
                        otherNode->ParameterInstance->SetInt(m_IntValue);
                        otherNode->IntValue = m_IntValue;
                        break;
                    case PinType::Float:
                        otherNode->ParameterInstance->SetFloat(m_FloatValue);
                        otherNode->FloatValue = m_FloatValue;
                        break;
                    case PinType::String:
                        otherNode->ParameterInstance->SetString(m_StringValue);
                        otherNode->StringValue = m_StringValue;
                        break;
                    default:
                        break;
                }
            }
        }
    }
}

void ParameterNode::SyncNameToSource(App* app)
{
    // Only sync if this is a shortcut (has source ID)
    if (!app || m_SourceID == 0)
        return;
    
    // Find the source node
    Node* sourceNode = app->FindNode(ed::NodeId(m_SourceID));
    if (!sourceNode || !sourceNode->ParameterInstance)
    {
        // Source doesn't exist - orphaned shortcut, clear reference
        printf("[SYNC] Parameter node: Source node %d not found for name sync, clearing shortcut reference\n", m_SourceID);
        m_SourceID = 0;
        m_IsSource = false;
        return;
    }
    
    // Update source's name from this shortcut
    sourceNode->ParameterInstance->SetName(m_Name.c_str());
    sourceNode->Name = m_Name;
}

void ParameterNode::SyncNameToAllShortcuts(Node& node, App* app)
{
    // Only sync if this is a source node
    if (!app || !m_IsSource)
        return;
    
    // Get all nodes from active root container
    auto* container = app->GetActiveRootContainer();
    if (!container)
        return;
    
    auto allNodes = container->GetNodes(app);
    
    // Find all shortcuts that reference this source
    for (Node* otherNode : allNodes)
    {
        if (!otherNode || otherNode->ID == node.ID)
            continue;
        
        if (otherNode->Type == NodeType::Parameter && otherNode->ParameterInstance)
        {
            // Check if this node is a shortcut referencing us
            if (otherNode->ParameterInstance->GetSourceID() == m_ID)
            {
                // Update shortcut's name from source
                otherNode->ParameterInstance->SetName(m_Name.c_str());
                otherNode->Name = m_Name;
            }
        }
    }
}

int ParameterNode::Run(Node& node, App* app)
{
    return RunInternal(node, app, 0);
}

int ParameterNode::RunInternal(Node& node, App* app, int depth)
{
    // Prevent infinite recursion (max depth of 10)
    if (depth > 10 || !app || node.Inputs.empty())
        return E_OK;
    
    // If this is a shortcut, sync value from source first (before checking input connections)
    if (m_SourceID > 0)
    {
        Node* sourceNode = app->FindNode(ed::NodeId(m_SourceID));
        if (sourceNode && sourceNode->ParameterInstance)
        {
            // Source exists - sync value from source
            switch (m_Type)
            {
                case PinType::Bool:
                    m_BoolValue = sourceNode->ParameterInstance->GetBool();
                    node.BoolValue = m_BoolValue;
                    break;
                case PinType::Int:
                    m_IntValue = sourceNode->ParameterInstance->GetInt();
                    node.IntValue = m_IntValue;
                    break;
                case PinType::Float:
                    m_FloatValue = sourceNode->ParameterInstance->GetFloat();
                    node.FloatValue = m_FloatValue;
                    break;
                case PinType::String:
                    m_StringValue = sourceNode->ParameterInstance->GetString();
                    node.StringValue = m_StringValue;
                    break;
                default:
                    break;
            }
        }
        else
        {
            // Source doesn't exist - orphaned shortcut, clear reference
            printf("[RUN] Parameter node %d: Source node %d not found, clearing shortcut reference\n",
                   ToRuntimeId(node.ID), m_SourceID);
            m_SourceID = 0;
            m_IsSource = false;
        }
    }
    
    // Get the input pin (should be at index 0)
    auto& inputPin = node.Inputs[0];
    
    // Find link connected to this input pin (must be EndPinID - data flows TO this input)
    auto* link = app->FindLinkConnectedToPin(inputPin.ID);
    if (!link || link->EndPinID != inputPin.ID)
    {
        // No connection or link is in wrong direction, keep current value
        return E_OK;
    }
    
    // Get the source pin (StartPinID of the link - where data flows FROM)
    auto* sourcePin = app->FindPin(link->StartPinID);
    if (!sourcePin || !sourcePin->Node)
    {
        // Invalid source
        return E_OK;
    }
    
    // Get value from source node based on its type
    auto* sourceNode = sourcePin->Node;
    
    if (sourceNode->Type == NodeType::Parameter && sourceNode->ParameterInstance)
    {
        // First, run the source node to ensure it has the latest value from its inputs
        // This propagates values through the chain (with depth limit to prevent recursion)
        sourceNode->ParameterInstance->RunInternal(*sourceNode, app, depth + 1);
        
        // Read from node structure values (source of truth - these are updated via UI and Run())
        // This ensures we always get the current displayed value
        switch (m_Type)
        {
            case PinType::Bool:
                m_BoolValue = sourceNode->BoolValue;
                node.BoolValue = m_BoolValue;
                break;
            case PinType::Int:
                m_IntValue = sourceNode->IntValue;
                node.IntValue = m_IntValue;
                break;
            case PinType::Float:
                m_FloatValue = sourceNode->FloatValue;
                node.FloatValue = m_FloatValue;
                break;
            case PinType::String:
                m_StringValue = sourceNode->StringValue;
                node.StringValue = m_StringValue;
                break;
            default:
                break;
        }
        
        // Sync the source ParameterInstance to match its node value (only the matching type)
        // This ensures ParameterInstance stays in sync with node structure
        switch (sourceNode->ParameterType)
        {
            case PinType::Bool:
                sourceNode->ParameterInstance->SetBool(sourceNode->BoolValue);
                break;
            case PinType::Int:
                sourceNode->ParameterInstance->SetInt(sourceNode->IntValue);
                break;
            case PinType::Float:
                sourceNode->ParameterInstance->SetFloat(sourceNode->FloatValue);
                break;
            case PinType::String:
                sourceNode->ParameterInstance->SetString(sourceNode->StringValue);
                break;
            default:
                break;
        }
    }
    else if (sourceNode->IsBlockBased() && sourceNode->BlockInstance)
    {
        // Source is a block output - read value from block's output parameter
        // Block outputs store their values in UnconnectedParamValues
        const int sourcePinId = ToRuntimeId(sourcePin->ID);
        auto& paramValues = sourceNode->UnconnectedParamValues;
        
        if (paramValues.find(sourcePinId) != paramValues.end())
        {
            const std::string& valueStr = paramValues[sourcePinId];
            
            // Convert string value to appropriate type
            try {
                switch (m_Type)
                {
                    case PinType::Bool:
                        m_BoolValue = (valueStr == "true" || valueStr == "1");
                        node.BoolValue = m_BoolValue;
                        break;
                    case PinType::Int:
                        m_IntValue = std::stoi(valueStr);
                        node.IntValue = m_IntValue;
                        break;
                    case PinType::Float:
                        m_FloatValue = std::stof(valueStr);
                        node.FloatValue = m_FloatValue;
                        break;
                    case PinType::String:
                        m_StringValue = valueStr;
                        node.StringValue = m_StringValue;
                        break;
                    default:
                        break;
                }
            } catch (...) {
                // Conversion failed, keep current value
            }
        }
    }
    
    return E_OK;
}

void ParameterNode::Build(Node& node, App* app)
{
    // Parameter node has both input and output pins
    int inputPinId = app->GetNextId();
    node.Inputs.emplace_back(inputPinId, "", m_Type);
    
    int outputPinId = app->GetNextId();
    node.Outputs.emplace_back(outputPinId, "", m_Type);
    
    // Store parameter data in node
    node.Type = NodeType::Parameter;
    node.ParameterType = m_Type;
    
    // Copy values to node structure
    switch (m_Type)
    {
        case PinType::Bool:   node.BoolValue = m_BoolValue; break;
        case PinType::Int:    node.IntValue = m_IntValue; break;
        case PinType::Float:  node.FloatValue = m_FloatValue; break;
        case PinType::String: node.StringValue = m_StringValue; break;
        default: break;
    }
}

void ParameterNode::Render(Node& node, App* app, Pin* newLinkPin)
{
    switch (m_DisplayMode)
    {
        case ParameterDisplayMode::NameOnly:
            RenderNameOnly(node, app, newLinkPin);
            break;
        case ParameterDisplayMode::NameAndValue:
            RenderNameAndValue(node, app, newLinkPin);
            break;
        case ParameterDisplayMode::SmallBox:
            RenderSmallBox(node, app, newLinkPin);
            break;
        case ParameterDisplayMode::Minimal:
            RenderMinimal(node, app, newLinkPin);
            break;
        case ParameterDisplayMode::MinimalLinks:
            RenderMinimalLinks(node, app, newLinkPin);
            break;
    }
}

void ParameterNode::RenderNameOnly(Node& node, App* app, Pin* newLinkPin)
{
    // Get styles from StyleManager
    auto& styleManager = app->GetStyleManager();
    auto& paramStyle = styleManager.ParameterStyle;
    
    // Use dedicated ParameterStyle background color (visually distinct from blocks)
    ImColor bgColor = paramStyle.BgColor;
    ImColor borderColor = paramStyle.BorderColor;
    float borderWidth = paramStyle.BorderWidth;
    
    if (m_IsSource)
    {
        borderColor = styleManager.ParamBorderColorSource;
        borderWidth = styleManager.ParamBorderWidthSource;
    }
    else if (m_SourceID > 0)
    {
        // Shortcut node: dimmed background
        bgColor = ImColor(bgColor.Value.x * 0.7f, bgColor.Value.y * 0.7f, bgColor.Value.z * 0.7f, bgColor.Value.w);
        borderColor = styleManager.ParamBorderColorShortcut;
    }
    
    NodeStyleScope style(
        bgColor,
        borderColor,
        paramStyle.Rounding, borderWidth,
        styleManager.ParamPaddingNameOnly,
        ImVec2(0.0f, 1.0f),
        ImVec2(0.0f, -1.0f)
    );
    
    ed::BeginNode(node.ID);
    ImGui::PushID(node.ID.AsPointer());
    ImGui::BeginVertical("param_node");
    
    // Editable name
    ImGui::BeginHorizontal("param_name");
    ImGui::PushItemWidth(styleManager.ParamInputWidthNameOnly);
    char nameBuffer[64];
    strncpy(nameBuffer, node.Name.c_str(), 63);
    nameBuffer[63] = '\0';
    
    static ed::NodeId editingNode = 0;
    if (ImGui::InputText("##name", nameBuffer, 64))
    {
        node.Name = nameBuffer;
        SetName(nameBuffer);
        
        if (m_IsSource)
            SyncNameToAllShortcuts(node, app);
    }
    
    HandleTextInput(ImGui::IsItemActive(), node.ID, editingNode);
    
    ImGui::PopItemWidth();
    ImGui::EndHorizontal();
    
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
    
    // Place pins at top/bottom edges using NodeEx
    auto& input = node.Inputs[0];
    auto& output = node.Outputs[0];
    
    float inputAlpha = GetPinAlpha(&input, newLinkPin, app);
    float outputAlpha = GetPinAlpha(&output, newLinkPin, app);
    
    ed::PinState inputState = (inputAlpha < 1.0f) ? ed::PinState::Deactivated : ed::PinState::Normal;
    ed::PinState outputState = (outputAlpha < 1.0f) ? ed::PinState::Deactivated : ed::PinState::Normal;
    
    // Input pin at top center
    ImRect inputRect = ed::PinEx(input.ID, ed::PinKind::Input, ed::PinEdge::Top, 
                                  0.5f, styleManager.ParameterPinEdgeOffset, nodeRect, inputState);
    input.LastPivotPosition = ImVec2(inputRect.GetCenter().x, inputRect.Min.y);
    input.LastRenderBounds = inputRect;
    input.HasPositionData = true;
    
    // Output pin at bottom center
    ImRect outputRect = ed::PinEx(output.ID, ed::PinKind::Output, ed::PinEdge::Bottom, 
                                   0.5f, styleManager.ParameterPinEdgeOffset, nodeRect, outputState);
    output.LastPivotPosition = ImVec2(outputRect.GetCenter().x, outputRect.Max.y);
    output.LastRenderBounds = outputRect;
    output.HasPositionData = true;
    
    // Restore cursor
    ImGui::SetCursorScreenPos(contentEndPos);
    
    ImGui::PopID();
    ed::EndNode();
}

void ParameterNode::RenderNameAndValue(Node& node, App* app, Pin* newLinkPin)
{
    // Get styles from StyleManager
    auto& styleManager = app->GetStyleManager();
    auto& paramStyle = styleManager.ParameterStyle;
    
    // Use dedicated ParameterStyle background color (visually distinct from blocks)
    ImColor bgColor = paramStyle.BgColor;
    ImColor borderColor = paramStyle.BorderColor;
    float borderWidth = styleManager.ParamBorderWidthNameAndValue;
    
    if (m_IsSource)
    {
        borderColor = styleManager.ParamBorderColorSource;
        borderWidth = styleManager.ParamBorderWidthSourceNameAndValue;
    }
    else if (m_SourceID > 0)
    {
        // Shortcut node: dimmed background
        bgColor = ImColor(bgColor.Value.x * 0.7f, bgColor.Value.y * 0.7f, bgColor.Value.z * 0.7f, bgColor.Value.w);
        borderColor = styleManager.ParamBorderColorShortcut;
    }
    
    NodeStyleScope style(
        bgColor,
        borderColor,
        paramStyle.Rounding, borderWidth,
        styleManager.ParamPaddingNameAndValue,
        ImVec2(0.0f, 1.0f),
        ImVec2(0.0f, -1.0f)
    );
    
    ed::BeginNode(node.ID);
    ImGui::PushID(node.ID.AsPointer());
    ImGui::BeginVertical("param_node");
    
    // Editable name
    ImGui::BeginHorizontal("param_name");
    ImGui::PushItemWidth(styleManager.ParamInputWidthNameAndValue);
    char nameBuffer[64];
    strncpy(nameBuffer, node.Name.c_str(), 63);
    nameBuffer[63] = '\0';
    
    static ed::NodeId editingNameNode = 0;
    if (ImGui::InputText("##name", nameBuffer, 64))
    {
        node.Name = nameBuffer;
        SetName(nameBuffer);
        
        if (m_IsSource)
            SyncNameToAllShortcuts(node, app);
    }
    
    HandleTextInput(ImGui::IsItemActive(), node.ID, editingNameNode);
    
    ImGui::PopItemWidth();
    ImGui::EndHorizontal();
    
    // Value editor
    ImGui::BeginHorizontal("param_value");
    ImGui::PushItemWidth(styleManager.ParamInputWidthNameAndValue);
    
    static ed::NodeId editingValueNode = 0;
    bool wasEditing = false;
    
    switch (node.ParameterType)
    {
        case PinType::Bool:
            if (ImGui::Checkbox("##value", &node.BoolValue))
            {
                m_BoolValue = node.BoolValue;
                if (m_SourceID > 0)
                    SyncValueToSource(app);
                else if (m_IsSource)
                    SyncValueToAllShortcuts(node, app);
            }
            break;
        case PinType::Int:
            if (ImGui::DragInt("##value", &node.IntValue, 1.0f))
            {
                m_IntValue = node.IntValue;
                if (m_SourceID > 0)
                    SyncValueToSource(app);
                else if (m_IsSource)
                    SyncValueToAllShortcuts(node, app);
            }
            wasEditing = ImGui::IsItemActive();
            break;
        case PinType::Float:
            if (ImGui::DragFloat("##value", &node.FloatValue, 0.01f))
            {
                m_FloatValue = node.FloatValue;
                if (m_SourceID > 0)
                    SyncValueToSource(app);
                else if (m_IsSource)
                    SyncValueToAllShortcuts(node, app);
            }
            wasEditing = ImGui::IsItemActive();
            break;
        case PinType::String:
        {
            char strBuffer[256];
            strncpy(strBuffer, node.StringValue.c_str(), 255);
            strBuffer[255] = '\0';
            if (ImGui::InputText("##value", strBuffer, 256))
            {
                node.StringValue = strBuffer;
                m_StringValue = strBuffer;
                if (m_SourceID > 0)
                    SyncValueToSource(app);
                else if (m_IsSource)
                    SyncValueToAllShortcuts(node, app);
            }
            wasEditing = ImGui::IsItemActive();
            break;
        }
        default:
            ImGui::Text("Unknown");
            break;
    }
    
    HandleTextInput(wasEditing, node.ID, editingValueNode);
    
    ImGui::PopItemWidth();
    ImGui::EndHorizontal();
    
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
    
    // Place pins at top/bottom edges using NodeEx
    auto& input = node.Inputs[0];
    auto& output = node.Outputs[0];
    
    float inputAlpha = GetPinAlpha(&input, newLinkPin, app);
    float outputAlpha = GetPinAlpha(&output, newLinkPin, app);
    
    ed::PinState inputState = (inputAlpha < 1.0f) ? ed::PinState::Deactivated : ed::PinState::Normal;
    ed::PinState outputState = (outputAlpha < 1.0f) ? ed::PinState::Deactivated : ed::PinState::Normal;
    
    // Input pin at top center
    ImRect inputRect = ed::PinEx(input.ID, ed::PinKind::Input, ed::PinEdge::Top, 
                                  0.5f, styleManager.ParameterPinEdgeOffset, nodeRect, inputState);
    input.LastPivotPosition = ImVec2(inputRect.GetCenter().x, inputRect.Min.y);
    input.LastRenderBounds = inputRect;
    input.HasPositionData = true;
    
    // Output pin at bottom center
    ImRect outputRect = ed::PinEx(output.ID, ed::PinKind::Output, ed::PinEdge::Bottom, 
                                   0.5f, styleManager.ParameterPinEdgeOffset, nodeRect, outputState);
    output.LastPivotPosition = ImVec2(outputRect.GetCenter().x, outputRect.Max.y);
    output.LastRenderBounds = outputRect;
    output.HasPositionData = true;
    
    // Restore cursor
    ImGui::SetCursorScreenPos(contentEndPos);
    
    ImGui::PopID();
    ed::EndNode();
}

void ParameterNode::RenderSmallBox(Node& node, App* app, Pin* newLinkPin)
{
    // Get styles from StyleManager
    auto& styleManager = app->GetStyleManager();
    auto& paramStyle = styleManager.ParameterStyle;
    
    // Use dedicated ParameterStyle background color (visually distinct from blocks)
    ImColor bgColor = paramStyle.BgColor;
    ImColor borderColor = paramStyle.BorderColor;
    float borderWidth = paramStyle.BorderWidth;
    
    if (m_IsSource)
    {
        borderColor = styleManager.ParamBorderColorSource;
        borderWidth = styleManager.ParamBorderWidthSource;
    }
    else if (m_SourceID > 0)
    {
        // Shortcut node: dimmed background
        bgColor = ImColor(bgColor.Value.x * 0.7f, bgColor.Value.y * 0.7f, bgColor.Value.z * 0.7f, bgColor.Value.w);
        borderColor = styleManager.ParamBorderColorShortcut;
    }
    
    NodeStyleScope style(
        bgColor,
        borderColor,
        paramStyle.Rounding, borderWidth,
        styleManager.ParamPaddingSmallBox,
        ImVec2(0.0f, 1.0f),
        ImVec2(0.0f, -1.0f)
    );
    
    ed::BeginNode(node.ID);
    ImGui::PushID(node.ID.AsPointer());
    ImGui::BeginVertical("param_box");
    
    // Value editor row
    ImGui::BeginHorizontal("value_row");
    
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    std::string valueStr = GetValueString();
    
    ImGui::PushItemWidth(styleManager.ParamInputWidthSmallBox);
    char valueBuffer[32];
    strncpy(valueBuffer, valueStr.c_str(), 31);
    valueBuffer[31] = '\0';
    
    static ed::NodeId editingBoxNode = 0;
    bool edited = false;
    
    switch (node.ParameterType)
    {
        case PinType::Bool:
        {
            bool val = node.BoolValue;
            if (ImGui::Checkbox("##val", &val))
            {
                node.BoolValue = val;
                m_BoolValue = val;
                edited = true;
                if (m_SourceID > 0)
                    SyncValueToSource(app);
                else if (m_IsSource)
                    SyncValueToAllShortcuts(node, app);
            }
            break;
        }
        case PinType::Int:
            if (ImGui::DragInt("##val", &node.IntValue, 1.0f))
            {
                m_IntValue = node.IntValue;
                edited = true;
                if (m_SourceID > 0)
                    SyncValueToSource(app);
                else if (m_IsSource)
                    SyncValueToAllShortcuts(node, app);
            }
            break;
        case PinType::Float:
            if (ImGui::DragFloat("##val", &node.FloatValue, 0.01f, 0.0f, 0.0f, "%.1f"))
            {
                m_FloatValue = node.FloatValue;
                edited = true;
                if (m_SourceID > 0)
                    SyncValueToSource(app);
                else if (m_IsSource)
                    SyncValueToAllShortcuts(node, app);
            }
            break;
        default:
            ImGui::TextUnformatted(valueBuffer);
            break;
    }
    
    HandleTextInput(ImGui::IsItemActive() || edited, node.ID, editingBoxNode);
    
    ImGui::PopItemWidth();
    ImGui::PopStyleColor();
    ImGui::EndHorizontal();
    
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
    
    // Place pins at top/bottom edges using NodeEx
    auto& input = node.Inputs[0];
    auto& output = node.Outputs[0];
    
    float inputAlpha = GetPinAlpha(&input, newLinkPin, app);
    float outputAlpha = GetPinAlpha(&output, newLinkPin, app);
    
    ed::PinState inputState = (inputAlpha < 1.0f) ? ed::PinState::Deactivated : ed::PinState::Normal;
    ed::PinState outputState = (outputAlpha < 1.0f) ? ed::PinState::Deactivated : ed::PinState::Normal;
    
    // Input pin at top center
    ImRect inputRect = ed::PinEx(input.ID, ed::PinKind::Input, ed::PinEdge::Top, 
                                  0.5f, styleManager.ParameterPinEdgeOffset, nodeRect, inputState);
    input.LastPivotPosition = ImVec2(inputRect.GetCenter().x, inputRect.Min.y);
    input.LastRenderBounds = inputRect;
    input.HasPositionData = true;
    
    // Output pin at bottom center
    ImRect outputRect = ed::PinEx(output.ID, ed::PinKind::Output, ed::PinEdge::Bottom, 
                                   0.5f, styleManager.ParameterPinEdgeOffset, nodeRect, outputState);
    output.LastPivotPosition = ImVec2(outputRect.GetCenter().x, outputRect.Max.y);
    output.LastRenderBounds = outputRect;
    output.HasPositionData = true;
    
    // Restore cursor
    ImGui::SetCursorScreenPos(contentEndPos);
    
    ImGui::PopID();
    ed::EndNode();
}

void ParameterNode::RenderMinimal(Node& node, App* app, Pin* newLinkPin)
{
    // Get styles from StyleManager
    auto& styleManager = app->GetStyleManager();
    auto& paramStyle = styleManager.ParameterStyle;
    
    // Use dedicated ParameterStyle background color (visually distinct from blocks)
    ImColor bgColor = paramStyle.BgColor;
    ImColor borderColor = paramStyle.BorderColor;
    float borderWidth = paramStyle.BorderWidth;
    
    if (m_IsSource)
    {
        borderColor = styleManager.ParamBorderColorSource;
        borderWidth = styleManager.ParamBorderWidthSource;
    }
    else if (m_SourceID > 0)
    {
        // Shortcut node: dimmed background
        bgColor = ImColor(bgColor.Value.x * 0.7f, bgColor.Value.y * 0.7f, bgColor.Value.z * 0.7f, bgColor.Value.w);
        borderColor = styleManager.ParamBorderColorShortcut;
    }
    
    NodeStyleScope style(
        bgColor,
        borderColor,
        paramStyle.Rounding, borderWidth,
        styleManager.ParamPaddingMinimal,
        ImVec2(0.0f, 0.0f),
        ImVec2(0.0f, 0.0f)
    );
    
    ed::BeginNode(node.ID);
    ImGui::PushID(node.ID.AsPointer());
    ImGui::BeginVertical("param_minimal");
    
    // Just a minimal rectangle - fixed size, no name, no pins, no links
    ImGui::Dummy(styleManager.ParamMinimalSize);
    
    ImGui::EndVertical();
    ImGui::PopID();
    ed::EndNode();
    // Note: Pins are NOT rendered in Minimal mode
}

void ParameterNode::RenderMinimalLinks(Node& node, App* app, Pin* newLinkPin)
{
    // Get styles from StyleManager
    auto& styleManager = app->GetStyleManager();
    auto& paramStyle = styleManager.ParameterStyle;
    
    // Use dedicated ParameterStyle background color (visually distinct from blocks)
    ImColor bgColor = paramStyle.BgColor;
    ImColor borderColor = paramStyle.BorderColor;
    float borderWidth = paramStyle.BorderWidth;
    
    if (m_IsSource)
    {
        borderColor = styleManager.ParamBorderColorSource;
        borderWidth = styleManager.ParamBorderWidthSource;
    }
    else if (m_SourceID > 0)
    {
        // Shortcut node: dimmed background
        bgColor = ImColor(bgColor.Value.x * 0.7f, bgColor.Value.y * 0.7f, bgColor.Value.z * 0.7f, bgColor.Value.w);
        borderColor = styleManager.ParamBorderColorShortcut;
    }
    
    NodeStyleScope style(
        bgColor,
        borderColor,
        paramStyle.Rounding, borderWidth,
        styleManager.ParamPaddingMinimal,
        ImVec2(0.0f, 1.0f),
        ImVec2(0.0f, -1.0f)
    );
    
    ed::BeginNode(node.ID);
    ImGui::PushID(node.ID.AsPointer());
    ImGui::BeginVertical("param_minimal_links");
    
    // Minimal content - just a small rectangle, no name
    ImGui::Dummy(styleManager.ParamMinimalLinksSize);
    
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
    
    // Place pins at top/bottom edges using NodeEx
    auto& input = node.Inputs[0];
    auto& output = node.Outputs[0];
    
    float inputAlpha = GetPinAlpha(&input, newLinkPin, app);
    float outputAlpha = GetPinAlpha(&output, newLinkPin, app);
    
    ed::PinState inputState = (inputAlpha < 1.0f) ? ed::PinState::Deactivated : ed::PinState::Normal;
    ed::PinState outputState = (outputAlpha < 1.0f) ? ed::PinState::Deactivated : ed::PinState::Normal;
    
    // Input pin at top center (smaller size for minimal mode)
    ImRect inputRect = ed::PinEx(input.ID, ed::PinKind::Input, ed::PinEdge::Top, 
                                  0.5f, styleManager.ParameterPinEdgeOffset, nodeRect, inputState);
    input.LastPivotPosition = ImVec2(inputRect.GetCenter().x, inputRect.Min.y);
    input.LastRenderBounds = inputRect;
    input.HasPositionData = true;
    
    // Output pin at bottom center (smaller size for minimal mode)
    ImRect outputRect = ed::PinEx(output.ID, ed::PinKind::Output, ed::PinEdge::Bottom, 
                                   0.5f, styleManager.ParameterPinEdgeOffset, nodeRect, outputState);
    output.LastPivotPosition = ImVec2(outputRect.GetCenter().x, outputRect.Max.y);
    output.LastRenderBounds = outputRect;
    output.HasPositionData = true;
    
    // Restore cursor
    ImGui::SetCursorScreenPos(contentEndPos);
    
    ImGui::PopID();
    ed::EndNode();
}

void ParameterNode::OnMenu(Node& node, App* app)
{
    ImGui::Separator();
    
    auto mode = GetDisplayMode();
    const char* modeStr = "Unknown";
    if (mode == ParameterDisplayMode::NameOnly) modeStr = "Name Only";
    else if (mode == ParameterDisplayMode::NameAndValue) modeStr = "Name + Value";
    else if (mode == ParameterDisplayMode::SmallBox) modeStr = "Small Box";
    else if (mode == ParameterDisplayMode::Minimal) modeStr = "Minimal";
    else if (mode == ParameterDisplayMode::MinimalLinks) modeStr = "Minimal Links";
    
    ImGui::Text("Display: %s", modeStr);
    
    if (ImGui::MenuItem("Cycle Display Mode (Space)"))
    {
        CycleDisplayMode();
        // Notify editor that display mode changed (triggers link auto-adjustment)
        ed::NotifyBlockDisplayModeChanged(node.ID);
    }
    
    if (ImGui::MenuItem("Run (R)"))
    {
        int result = Run(node, app);
    }
    
    // Source/Shortcut management
    ImGui::Separator();
    
    // Only allow marking as source if not already a shortcut
    if (m_SourceID == 0)
    {
        bool isSource = m_IsSource;
        if (ImGui::MenuItem("As Source", nullptr, &isSource))
        {
            SetIsSource(isSource);
            // If unmarking as source, ensure SourceID is cleared
            if (!isSource)
            {
                SetSourceID(0);
            }
        }
        
        // Show "Create Shortcut" option only if this node is a source
        if (m_IsSource)
        {
            if (ImGui::MenuItem("Create Shortcut"))
            {
                Node* shortcut = CreateShortcut(node, app);
                if (shortcut)
                {
                    printf("Shortcut created successfully\n");
                }
                else
                {
                    printf("Failed to create shortcut\n");
                }
            }
        }
    }
    else
    {
        // This is a shortcut - show info about source
        ImGui::Text("Shortcut to source: %d", m_SourceID);
    }
}

void ParameterNode::SaveState(Node& node, crude_json::value& nodeData, const Container* container, App* app)
{
    // Parameter node saves its own state
    nodeData["node_type"] = "parameter";
    nodeData["param_type"] = (double)static_cast<int>(m_Type);
    
    // Save display mode
    nodeData["display_mode"] = (double)static_cast<int>(m_DisplayMode);
    
    // Save source/shortcut state
    nodeData["is_source"] = m_IsSource;
    nodeData["source_id"] = (double)m_SourceID;
    
    // Check if input pin is connected - only save value if input is NOT connected
    bool shouldSaveValue = true;
    
    if (app)
    {
        // Find the input pin (parameter nodes have one input pin)
        for (const auto& pin : node.Inputs)
        {
            // Skip flow pins - only check parameter input pins
            if (pin.Type == PinType::Flow)
                continue;
            
            // Check if this input pin is connected
            auto* link = app->FindLinkConnectedToPin(pin.ID);
            bool isConnected = (link != nullptr && link->EndPinID == pin.ID);
            
            // If input is connected, don't save the local value (it comes from the connection)
            if (isConnected)
            {
                shouldSaveValue = false;
                break;
            }
        }
    }
    
    // Save value only if input is not connected
    if (shouldSaveValue)
    {
        switch (m_Type)
        {
            case PinType::Bool:   nodeData["value"] = m_BoolValue; break;
            case PinType::Int:    nodeData["value"] = (double)m_IntValue; break;
            case PinType::Float:  nodeData["value"] = (double)m_FloatValue; break;
            case PinType::String: nodeData["value"] = m_StringValue; break;
            default: break;
        }
    }
    
    // Note: container and app parameters are available for subclasses that need them
    (void)container;
}

void ParameterNode::LoadState(Node& node, const crude_json::value& nodeData, Container* container, App* app)
{
    // Load source/shortcut state
    if (nodeData.contains("is_source"))
    {
        m_IsSource = nodeData["is_source"].get<bool>();
    }
    
    if (nodeData.contains("source_id"))
    {
        int sourceId = (int)nodeData["source_id"].get<double>();
        
        // Validate source node exists
        if (sourceId > 0 && app)
        {
            Node* sourceNode = app->FindNode(ed::NodeId(sourceId));
            if (sourceNode && sourceNode->ParameterInstance)
            {
                // Source exists - set as shortcut
                m_SourceID = sourceId;
                m_IsSource = false;  // Shortcuts are not sources
            }
            else
            {
                // Source doesn't exist - orphaned shortcut, clear reference
                m_SourceID = 0;
                m_IsSource = false;
            }
        }
        else if (sourceId == 0)
        {
            // Not a shortcut
            m_SourceID = 0;
        }
    }
}

Node* ParameterNode::CreateShortcut(Node& sourceNode, App* app)
{
    if (!app)
        return nullptr;
    
    // Get source node's position
    ImVec2 sourcePos = ed::GetNodePosition(sourceNode.ID);
    
    // Calculate offset position (200px to the right)
    const float offsetX = 200.0f;
    ImVec2 shortcutPos(sourcePos.x + offsetX, sourcePos.y);
    
    // Create new parameter node with same type and display mode
    Node* shortcutNode = app->SpawnParameterNode(m_Type, -1, m_DisplayMode);
    if (!shortcutNode)
    {
        printf("[SHORTCUT] Failed to create shortcut node\n");
        return nullptr;
    }
    
    // Configure shortcut node
    if (shortcutNode->ParameterInstance)
    {
        // Set as shortcut (not source)
        shortcutNode->ParameterInstance->SetIsSource(false);
        shortcutNode->ParameterInstance->SetSourceID(m_ID);  // Reference to source
        
        // Copy current value from source
        switch (m_Type)
        {
            case PinType::Bool:
                shortcutNode->ParameterInstance->SetBool(m_BoolValue);
                shortcutNode->BoolValue = m_BoolValue;
                break;
            case PinType::Int:
                shortcutNode->ParameterInstance->SetInt(m_IntValue);
                shortcutNode->IntValue = m_IntValue;
                break;
            case PinType::Float:
                shortcutNode->ParameterInstance->SetFloat(m_FloatValue);
                shortcutNode->FloatValue = m_FloatValue;
                break;
            case PinType::String:
                shortcutNode->ParameterInstance->SetString(m_StringValue);
                shortcutNode->StringValue = m_StringValue;
                break;
            default:
                break;
        }
        
        // Copy name from source (no suffix)
        shortcutNode->ParameterInstance->SetName(m_Name.c_str());
        shortcutNode->Name = m_Name;
    }
    
    // Position the shortcut node
    ed::SetNodePosition(shortcutNode->ID, shortcutPos);
    
    printf("[SHORTCUT] Created shortcut node %d for source %d at (%.1f, %.1f)\n",
           ToRuntimeId(shortcutNode->ID), m_ID, shortcutPos.x, shortcutPos.y);
    
    return shortcutNode;
}

ParameterNode* ParameterRegistry::CreateParameter(PinType type, int id, NH_CSTRING name)
{
    NH_CSTRING defaultName = nullptr;
    switch (type)
    {
        case PinType::Bool:   defaultName = "Bool"; break;
        case PinType::Int:    defaultName = "Int"; break;
        case PinType::Float:  defaultName = "Float"; break;
        case PinType::String: defaultName = "String"; break;
        default: return nullptr;
    }
    
    if (!name)
        name = defaultName;
    
    return new ParameterNode(id, name, type);
}

