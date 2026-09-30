#define IMGUI_DEFINE_MATH_OPERATORS
#include "pin_renderer.h"
#include "../app.h"
#include "../utilities/node_renderer_base.h"
#include <imgui_internal.h>

//==============================================================================
// ParameterPinRenderer Implementation
//==============================================================================

void ParameterPinRenderer::Render(
    ed::PinId pinId,
    ed::PinKind kind,
    const Pin& pin,
    bool isLinked,
    App* app,
    const Config* overrideConfig)
{
    const Config& config = overrideConfig ? *overrideConfig : m_Config;
    
    // Start vertical layout for this pin
    ImGui::BeginVertical(pinId.AsPointer());
    
    // Calculate layout BEFORE rendering
    ImVec2 startCursor = ImGui::GetCursorScreenPos();
    Layout layout = CalculateLayout(
        kind,
        config.showLabel ? pin.Name.c_str() : nullptr,
        config.iconSize,
        config.showLabel,
        startCursor);
    
    // For INPUT pins: Icon at TOP, label below
    // For OUTPUT pins: Label at TOP (optional), icon at bottom
    if (kind == ed::PinKind::Input)
    {
        // Draw icon first (at top)
        ImGui::SetCursorScreenPos(layout.iconRect.Min);
        DrawIcon(pin, isLinked, (int)(m_Alpha * 255), layout.iconRect, app);
        
        // Draw label below icon (if showing)
        if (config.showLabel)
        {
            ImGui::SetCursorScreenPos(layout.labelRect.Min);
            ImGui::BeginHorizontal("label");
            ImGui::Spring(1, 0);
            ImGui::TextUnformatted(pin.Name.c_str());
            ImGui::Spring(1, 0);
            ImGui::EndHorizontal();
        }
    }
    else // Output
    {
        // Draw label first (if showing)
        if (config.showLabel)
        {
            ImGui::SetCursorScreenPos(layout.labelRect.Min);
            ImGui::BeginHorizontal("label");
            ImGui::Spring(1, 0);
            ImGui::TextUnformatted(pin.Name.c_str());
            ImGui::Spring(1, 0);
            ImGui::EndHorizontal();
        }
        
        // Draw icon at bottom
        ImGui::SetCursorScreenPos(layout.iconRect.Min);
		// layout.iconRect.Min.y -= 30; // slight nudge for better vertical alignment
        DrawIcon(pin, isLinked, (int)(m_Alpha * 255), layout.iconRect, app);
    }
    
    // Register pin with editor
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, m_Alpha);
    BeginPin(pinId, kind);
    
    // Set pin rect and pivot
    ed::PinRect(layout.combinedRect.Min, layout.combinedRect.Max);
    ed::PinPivotRect(layout.pivotPoint, layout.pivotPoint);
    
    EndPin();
    ImGui::PopStyleVar();
    
    // Store layout for queries
    m_LastPivotPosition = layout.pivotPoint;
    m_LastRenderBounds = layout.combinedRect;
    m_RelativeOffset = layout.pivotPoint - startCursor;
    
    // Tooltip: Show parameter name and value
    if (ImGui::IsItemHovered())
    {
        std::string tooltipText = pin.Name.empty() ? "<unnamed>" : pin.Name;
        
        // Get value based on connection status
        std::string valueStr;
        auto* link = app->FindLinkConnectedToPin(pin.ID);
        bool isLinked = (link != nullptr && link->EndPinID == pin.ID);
        
        if (isLinked)
        {
            // Connected: get value from source parameter node
            auto* sourcePin = app->FindPin(link->StartPinID);
            if (sourcePin && sourcePin->Node && sourcePin->Node->Type == NodeType::Parameter)
            {
                Node* paramNode = sourcePin->Node;
                switch (paramNode->ParameterType)
                {
                    case PinType::Bool:
                        valueStr = paramNode->BoolValue ? "true" : "false";
                        break;
                    case PinType::Int:
                        {
                            char buf[32];
                            snprintf(buf, sizeof(buf), "%d", paramNode->IntValue);
                            valueStr = buf;
                        }
                        break;
                    case PinType::Float:
                        {
                            char buf[32];
                            snprintf(buf, sizeof(buf), "%.3f", paramNode->FloatValue);
                            valueStr = buf;
                        }
                        break;
                    case PinType::String:
                        valueStr = paramNode->StringValue;
                        break;
                    default:
                        valueStr = "?";
                        break;
                }
            }
            else
            {
                valueStr = "[Connected]";
            }
        }
        else
        {
            // Unconnected: get default value from node
            if (pin.Node)
            {
                const int pinId = ToRuntimeId(pin.ID);
                auto& paramValues = pin.Node->UnconnectedParamValues;
                if (paramValues.find(pinId) != paramValues.end())
                {
                    valueStr = paramValues[pinId];
                }
                else
                {
                    // Default value based on type
                    switch (pin.Type)
                    {
                        case PinType::Bool:   valueStr = "false"; break;
                        case PinType::Int:    valueStr = "0"; break;
                        case PinType::Float:  valueStr = "0.0"; break;
                        case PinType::String: valueStr = ""; break;
                        default: valueStr = "?"; break;
                    }
                }
            }
            else
            {
                valueStr = "[Not connected]";
            }
        }
        
        // Format tooltip: "Name: Value"
        char tooltip[512];
        if (!valueStr.empty())
        {
            snprintf(tooltip, sizeof(tooltip), "%s\nValue: %s", tooltipText.c_str(), valueStr.c_str());
        }
        else
        {
            snprintf(tooltip, sizeof(tooltip), "%s", tooltipText.c_str());
        }
        
        // Use deferred tooltip system (set hovered pin and tooltip text)
        app->m_HoveredPin = const_cast<Pin*>(&pin);
        app->m_HoveredPinTooltip = tooltip;
    }
    
    ImGui::EndVertical();
}

