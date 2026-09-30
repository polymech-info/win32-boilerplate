// NodeEx.h - Extended Node Builder for Precise Pin Placement
// Provides PinEx function for placing pins at specific node edges

#pragma once

#include <imgui.h>
#include <imgui_node_editor.h>
#include <imgui_internal.h>

namespace ax {
namespace NodeEditor {

// Enum for pin edge locations
enum class PinEdge
{
    Top,
    Bottom,
    Left,
    Right
};

// Enum for pin states
enum class PinState
{
    Normal,      // Default state
    Running,     // Active/running state (e.g., green)
    Deactivated, // Disabled/inactive state (e.g., gray)
    Error,       // Error state (e.g., red)
    Warning      // Warning state (e.g., yellow)
};

// Renderer function type for custom pin rendering
// Parameters:
//   drawList: The ImGui draw list to draw to
//   center: Center position of the pin
//   pinSize: Size of the pin (ImVec2)
//   fillColor: Fill color (ImU32)
//   borderColor: Border color (ImU32)
//   state: The pin state (for state-aware rendering)
typedef void (*PinRenderer)(ImDrawList* drawList, const ImVec2& center, const ImVec2& pinSize, ImU32 fillColor, ImU32 borderColor, PinState state);

// Default renderers provided by NodeEx
void RenderPinCircle(ImDrawList* drawList, const ImVec2& center, const ImVec2& pinSize, ImU32 fillColor, ImU32 borderColor, PinState state);
void RenderPinBox(ImDrawList* drawList, const ImVec2& center, const ImVec2& pinSize, ImU32 fillColor, ImU32 borderColor, PinState state);

// Helper function to get colors based on pin kind and state
void GetPinColors(PinKind kind, PinState state, ImU32& fillColor, ImU32& borderColor);

// PinEx: Precise pin placement at node edges
// Must be called between BeginNode() and EndNode()
// 
// Parameters:
//   pinId: The pin ID
//   kind: Input or Output pin kind
//   edge: Edge location (Top, Bottom, Left, Right)
//   offset: Offset along the edge (0.0 = start, 1.0 = end, 0.5 = center)
//   direction: Distance from edge (0 = on edge, positive = outside, negative = inside)
//   nodeRect: The node's bounds rectangle (can be obtained via ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()) after content)
//   state: Pin state (Normal, Running, Deactivated, Error, Warning) - affects colors
//   tooltip: Optional tooltip text shown on hover (nullptr = no tooltip)
//   renderer: Optional custom renderer function. If nullptr, uses default (box for Top/Bottom, circle for Left/Right)
//
// Returns: The pin rectangle for reference
ImRect PinEx(
    PinId pinId,
    PinKind kind,
    PinEdge edge,
    float offset,
    float direction,
    const ImRect& nodeRect,
    PinState state = PinState::Normal,
    const char* tooltip = nullptr,
    PinRenderer renderer = nullptr);

// Overload: PinEx with nodeId (for use after EndNode, when node bounds are finalized)
// Note: This version is less precise as it relies on GetNodePosition/GetNodeSize
// For precise placement, use the nodeRect version during node building
ImRect PinEx(
    PinId pinId,
    PinKind kind,
    PinEdge edge,
    float offset,
    float direction,
    NodeId nodeId,
    PinState state = PinState::Normal,
    const char* tooltip = nullptr,
    PinRenderer renderer = nullptr);

// Helper: Process tooltips for pins (call this after EndNode, typically in a deferred section)
// This checks if any pin is hovered and displays its tooltip
// Note: Pin tooltips need to be processed in a deferred manner due to node editor coordinate space
// Call this after all nodes are built, typically in a Suspend/Resume block like widgets-example
void ProcessPinTooltips();

// Clear tooltip data (call at start of frame before building nodes)
void ClearPinTooltips();

} // namespace NodeEditor
} // namespace ax
