// NodeEx.cpp - Extended Node Builder for Precise Pin Placement
// Implementation of PinEx function

# define IMGUI_DEFINE_MATH_OPERATORS
# include "NodeEx.h"
# include <vector>
# include <map>
# include <cstring>

namespace ax {
namespace NodeEditor {

// Internal storage for pin tooltips
struct PinTooltipData
{
    PinId pinId;
    ImRect bounds;
    const char* tooltip;
};

static std::vector<PinTooltipData> s_PinTooltips;

// Helper function to get colors based on pin kind and state
void GetPinColors(PinKind kind, PinState state, ImU32& fillColor, ImU32& borderColor)
{
    borderColor = IM_COL32(0, 0, 0, 0); // No border
    
    switch (state)
    {
        case PinState::Normal:
            fillColor = kind == PinKind::Input 
                ? IM_COL32(120, 120, 150, 255)  // Muted blue for inputs
                : IM_COL32(150, 120, 120, 255);  // Muted red for outputs
            break;
        
        case PinState::Running:
            fillColor = IM_COL32(80, 160, 80, 255); // Muted green for running
            break;
        
        case PinState::Deactivated:
            fillColor = IM_COL32(80, 80, 80, 200); // Dark gray for deactivated
            break;
        
        case PinState::Error:
            fillColor = IM_COL32(200, 80, 80, 255); // Muted red for error
            break;
        
        case PinState::Warning:
            fillColor = IM_COL32(200, 160, 80, 255); // Muted orange for warning
            break;
    }
}

// Default renderer: Small rectangle for parameter pins
void RenderPinCircle(ImDrawList* drawList, const ImVec2& center, const ImVec2& pinSize, ImU32 fillColor, ImU32 borderColor, PinState state)
{
    // Small rectangle - about half the height of flow pins
    float width = 8.0f;
    float height = 4.0f;
    ImVec2 halfSize(width * 0.5f, height * 0.5f);
    ImVec2 boxMin = center - halfSize;
    ImVec2 boxMax = center + halfSize;
    
    // Simple filled rectangle, no border
    drawList->AddRectFilled(boxMin, boxMax, fillColor, 0.0f);
}

// Default renderer: Small square for flow pins
void RenderPinBox(ImDrawList* drawList, const ImVec2& center, const ImVec2& pinSize, ImU32 fillColor, ImU32 borderColor, PinState state)
{
    // Small square - 8x8 pixels
    float boxSize = 8.0f;
    ImVec2 boxHalfSize(boxSize * 0.5f, boxSize * 0.5f);
    ImVec2 boxMin = center - boxHalfSize;
    ImVec2 boxMax = center + boxHalfSize;
    
    // Simple filled square, no border, no rounding
    drawList->AddRectFilled(boxMin, boxMax, fillColor, 0.0f);
}

ImRect PinEx(
    PinId pinId,
    PinKind kind,
    PinEdge edge,
    float offset,
    float direction,
    const ImRect& nodeRect,
    PinState state,
    const char* tooltip,
    PinRenderer renderer)
{
    // Default pin size
    const ImVec2 pinSize(16.0f, 16.0f);
    ImVec2 halfSize = pinSize * 0.5f;
    
    // Calculate pin center position based on edge
    ImVec2 pinCenter;
    
    switch (edge)
    {
        case PinEdge::Top:
        {
            float x = ImLerp(nodeRect.Min.x, nodeRect.Max.x, offset);
            float y = nodeRect.Min.y - direction;
            pinCenter = ImVec2(x, y - halfSize.y);
            break;
        }
        
        case PinEdge::Bottom:
        {
            float x = ImLerp(nodeRect.Min.x, nodeRect.Max.x, offset);
            float y = nodeRect.Max.y + direction;
            pinCenter = ImVec2(x, y + halfSize.y);
            break;
        }
        
        case PinEdge::Left:
        {
            float x = nodeRect.Min.x - direction;
            float y = ImLerp(nodeRect.Min.y, nodeRect.Max.y, offset);
            pinCenter = ImVec2(x - halfSize.x, y);
            break;
        }
        
        case PinEdge::Right:
        {
            float x = nodeRect.Max.x + direction;
            float y = ImLerp(nodeRect.Min.y, nodeRect.Max.y, offset);
            pinCenter = ImVec2(x + halfSize.x, y);
            break;
        }
    }
    
    // Calculate pin rectangle
    ImRect pinRect = ImRect(
        pinCenter - halfSize,
        pinCenter + halfSize
    );
    
    // Calculate pivot point at the edge for link attachment
    ImVec2 pivotPoint;
    switch (edge)
    {
        case PinEdge::Top:
            pivotPoint = ImVec2(pinRect.GetCenter().x, pinRect.Min.y);
            break;
        
        case PinEdge::Bottom:
            pivotPoint = ImVec2(pinRect.GetCenter().x, pinRect.Max.y);
            break;
        
        case PinEdge::Left:
            pivotPoint = ImVec2(pinRect.Min.x, pinRect.GetCenter().y);
            break;
        
        case PinEdge::Right:
            pivotPoint = ImVec2(pinRect.Max.x, pinRect.GetCenter().y);
            break;
    }
    
    // Set pin direction BEFORE BeginPin (direction is captured during BeginPin)
    // This overrides the node-level style scope
    switch (edge)
    {
        case PinEdge::Top:
            // Pins on top: links flow upward (negative Y)
            if (kind == PinKind::Input)
                PushStyleVar(StyleVar_TargetDirection, ImVec2(0.0f, -1.0f));
            else
                PushStyleVar(StyleVar_SourceDirection, ImVec2(0.0f, -1.0f));
            break;
        
        case PinEdge::Bottom:
            // Pins on bottom: links flow downward (positive Y)
            if (kind == PinKind::Input)
                PushStyleVar(StyleVar_TargetDirection, ImVec2(0.0f, 1.0f));
            else
                PushStyleVar(StyleVar_SourceDirection, ImVec2(0.0f, 1.0f));
            break;
        
        case PinEdge::Left:
            // Pins on left: links flow LEFT (negative X)
            if (kind == PinKind::Input)
                PushStyleVar(StyleVar_TargetDirection, ImVec2(-1.0f, 0.0f));
            else
                PushStyleVar(StyleVar_SourceDirection, ImVec2(-1.0f, 0.0f));
            break;
        
        case PinEdge::Right:
            // Pins on right: links flow RIGHT (positive X)
            if (kind == PinKind::Input)
                PushStyleVar(StyleVar_TargetDirection, ImVec2(1.0f, 0.0f));
            else
                PushStyleVar(StyleVar_SourceDirection, ImVec2(1.0f, 0.0f));
            break;
    }
    
    // Begin pin (direction is captured here from the style we just pushed)
    BeginPin(pinId, kind);
    
    // Save current cursor position so we can restore it
    ImVec2 savedCursorPos = ImGui::GetCursorScreenPos();
    
    // Draw visual indicator using renderer function
    auto drawList = ImGui::GetWindowDrawList();
    ImVec2 center = pinRect.GetCenter();
    
    // Get colors based on pin kind and state
    ImU32 fillColor, borderColor;
    GetPinColors(kind, state, fillColor, borderColor);
    
    // Use custom renderer if provided, otherwise use default based on edge
    if (renderer != nullptr)
    {
        renderer(drawList, center, pinSize, fillColor, borderColor, state);
    }
    else
    {
        // Default: boxes for top/bottom, circles for left/right
        if (edge == PinEdge::Top || edge == PinEdge::Bottom)
        {
            RenderPinBox(drawList, center, pinSize, fillColor, borderColor, state);
        }
        else // Left or Right
        {
            RenderPinCircle(drawList, center, pinSize, fillColor, borderColor, state);
        }
    }
    
    // Set precise pin rectangle (this prevents automatic bounds resolution)
    PinRect(pinRect.Min, pinRect.Max);
    
    // Set pivot point at edge for proper link connection
    PinPivotRect(pivotPoint, pivotPoint);
    
    // Restore cursor position so we don't affect node size calculation
    ImGui::SetCursorScreenPos(savedCursorPos);
    
    // End pin (now it won't try to resolve bounds from cursor/item rect)
    EndPin();
    
    // Pop the direction style var we pushed
    PopStyleVar();
    
    // Store tooltip data if provided
    if (tooltip != nullptr && strlen(tooltip) > 0)
    {
        PinTooltipData tooltipData;
        tooltipData.pinId = pinId;
        tooltipData.bounds = pinRect;
        tooltipData.tooltip = tooltip;
        s_PinTooltips.push_back(tooltipData);
    }
    
    return pinRect;
}

// Overload: PinEx with nodeId (for use after EndNode, when node bounds are finalized)
ImRect PinEx(
    PinId pinId,
    PinKind kind,
    PinEdge edge,
    float offset,
    float direction,
    NodeId nodeId,
    PinState state,
    const char* tooltip,
    PinRenderer renderer)
{
    // Get node bounds from node ID
    ImVec2 nodePos = GetNodePosition(nodeId);
    ImVec2 nodeSize = GetNodeSize(nodeId);
    ImRect nodeRect = ImRect(nodePos, nodePos + nodeSize);
    
    // Use the main implementation
    return PinEx(pinId, kind, edge, offset, direction, nodeRect, state, tooltip, renderer);
}

// Helper: Process tooltips for pins (call this after EndNode, typically in a deferred section)
// Must be called inside Suspend() block (outside canvas coordinate space)
void ProcessPinTooltips()
{
    if (s_PinTooltips.empty())
        return;
    
    // Get mouse position in screen space (we're in Suspend, so mouse is in screen space)
    ImVec2 mouseScreenPos = ImGui::GetMousePos();
    
    // Convert screen position to canvas space
    // Pin bounds are stored in canvas space (from PinRect), so we need to convert mouse pos
    ImVec2 mouseCanvasPos = ScreenToCanvas(mouseScreenPos);
    
    // Find hovered pin
    for (const auto& tooltipData : s_PinTooltips)
    {
        // Check if mouse is within pin bounds (bounds are in canvas space)
        if (tooltipData.bounds.Contains(mouseCanvasPos))
        {
            ImGui::SetTooltip("%s", tooltipData.tooltip);
            break; // Only show one tooltip at a time
        }
    }
}

// Clear tooltip data (call at start of frame or after processing)
void ClearPinTooltips()
{
    s_PinTooltips.clear();
}

} // namespace NodeEditor
} // namespace ax