ParameterPinRenderer::Layout ParameterPinRenderer::CalculateLayout(
    ed::PinKind kind,
    const char* labelText,
    float iconSize,
    bool showLabel,
    const ImVec2& cursorPos)
{
    Layout layout;
    
    ImVec2 iconSizeVec(iconSize, iconSize);
    float labelHeight = showLabel ? ImGui::GetTextLineHeight() : 0.0f;
    float spacing = showLabel ? m_Config.labelSpacing : 0.0f;
    
    if (kind == ed::PinKind::Input)
    {
        // INPUT: Icon at top, label below
        // Pivot at TOP edge of icon (links come from above)
        
        layout.iconRect = ImRect(cursorPos, cursorPos + iconSizeVec);
        
        if (showLabel)
        {
            ImVec2 labelStart = ImVec2(cursorPos.x, cursorPos.y + iconSize + spacing);
            float labelWidth = ImGui::CalcTextSize(labelText).x;
            layout.labelRect = ImRect(
                labelStart,
                labelStart + ImVec2(labelWidth, labelHeight));
            
            layout.combinedRect = layout.iconRect;
            layout.combinedRect.Add(layout.labelRect);
        }
        else
        {
            layout.combinedRect = layout.iconRect;
            layout.labelRect = ImRect(); // Empty
        }
        
        // Pivot at TOP CENTER of icon (where link connects)
        layout.pivotPoint = ImVec2( layout.iconRect.GetCenter().x, layout.iconRect.Min.y);
    }
    else // Output
    {
        // OUTPUT: Label at top (optional), icon at bottom
        // Pivot at BOTTOM edge of icon (links go below)
        
        ImVec2 iconStart = cursorPos;
        if (showLabel)
        {
            // Place icon below label
            layout.labelRect = ImRect(
                cursorPos,
                cursorPos + ImVec2(ImGui::CalcTextSize(labelText).x, labelHeight));
            
            iconStart = ImVec2(cursorPos.x, cursorPos.y + labelHeight + spacing);
        }
        else
        {
            layout.labelRect = ImRect(); // Empty
        }
        
        layout.iconRect = ImRect(iconStart, iconStart + iconSizeVec);
        
        layout.combinedRect = layout.iconRect;
        if (showLabel)
            layout.combinedRect.Add(layout.labelRect);
        
        // Pivot at BOTTOM CENTER of icon (where link connects)
        layout.pivotPoint = ImVec2(
            layout.iconRect.GetCenter().x,
            layout.iconRect.Max.y);
    }
    
    // Apply edge offset
    layout.iconRect.Translate(m_Config.edgeOffset);
    layout.labelRect.Translate(m_Config.edgeOffset);
    layout.combinedRect.Translate(m_Config.edgeOffset);
    layout.pivotPoint += m_Config.edgeOffset;
    
    return layout;
}

void ParameterPinRenderer::DrawIcon(
    const Pin& pin,
    bool isLinked,
    int alpha,
    const ImRect& iconRect,
    App* app)
{
    // Set cursor to icon position, then use config offset for fine-tuning
    ImGui::SetCursorScreenPos(iconRect.Min);
    // ImVec2 cursorPos = ImGui::GetCursorScreenPos();
    
    // Use NodeRendererBase::DrawPinIcon with offset from config
    ax::NodeRendering::NodeRendererBase::DrawPinIcon(pin, isLinked, alpha, m_Config.iconOffset, app);
}

void ParameterPinRenderer::BeginPin(ed::PinId id, ed::PinKind kind)
{
    m_IsActive = true;
    ed::BeginPin(id, kind);
    
    // Set link direction based on kind
    if (kind == ed::PinKind::Input)
    {
        // Links come from above (vertical, negative Y)
        ed::PushStyleVar(ed::StyleVar_TargetDirection, ImVec2(0.0f, -1.0f));
    }
    else
    {
        // Links go below (vertical, positive Y)
        ed::PushStyleVar(ed::StyleVar_SourceDirection, ImVec2(0.0f, 1.0f));
    }
}

