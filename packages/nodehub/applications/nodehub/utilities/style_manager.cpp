#include "style_manager.h"
#include <crude_json.h>
#include <imgui_node_editor_internal.h>
#include <fstream>
#include <cstdio>

namespace ax {
namespace NodeEditor {

// Macros for cleaner JSON serialization
#define SAVE_COLOR(obj, field, name) \
    obj[name]["r"] = (double)field.Value.x; \
    obj[name]["g"] = (double)field.Value.y; \
    obj[name]["b"] = (double)field.Value.z; \
    obj[name]["a"] = (double)field.Value.w

#define LOAD_COLOR(obj, field, name) \
    if (obj.contains(name) && obj[name].is_object()) { \
        auto& c = obj[name]; \
        field = ImColor( \
            c.contains("r") ? (float)c["r"].get<double>() : 1.0f, \
            c.contains("g") ? (float)c["g"].get<double>() : 1.0f, \
            c.contains("b") ? (float)c["b"].get<double>() : 1.0f, \
            c.contains("a") ? (float)c["a"].get<double>() : 1.0f); \
    }

#define SAVE_VEC4(obj, field, name) \
    obj[name]["x"] = (double)field.x; \
    obj[name]["y"] = (double)field.y; \
    obj[name]["z"] = (double)field.z; \
    obj[name]["w"] = (double)field.w

#define LOAD_VEC4(obj, field, name) \
    if (obj.contains(name) && obj[name].is_object()) { \
        auto& v = obj[name]; \
        field = ImVec4( \
            v.contains("x") ? (float)v["x"].get<double>() : 0.0f, \
            v.contains("y") ? (float)v["y"].get<double>() : 0.0f, \
            v.contains("z") ? (float)v["z"].get<double>() : 0.0f, \
            v.contains("w") ? (float)v["w"].get<double>() : 0.0f); \
    }

#define SAVE_VEC2(obj, field, name) \
    obj[name]["x"] = (double)field.x; \
    obj[name]["y"] = (double)field.y

#define LOAD_VEC2(obj, field, name) \
    if (obj.contains(name) && obj[name].is_object()) { \
        auto& v = obj[name]; \
        field = ImVec2( \
            v.contains("x") ? (float)v["x"].get<double>() : 0.0f, \
            v.contains("y") ? (float)v["y"].get<double>() : 0.0f); \
    }

#define SAVE_FLOAT(obj, field, name) obj[name] = (double)field
#define LOAD_FLOAT(obj, field, name) if (obj.contains(name)) field = (float)obj[name].get<double>()

StyleManager::StyleManager()
{
    // Initialize with clean, minimal defaults
    
    // Block style
    BlockStyle.BgColor = ImColor(50, 50, 60, 240);
    BlockStyle.BorderColor = ImColor(100, 100, 110, 255);
    BlockStyle.BorderColorRunning = ImColor(255, 0, 0, 255);
    BlockStyle.Rounding = 1.0f;
    BlockStyle.BorderWidth = 1.0f;
    BlockStyle.BorderWidthRunning = 3.0f;
    BlockStyle.Padding = ImVec4(8, 8, 8, 8);
    
    // Group style (slightly lighter/different for visual distinction)
    GroupStyle.BgColor = ImColor(55, 55, 70, 240);  // Slightly different from blocks
    GroupStyle.BorderColor = ImColor(110, 110, 120, 255);
    GroupStyle.BorderColorRunning = ImColor(255, 0, 0, 255);
    GroupStyle.Rounding = 2.0f;  // Slightly more rounded
    GroupStyle.BorderWidth = 1.0f;
    GroupStyle.BorderWidthRunning = 3.0f;
    GroupStyle.Padding = ImVec4(12, 12, 12, 12);  // More padding for groups
    
    // Parameter style (more rounded and grayer for visual distinction from blocks)
    ParameterStyle.BgColor = ImColor(70, 70, 80, 255);  // More gray for distinction
    ParameterStyle.BorderColor = ImColor(255, 255, 255, 200);
    ParameterStyle.BorderColorRunning = ImColor(255, 0, 0, 255);
    ParameterStyle.Rounding = 4.0f;  // More rounded than blocks
    ParameterStyle.BorderWidth = 1.0f;
    ParameterStyle.BorderWidthRunning = 3.0f;
    ParameterStyle.Padding = ImVec4(4, 2, 4, 2);
    
    // Pin colors (minimal, professional)
    PinColor_Input = ImColor(120, 120, 150, 255);      // Muted blue
    PinColor_Output = ImColor(150, 120, 120, 255);     // Muted red
    PinColor_Running = ImColor(80, 160, 80, 255);      // Muted green
    PinColor_Deactivated = ImColor(80, 80, 80, 200);   // Dark gray
    PinColor_Error = ImColor(200, 80, 80, 255);        // Muted red
    PinColor_Warning = ImColor(200, 160, 80, 255);     // Muted orange
    
    // Pin sizes (small and minimal)
    FlowPinSize = 8.0f;
    ParameterPinWidth = 8.0f;
    ParameterPinHeight = 4.0f;
    
    // Pin edge offsets
    ParameterPinEdgeOffset = 0.0f;  // Exactly on edge
    FlowPinEdgeOffset = 0.0f;       // Exactly on edge
    
    // Layout
    MinNodeWidth = 80.0f;
    MinNodeHeight = 40.0f;
    MinGroupSize = 100.0f;
    
    // Group-specific
    GroupResizeGripSize = 16.0f;
    GroupResizeGripLineSpacing = 3.5f;
    GroupResizeGripColor = ImColor(200, 200, 200, 220);
    
    // Parameter node-specific
    ParamBorderColorSource = ImColor(255, 215, 0, 255);      // Gold
    ParamBorderColorShortcut = ImColor(200, 200, 200, 180);  // Dimmed
    ParamBorderWidthSource = 2.0f;
    ParamBorderWidthNameAndValue = 1.5f;
    ParamBorderWidthSourceNameAndValue = 2.5f;
    ParamPaddingNameOnly = ImVec4(4, 2, 4, 2);
    ParamPaddingNameAndValue = ImVec4(8, 4, 8, 4);
    ParamPaddingSmallBox = ImVec4(2, 2, 2, 2);
    ParamPaddingMinimal = ImVec4(4, 4, 4, 4);
    ParamInputWidthNameOnly = 80.0f;
    ParamInputWidthNameAndValue = 100.0f;
    ParamInputWidthSmallBox = 50.0f;
    ParamMinimalSize = ImVec2(27, 20);
    ParamMinimalLinksSize = ImVec2(40, 20);
    
    // Link styling
    LinkThicknessNormal = 2.0f;
    LinkThicknessSelected = 4.0f;
    LinkThicknessParameterNormal = 1.5f;
    LinkThicknessParameterSelected = 3.0f;
    LinkColorHighlighted = ImColor(255, 0, 0, 255);  // Red for execution
    
    // Waypoint/Control Point styling
    WaypointRadius = 6.0f;
    WaypointBorderWidth = 2.0f;
    WaypointColor = ImColor(255, 200, 100, 255);      // Yellow/orange
    WaypointBorderColor = ImColor(0, 0, 0, 200);      // Black border
    WaypointColorHovered = ImColor(255, 220, 120, 255);  // Lighter yellow when hovered
    WaypointColorSelected = ImColor(255, 255, 150, 255);  // Bright yellow when selected
    WaypointPreviewRadius = 4.0f;
    WaypointPreviewColor = ImColor(255, 200, 100, 180);   // Semi-transparent yellow
    WaypointPreviewBorderColor = ImColor(0, 0, 0, 150);   // Semi-transparent black
}

bool StyleManager::LoadFromFile(const std::string& filename)
{
    std::ifstream file(filename);
    if (!file)
    {
        return false;
    }
    
    std::string jsonData((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return Deserialize(jsonData);
}

bool StyleManager::SaveToFile(const std::string& filename) const
{
    std::string jsonData = Serialize();
    
    std::ofstream file(filename);
    if (!file)
    {
        return false;
    }
    
    file << jsonData;
    return true;
}

std::string StyleManager::Serialize() const
{
    crude_json::value root;
    
    // Block Style
    auto& bs = root["block_style"];
    SAVE_COLOR(bs, BlockStyle.BgColor, "bg_color");
    SAVE_COLOR(bs, BlockStyle.BorderColor, "border_color");
    SAVE_COLOR(bs, BlockStyle.BorderColorRunning, "border_color_running");
    SAVE_FLOAT(bs, BlockStyle.Rounding, "rounding");
    SAVE_FLOAT(bs, BlockStyle.BorderWidth, "border_width");
    SAVE_FLOAT(bs, BlockStyle.BorderWidthRunning, "border_width_running");
    SAVE_VEC4(bs, BlockStyle.Padding, "padding");
    
    // Group Style
    auto& gs = root["group_style"];
    SAVE_COLOR(gs, GroupStyle.BgColor, "bg_color");
    SAVE_COLOR(gs, GroupStyle.BorderColor, "border_color");
    SAVE_COLOR(gs, GroupStyle.BorderColorRunning, "border_color_running");
    SAVE_FLOAT(gs, GroupStyle.Rounding, "rounding");
    SAVE_FLOAT(gs, GroupStyle.BorderWidth, "border_width");
    SAVE_FLOAT(gs, GroupStyle.BorderWidthRunning, "border_width_running");
    SAVE_VEC4(gs, GroupStyle.Padding, "padding");
    SAVE_FLOAT(gs, GroupResizeGripSize, "resize_grip_size");
    SAVE_FLOAT(gs, GroupResizeGripLineSpacing, "resize_grip_line_spacing");
    SAVE_COLOR(gs, GroupResizeGripColor, "resize_grip_color");
    
    // Parameter Style
    auto& ps = root["parameter_style"];
    SAVE_COLOR(ps, ParameterStyle.BgColor, "bg_color");
    SAVE_COLOR(ps, ParameterStyle.BorderColor, "border_color");
    SAVE_FLOAT(ps, ParameterStyle.Rounding, "rounding");
    SAVE_FLOAT(ps, ParameterStyle.BorderWidth, "border_width");
    SAVE_VEC4(ps, ParameterStyle.Padding, "padding");
    SAVE_COLOR(ps, ParamBorderColorSource, "border_color_source");
    SAVE_COLOR(ps, ParamBorderColorShortcut, "border_color_shortcut");
    SAVE_FLOAT(ps, ParamBorderWidthSource, "border_width_source");
    SAVE_FLOAT(ps, ParamBorderWidthNameAndValue, "border_width_name_and_value");
    SAVE_FLOAT(ps, ParamBorderWidthSourceNameAndValue, "border_width_source_name_and_value");
    SAVE_VEC4(ps, ParamPaddingNameOnly, "padding_name_only");
    SAVE_VEC4(ps, ParamPaddingNameAndValue, "padding_name_and_value");
    SAVE_VEC4(ps, ParamPaddingSmallBox, "padding_small_box");
    SAVE_VEC4(ps, ParamPaddingMinimal, "padding_minimal");
    SAVE_FLOAT(ps, ParamInputWidthNameOnly, "input_width_name_only");
    SAVE_FLOAT(ps, ParamInputWidthNameAndValue, "input_width_name_and_value");
    SAVE_FLOAT(ps, ParamInputWidthSmallBox, "input_width_small_box");
    SAVE_VEC2(ps, ParamMinimalSize, "minimal_size");
    SAVE_VEC2(ps, ParamMinimalLinksSize, "minimal_links_size");
    
    // Pin Colors
    auto& pc = root["pin_colors"];
    SAVE_COLOR(pc, PinColor_Input, "input");
    SAVE_COLOR(pc, PinColor_Output, "output");
    SAVE_COLOR(pc, PinColor_Running, "running");
    SAVE_COLOR(pc, PinColor_Deactivated, "deactivated");
    SAVE_COLOR(pc, PinColor_Error, "error");
    SAVE_COLOR(pc, PinColor_Warning, "warning");
    
    // Pin Sizes
    SAVE_FLOAT(root, FlowPinSize, "flow_pin_size");
    SAVE_FLOAT(root, ParameterPinWidth, "parameter_pin_width");
    SAVE_FLOAT(root, ParameterPinHeight, "parameter_pin_height");
    SAVE_FLOAT(root, ParameterPinEdgeOffset, "parameter_pin_edge_offset");
    SAVE_FLOAT(root, FlowPinEdgeOffset, "flow_pin_edge_offset");
    
    // Layout
    SAVE_FLOAT(root, MinNodeWidth, "min_node_width");
    SAVE_FLOAT(root, MinNodeHeight, "min_node_height");
    SAVE_FLOAT(root, MinGroupSize, "min_group_size");
    
    // Link styling
    auto& ls = root["link_style"];
    SAVE_FLOAT(ls, LinkThicknessNormal, "thickness_normal");
    SAVE_FLOAT(ls, LinkThicknessSelected, "thickness_selected");
    SAVE_FLOAT(ls, LinkThicknessParameterNormal, "thickness_parameter_normal");
    SAVE_FLOAT(ls, LinkThicknessParameterSelected, "thickness_parameter_selected");
    SAVE_COLOR(ls, LinkColorHighlighted, "color_highlighted");
    
    // Waypoint styling
    auto& ws = root["waypoint_style"];
    SAVE_FLOAT(ws, WaypointRadius, "radius");
    SAVE_FLOAT(ws, WaypointBorderWidth, "border_width");
    SAVE_COLOR(ws, WaypointColor, "color");
    SAVE_COLOR(ws, WaypointBorderColor, "border_color");
    SAVE_COLOR(ws, WaypointColorHovered, "color_hovered");
    SAVE_COLOR(ws, WaypointColorSelected, "color_selected");
    SAVE_FLOAT(ws, WaypointPreviewRadius, "preview_radius");
    SAVE_COLOR(ws, WaypointPreviewColor, "preview_color");
    SAVE_COLOR(ws, WaypointPreviewBorderColor, "preview_border_color");
    
    return root.dump(4);
}

bool StyleManager::Deserialize(const std::string& jsonData)
{
    auto root = crude_json::value::parse(jsonData);
    if (root.is_discarded() || !root.is_object())
    {
        printf("[StyleManager] Failed to parse JSON\n");
        return false;
    }
    
    // Block Style
    if (root.contains("block_style") && root["block_style"].is_object())
    {
        auto& bs = root["block_style"];
        LOAD_COLOR(bs, BlockStyle.BgColor, "bg_color");
        LOAD_COLOR(bs, BlockStyle.BorderColor, "border_color");
        LOAD_COLOR(bs, BlockStyle.BorderColorRunning, "border_color_running");
        LOAD_FLOAT(bs, BlockStyle.Rounding, "rounding");
        LOAD_FLOAT(bs, BlockStyle.BorderWidth, "border_width");
        LOAD_FLOAT(bs, BlockStyle.BorderWidthRunning, "border_width_running");
        LOAD_VEC4(bs, BlockStyle.Padding, "padding");
    }
    
    // Group Style
    if (root.contains("group_style") && root["group_style"].is_object())
    {
        auto& gs = root["group_style"];
        LOAD_COLOR(gs, GroupStyle.BgColor, "bg_color");
        LOAD_COLOR(gs, GroupStyle.BorderColor, "border_color");
        LOAD_COLOR(gs, GroupStyle.BorderColorRunning, "border_color_running");
        LOAD_FLOAT(gs, GroupStyle.Rounding, "rounding");
        LOAD_FLOAT(gs, GroupStyle.BorderWidth, "border_width");
        LOAD_FLOAT(gs, GroupStyle.BorderWidthRunning, "border_width_running");
        LOAD_VEC4(gs, GroupStyle.Padding, "padding");
        LOAD_FLOAT(gs, GroupResizeGripSize, "resize_grip_size");
        LOAD_FLOAT(gs, GroupResizeGripLineSpacing, "resize_grip_line_spacing");
        LOAD_COLOR(gs, GroupResizeGripColor, "resize_grip_color");
    }
    
    // Parameter Style
    if (root.contains("parameter_style") && root["parameter_style"].is_object())
    {
        auto& ps = root["parameter_style"];
        LOAD_COLOR(ps, ParameterStyle.BgColor, "bg_color");
        LOAD_COLOR(ps, ParameterStyle.BorderColor, "border_color");
        LOAD_FLOAT(ps, ParameterStyle.Rounding, "rounding");
        LOAD_FLOAT(ps, ParameterStyle.BorderWidth, "border_width");
        LOAD_VEC4(ps, ParameterStyle.Padding, "padding");
        LOAD_COLOR(ps, ParamBorderColorSource, "border_color_source");
        LOAD_COLOR(ps, ParamBorderColorShortcut, "border_color_shortcut");
        LOAD_FLOAT(ps, ParamBorderWidthSource, "border_width_source");
        LOAD_FLOAT(ps, ParamBorderWidthNameAndValue, "border_width_name_and_value");
        LOAD_FLOAT(ps, ParamBorderWidthSourceNameAndValue, "border_width_source_name_and_value");
        LOAD_VEC4(ps, ParamPaddingNameOnly, "padding_name_only");
        LOAD_VEC4(ps, ParamPaddingNameAndValue, "padding_name_and_value");
        LOAD_VEC4(ps, ParamPaddingSmallBox, "padding_small_box");
        LOAD_VEC4(ps, ParamPaddingMinimal, "padding_minimal");
        LOAD_FLOAT(ps, ParamInputWidthNameOnly, "input_width_name_only");
        LOAD_FLOAT(ps, ParamInputWidthNameAndValue, "input_width_name_and_value");
        LOAD_FLOAT(ps, ParamInputWidthSmallBox, "input_width_small_box");
        LOAD_VEC2(ps, ParamMinimalSize, "minimal_size");
        LOAD_VEC2(ps, ParamMinimalLinksSize, "minimal_links_size");
    }
    
    // Pin Colors
    if (root.contains("pin_colors") && root["pin_colors"].is_object())
    {
        auto& pc = root["pin_colors"];
        LOAD_COLOR(pc, PinColor_Input, "input");
        LOAD_COLOR(pc, PinColor_Output, "output");
        LOAD_COLOR(pc, PinColor_Running, "running");
        LOAD_COLOR(pc, PinColor_Deactivated, "deactivated");
        LOAD_COLOR(pc, PinColor_Error, "error");
        LOAD_COLOR(pc, PinColor_Warning, "warning");
    }
    
    // Pin Sizes
    LOAD_FLOAT(root, FlowPinSize, "flow_pin_size");
    LOAD_FLOAT(root, ParameterPinWidth, "parameter_pin_width");
    LOAD_FLOAT(root, ParameterPinHeight, "parameter_pin_height");
    LOAD_FLOAT(root, ParameterPinEdgeOffset, "parameter_pin_edge_offset");
    LOAD_FLOAT(root, FlowPinEdgeOffset, "flow_pin_edge_offset");
    
    // Layout
    LOAD_FLOAT(root, MinNodeWidth, "min_node_width");
    LOAD_FLOAT(root, MinNodeHeight, "min_node_height");
    LOAD_FLOAT(root, MinGroupSize, "min_group_size");
    
    // Link styling
    if (root.contains("link_style") && root["link_style"].is_object())
    {
        auto& ls = root["link_style"];
        LOAD_FLOAT(ls, LinkThicknessNormal, "thickness_normal");
        LOAD_FLOAT(ls, LinkThicknessSelected, "thickness_selected");
        LOAD_FLOAT(ls, LinkThicknessParameterNormal, "thickness_parameter_normal");
        LOAD_FLOAT(ls, LinkThicknessParameterSelected, "thickness_parameter_selected");
        LOAD_COLOR(ls, LinkColorHighlighted, "color_highlighted");
    }
    
    // Waypoint styling
    if (root.contains("waypoint_style") && root["waypoint_style"].is_object())
    {
        auto& ws = root["waypoint_style"];
        LOAD_FLOAT(ws, WaypointRadius, "radius");
        LOAD_FLOAT(ws, WaypointBorderWidth, "border_width");
        LOAD_COLOR(ws, WaypointColor, "color");
        LOAD_COLOR(ws, WaypointBorderColor, "border_color");
        LOAD_COLOR(ws, WaypointColorHovered, "color_hovered");
        LOAD_COLOR(ws, WaypointColorSelected, "color_selected");
        LOAD_FLOAT(ws, WaypointPreviewRadius, "preview_radius");
        LOAD_COLOR(ws, WaypointPreviewColor, "preview_color");
        LOAD_COLOR(ws, WaypointPreviewBorderColor, "preview_border_color");
    }
    
    return true;
}

void StyleManager::ApplyToEditorStyle(EditorContext* editorCtx)
{
    if (!editorCtx)
        return;
    
    // Set the editor context before getting style (GetStyle uses global context)
    // Use full namespace since we're inside ax::NodeEditor namespace
    ax::NodeEditor::SetCurrentEditor(editorCtx);
    
    // Get editor style via public API
    auto& style = ax::NodeEditor::GetStyle();
    
    // Set editor-level defaults
    // These can still be overridden per-node via PushStyleVar/PushStyleColor
    style.NodePadding = BlockStyle.Padding;
    style.NodeRounding = BlockStyle.Rounding;
    style.NodeBorderWidth = BlockStyle.BorderWidth;
    style.PinArrowSize = 0.0f;   // No arrows
    style.PinArrowWidth = 0.0f;
    
    // Apply waypoint styling (convert ImColor to ImU32)
    style.WaypointRadius = WaypointRadius;
    style.WaypointBorderWidth = WaypointBorderWidth;
    style.WaypointColor = WaypointColor;
    style.WaypointBorderColor = WaypointBorderColor;
    style.WaypointColorHovered = WaypointColorHovered;
    style.WaypointColorSelected = WaypointColorSelected;
}

} // namespace NodeEditor
} // namespace ax

