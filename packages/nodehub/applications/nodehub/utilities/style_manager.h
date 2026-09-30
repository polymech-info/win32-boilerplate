// style_manager.h - Node style management with JSON persistence
#pragma once
#include <imgui.h>
#include <imgui_node_editor.h>
#include <string>

namespace ax {
namespace NodeEditor {

// Node type-specific styles
struct NodeTypeStyle
{
    ImColor BgColor;
    ImColor BorderColor;
    ImColor BorderColorRunning;  // For execution visualization
    float   Rounding;
    float   BorderWidth;
    float   BorderWidthRunning;
    ImVec4  Padding;
    
    NodeTypeStyle()
        : BgColor(50, 50, 60, 240)
        , BorderColor(100, 100, 110, 255)
        , BorderColorRunning(255, 0, 0, 255)
        , Rounding(1.0f)
        , BorderWidth(1.0f)
        , BorderWidthRunning(3.0f)
        , Padding(8, 8, 8, 8)
    {
    }
};

// Complete style configuration with JSON serialization
class StyleManager
{
public:
    // Node type styles
    NodeTypeStyle BlockStyle;      // Regular blocks (e.g., Math.Add, Math.Multiply)
    NodeTypeStyle GroupStyle;      // Group blocks
    NodeTypeStyle ParameterStyle;  // Parameter nodes
    
    // Pin styles
    ImColor PinColor_Input;
    ImColor PinColor_Output;
    ImColor PinColor_Running;
    ImColor PinColor_Deactivated;
    ImColor PinColor_Error;
    ImColor PinColor_Warning;
    
    // Pin sizes
    float FlowPinSize;
    float ParameterPinWidth;
    float ParameterPinHeight;
    
    // Pin edge offsets
    float ParameterPinEdgeOffset;
    float FlowPinEdgeOffset;
    
    // Layout
    float MinNodeWidth;
    float MinNodeHeight;
    float MinGroupSize;
    
    // Group-specific
    float GroupResizeGripSize;
    float GroupResizeGripLineSpacing;
    ImColor GroupResizeGripColor;
    
    // Parameter node-specific
    ImColor ParamBorderColorSource;     // Gold for source nodes
    ImColor ParamBorderColorShortcut;   // Dimmed for shortcuts
    float ParamBorderWidthSource;
    float ParamBorderWidthNameAndValue;
    float ParamBorderWidthSourceNameAndValue;
    ImVec4 ParamPaddingNameOnly;
    ImVec4 ParamPaddingNameAndValue;
    ImVec4 ParamPaddingSmallBox;
    ImVec4 ParamPaddingMinimal;
    float ParamInputWidthNameOnly;
    float ParamInputWidthNameAndValue;
    float ParamInputWidthSmallBox;
    ImVec2 ParamMinimalSize;
    ImVec2 ParamMinimalLinksSize;
    
    // Link styling
    float LinkThicknessNormal;          // Normal link thickness
    float LinkThicknessSelected;        // Selected/highlighted link thickness
    float LinkThicknessParameterNormal; // Parameter link thickness (normal)
    float LinkThicknessParameterSelected; // Parameter link thickness (selected/highlighted)
    ImColor LinkColorHighlighted;       // Highlighted link color (red for execution)
    
    // Waypoint/Control Point styling
    float WaypointRadius;               // Base radius for waypoints (scales with zoom)
    float WaypointBorderWidth;          // Border width for waypoints
    ImColor WaypointColor;              // Waypoint fill color
    ImColor WaypointBorderColor;        // Waypoint border color
    ImColor WaypointColorHovered;       // Waypoint color when hovered
    ImColor WaypointColorSelected;      // Waypoint color when selected
    float WaypointPreviewRadius;        // Radius for preview waypoints (while dragging)
    ImColor WaypointPreviewColor;       // Preview waypoint fill color
    ImColor WaypointPreviewBorderColor;  // Preview waypoint border color
    
    // Constructor with defaults
    StyleManager();
    
    // JSON serialization
    bool LoadFromFile(const std::string& filename);
    bool SaveToFile(const std::string& filename) const;
    
    // Apply to editor style (sets editor-level defaults)
    void ApplyToEditorStyle(EditorContext* editor);
    
private:
    // Helper methods for JSON conversion
    std::string Serialize() const;
    bool Deserialize(const std::string& jsonData);
};

} // namespace NodeEditor
} // namespace ax