void ParameterPinRenderer::EndPin()
{
    if (m_IsActive)
    {
        ed::PopStyleVar(); // Pop direction
        ed::EndPin();
        m_IsActive = false;
    }
}

//==============================================================================
// FlowPinRenderer Implementation
//==============================================================================

void FlowPinRenderer::Render(
    ed::PinId pinId,
    ed::PinKind kind,
    const Pin& pin,
    App* app,
    const Config* overrideConfig)
{
    const Config& config = overrideConfig ? *overrideConfig : m_Config;
    
    // Create dummy to reserve space (editor will use this as base position)
    ImVec2 dummyPos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(config.pinSize, config.pinSize));
    
    // Calculate layout with offset
    Layout layout = CalculateLayout(kind, config.pinSize, config.edgeOffset, dummyPos);
    
    // Register pin with editor
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, m_Alpha);
    BeginPin(pinId, kind);
    
    // Set pin rect to visual position (where we actually draw)
    ed::PinRect(layout.visualRect.Min, layout.visualRect.Max);
    ed::PinPivotRect(layout.pivotPoint, layout.pivotPoint);
    
    EndPin();
    ImGui::PopStyleVar();
    
    // Draw the visual square
    ImU32 fillColor = IM_COL32(
        (config.fillColor >> IM_COL32_R_SHIFT) & 0xFF,
        (config.fillColor >> IM_COL32_G_SHIFT) & 0xFF,
        (config.fillColor >> IM_COL32_B_SHIFT) & 0xFF,
        (int)(m_Alpha * 255));
    
    ImU32 borderColor = IM_COL32(
        (config.borderColor >> IM_COL32_R_SHIFT) & 0xFF,
        (config.borderColor >> IM_COL32_G_SHIFT) & 0xFF,
        (config.borderColor >> IM_COL32_B_SHIFT) & 0xFF,
        (int)(m_Alpha * 200));
    
    DrawSquare(layout.visualRect, config.rounding, config.borderWidth, 
               fillColor, borderColor, (int)(m_Alpha * 255));
    
    // Store layout for queries
    m_LastPivotPosition = layout.pivotPoint;
    m_LastRenderBounds = layout.visualRect;
    m_RelativeOffset = layout.pivotPoint - dummyPos;
    
    // Tooltip
    if (ImGui::IsItemHovered())
    {
        char tooltip[256];
        snprintf(tooltip, sizeof(tooltip), "Flow %s: %s\n(Execution trigger)",
                kind == ed::PinKind::Input ? "Input" : "Output",
                pin.Name.c_str());
        ImGui::SetTooltip("%s", tooltip);
    }
}

FlowPinRenderer::Layout FlowPinRenderer::CalculateLayout(
    ed::PinKind kind,
    float pinSize,
    float edgeOffset,
    const ImVec2& dummyPos)
{
    Layout layout;
    
    // Start with dummy rect
    ImRect baseRect(dummyPos, dummyPos + ImVec2(pinSize, pinSize));
    
    // Translate based on kind
    if (kind == ed::PinKind::Input)
    {
        // Move LEFT (negative X)
        layout.visualRect = baseRect;
        layout.visualRect.TranslateX(-edgeOffset);
    }
    else // Output
    {
        // Move RIGHT (positive X)
        layout.visualRect = baseRect;
        layout.visualRect.TranslateX(edgeOffset);
    }
    
    // Pivot at center of visual rect
    layout.pivotPoint = layout.visualRect.GetCenter();
    
    return layout;
}

void FlowPinRenderer::DrawSquare(
    const ImRect& rect,
    float rounding,
    float borderWidth,
    ImU32 fillColor,
    ImU32 borderColor,
    int alpha)
{
    auto drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(rect.Min, rect.Max, fillColor, rounding);
    drawList->AddRect(rect.Min, rect.Max, borderColor, rounding, 0, borderWidth);
}

void FlowPinRenderer::BeginPin(ed::PinId id, ed::PinKind kind)
{
    m_IsActive = true;
    ed::BeginPin(id, kind);
    
    // Set link direction based on kind
    if (kind == ed::PinKind::Input)
    {
        // Links come from left (horizontal, negative X)
        ed::PushStyleVar(ed::StyleVar_TargetDirection, ImVec2(-1.0f, 0.0f));
    }
    else
    {
        // Links go right (horizontal, positive X)
        ed::PushStyleVar(ed::StyleVar_SourceDirection, ImVec2(1.0f, 0.0f));
    }
}

void FlowPinRenderer::EndPin()
{
    if (m_IsActive)
    {
        ed::PopStyleVar(); // Pop direction
        ed::EndPin();
        m_IsActive = false;
    }
}

