// constants.h - Centralized style and layout constants for node rendering
#pragma once
#include <imgui.h>

namespace NodeConstants {
    
    // ===== Pin Sizes =====
    constexpr float FLOW_PIN_SIZE = 8.0f;        // Size for flow pins (small squares)
    constexpr float PARAMETER_PIN_WIDTH = 8.0f;  // Width for parameter pins (small rectangles)
    constexpr float PARAMETER_PIN_HEIGHT = 4.0f; // Height for parameter pins (half of squares)
    
    // ===== Pin Edge Offsets =====
    // How far pins are from the node edge
    // 0.0f = exactly on edge (half inside, half outside)
    // Negative = inside the node
    // Positive = outside the node
    constexpr float PARAMETER_PIN_EDGE_OFFSET = 0.0f;  // Parameter pins on edge
    constexpr float FLOW_PIN_EDGE_OFFSET = 0.0f;       // Flow pins on edge
    
    // ===== Node Style - Regular Blocks =====
    namespace Blocks {
        constexpr float ROUNDING = 1.0f;
        constexpr float BORDER_WIDTH = 1.0f;
        constexpr float BORDER_WIDTH_RUNNING = 3.0f;  // Thicker when running
        constexpr float PADDING = 0.0f;
        
        // Padding as ImVec4 (left, top, right, bottom)
        const ImVec4 PADDING_VEC4(8.0f, 8.0f, 8.0f, 8.0f);
        
        // Colors
        const ImColor BG_COLOR(50, 50, 60, 240);
        const ImColor BORDER_COLOR(100, 100, 110, 255);
        const ImColor BORDER_COLOR_RUNNING(255, 0, 0, 255);  // Red when running
    }
    
    // ===== Node Style - Group Blocks =====
    namespace Groups {
        constexpr float ROUNDING = 1.0f;
        constexpr float BORDER_WIDTH = 1.0f;
        constexpr float BORDER_WIDTH_RUNNING = 3.0f;
        
        const ImVec4 PADDING_VEC4(8.0f, 8.0f, 8.0f, 8.0f);
        
        const ImColor BG_COLOR(50, 50, 60, 240);
        const ImColor BORDER_COLOR(100, 100, 110, 255);
        const ImColor BORDER_COLOR_RUNNING(255, 0, 0, 255);
        
        // Resize grip
        constexpr float RESIZE_GRIP_SIZE = 16.0f;
        constexpr float RESIZE_GRIP_LINE_SPACING = 3.5f;
        const ImColor RESIZE_GRIP_COLOR(200, 200, 200, 220);
    }
    
    // ===== Node Style - Parameter Nodes =====
    namespace Params {
        constexpr float ROUNDING = 4.0f;  // More rounded for visual distinction
        constexpr float BORDER_WIDTH = 1.0f;
        constexpr float BORDER_WIDTH_SOURCE = 2.0f;  // Thicker for source nodes
        constexpr float BORDER_WIDTH_NAME_AND_VALUE = 1.5f;
        constexpr float BORDER_WIDTH_SOURCE_NAME_AND_VALUE = 2.5f;
        
        const ImVec4 PADDING_NAME_ONLY(4.0f, 2.0f, 4.0f, 2.0f);
        const ImVec4 PADDING_NAME_AND_VALUE(8.0f, 4.0f, 8.0f, 4.0f);
        const ImVec4 PADDING_SMALL_BOX(2.0f, 2.0f, 2.0f, 2.0f);
        const ImVec4 PADDING_MINIMAL(4.0f, 4.0f, 4.0f, 4.0f);
        
        // Colors (grayer background for distinction from blocks)
        const ImColor BG_COLOR(70, 70, 80, 255);  // More gray
        const ImColor BORDER_COLOR(255, 255, 255, 200);
        const ImColor BORDER_COLOR_SOURCE(255, 215, 0, 255);      // Gold for source
        const ImColor BORDER_COLOR_SHORTCUT(200, 200, 200, 180);  // Dimmed for shortcuts
        
        // Pin icon sizes for different modes
        constexpr float PIN_ICON_SIZE_NORMAL = 24.0f;
        constexpr float PIN_ICON_SIZE_MINIMAL = 20.0f;
        
        // Widget widths
        constexpr float INPUT_WIDTH_NAME_ONLY = 80.0f;
        constexpr float INPUT_WIDTH_NAME_AND_VALUE = 100.0f;
        constexpr float INPUT_WIDTH_SMALL_BOX = 50.0f;
        
        // Dummy sizes for minimal modes
        const ImVec2 MINIMAL_SIZE(27.0f, 20.0f);
        const ImVec2 MINIMAL_LINKS_SIZE(40.0f, 20.0f);
    }
    
    // ===== Pin Renderer Colors =====
    namespace PinColors {
        // Flow pins
        const ImColor FLOW_FILL(68, 201, 156, 255);
        const ImColor FLOW_BORDER(68, 201, 156, 200);
        const ImColor FLOW_FILL_DEACTIVATED(68, 201, 156, 200);
        const ImColor FLOW_BORDER_DEACTIVATED(68, 201, 156, 150);
        
        constexpr float FLOW_ROUNDING = 3.0f;
        constexpr float FLOW_BORDER_WIDTH = 2.0f;
    }
    
    // ===== Layout Constants =====
    namespace Layout {
        constexpr float MIN_NODE_WIDTH = 80.0f;   // Minimum node width
        constexpr float MIN_NODE_HEIGHT = 40.0f;  // Minimum node height
        constexpr float MIN_GROUP_SIZE = 100.0f;  // Minimum group size
        
        // Pin spacing (when multiple pins on same edge)
        // Uses fractional offset: 1.0 / (count + 1) spacing
    }
    
} // namespace NodeConstants

