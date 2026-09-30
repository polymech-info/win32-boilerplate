#pragma once
#include "../types.h"
#include <imgui.h>

// Forward declarations
class App;

namespace ax {
namespace NodeRendering {

// Icon types (moved from drawing.h)
enum class IconType: ImU32 { Flow, Circle, Square, Grid, RoundSquare, Diamond };

// Base class for all node renderers
// Provides shared utilities for drawing pins, icons, and managing node styles
class NodeRendererBase
{
public:
    // ===== Icon Drawing =====
    // Low-level: Direct draw list drawing (moved from drawing.cpp)
    static void DrawIcon(ImDrawList* drawList, const ImVec2& a, const ImVec2& b, 
                        IconType type, bool filled, ImU32 color, ImU32 innerColor);
    
    // High-level: ImGui-friendly widget (auto cursor, visibility, layout)
    static void Icon(const ImVec2& size, IconType type, bool filled, 
                    const ImVec4& color = ImVec4(1, 1, 1, 1), 
                    const ImVec4& innerColor = ImVec4(0, 0, 0, 0));

    // ===== Pin Icon Drawing =====
    // Draw a pin icon at the current cursor position with offset
    static void DrawPinIcon(const Pin& pin, bool connected, int alpha, 
                           const ImVec2& offset, App* app);
    
protected:
    
    // ===== Style Management =====
    // RAII-style scope guard for node styles
    struct NodeStyleScope
    {
        NodeStyleScope(const ImColor& bgColor, const ImColor& borderColor,
                      float rounding, float borderWidth, const ImVec4& padding,
                      const ImVec2& sourceDir, const ImVec2& targetDir);
        ~NodeStyleScope();
        
    private:
        int m_StyleVarCount = 7;
        int m_StyleColorCount = 2;
    };
    
    // ===== Tooltip Management =====
    // Set deferred tooltip for a pin (rendered during ed::Suspend())
    static void SetPinTooltip(Pin* pin, const char* label, const char* typeName, 
                             App* app);
    
    // ===== Input Field Helpers =====
    // Handle text input shortcuts (disable node editor shortcuts while typing)
    static void HandleTextInput(bool isActive, ed::NodeId nodeId, 
                               ed::NodeId& staticEditingNode);
    
    // ===== Pin Alpha Calculation =====
    // Calculate alpha for a pin based on link compatibility
    static float GetPinAlpha(Pin* pin, Pin* newLinkPin, App* app);
};

} // namespace NodeRendering
} // namespace ax

