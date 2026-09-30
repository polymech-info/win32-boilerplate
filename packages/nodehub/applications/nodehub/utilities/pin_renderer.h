#pragma once
#include <imgui.h>
#include <imgui_node_editor.h>
#include "../types.h"

namespace ed = ax::NodeEditor;
class App;

//==============================================================================
// Pin Renderer Base Class
// 
// Encapsulates all pin rendering logic including:
// - Layout calculation (where the pin should be)
// - Visual rendering (how it looks)
// - Position queries (for waypoint creation)
// 
// Key principle: Pivot position == Render position (no manual tweaks!)
//==============================================================================

class PinRendererBase
{
public:
    virtual ~PinRendererBase() = default;
    
    // Core rendering interface
    virtual void BeginPin(ed::PinId id, ed::PinKind kind) = 0;
    virtual void EndPin() = 0;
    
    // Position queries (in screen space)
    virtual ImVec2 GetPivotPosition() const = 0;      // Where links connect
    virtual ImRect GetRenderBounds() const = 0;       // Full pin area (for hit testing)
    virtual ImVec2 GetRelativeOffset() const = 0;     // Offset from cursor position
    
    // Position queries (in canvas/node space) - useful for waypoint creation
    virtual ImVec2 GetPivotPositionRelativeToNode(const ImVec2& nodeTopLeft) const
    {
        return GetPivotPosition() - nodeTopLeft;
    }
    
    // Style configuration
    virtual void SetAlpha(float alpha) { m_Alpha = alpha; }
    float GetAlpha() const { return m_Alpha; }
    
protected:
    float m_Alpha = 1.0f;
    ImVec2 m_LastPivotPosition;
    ImRect m_LastRenderBounds;
};

//==============================================================================
// Parameter Pin Renderer
// 
// Renders parameter pins (data inputs/outputs) with icons and optional labels.
// - Input pins appear at TOP (links come from above)
// - Output pins appear at BOTTOM (links go below)
//==============================================================================

class ParameterPinRenderer : public PinRendererBase
{
public:
    struct Config
    {
        bool showLabel = true;           // Show parameter name
        float iconSize = 24.0f;          // Pin icon size
        float labelSpacing = 2.0f;       // Space between icon and label
        ImVec2 edgeOffset = ImVec2(0, 0); // Additional offset from node edge
        ImVec2 iconOffset = ImVec2(0, 0);  // Offset for pin icon positioning
    };
    
    ParameterPinRenderer(const Config& config = Config())
        : m_Config(config)
    {
    }
    
    // Main rendering method
    // Call this with pin data and it handles everything internally
    void Render(
        ed::PinId pinId,
        ed::PinKind kind,
        const Pin& pin,
        bool isLinked,
        App* app,
        const Config* overrideConfig = nullptr);
    
    // PinRendererBase interface
    void BeginPin(ed::PinId id, ed::PinKind kind) override;
    void EndPin() override;
    
    ImVec2 GetPivotPosition() const override { return m_LastPivotPosition; }
    ImRect GetRenderBounds() const override { return m_LastRenderBounds; }
    ImVec2 GetRelativeOffset() const override { return m_RelativeOffset; }
    
    // Configuration
    void SetConfig(const Config& config) { m_Config = config; }
    const Config& GetConfig() const { return m_Config; }
    
private:
    Config m_Config;
    ImVec2 m_RelativeOffset;
    bool m_IsActive = false;
    
    // Internal layout calculation
    struct Layout
    {
        ImRect iconRect;      // Where icon is drawn
        ImRect labelRect;     // Where label is drawn (if shown)
        ImRect combinedRect;  // Full pin area
        ImVec2 pivotPoint;    // Where links connect
    };
    
    Layout CalculateLayout(
        ed::PinKind kind,
        const char* labelText,
        float iconSize,
        bool showLabel,
        const ImVec2& cursorPos);
    
    void DrawIcon(const Pin& pin, bool isLinked, int alpha, const ImRect& iconRect, App* app);
};

//==============================================================================
// Flow Pin Renderer
// 
// Renders flow control pins (execution inputs/outputs) as small squares.
// - Input pins appear on LEFT (links come from left)
// - Output pins appear on RIGHT (links go right)
//==============================================================================

class FlowPinRenderer : public PinRendererBase
{
public:
    struct Config
    {
        float pinSize = 8.0f;              // Square size
        float edgeOffset = 10.0f;          // Distance from node edge
        float rounding = 2.0f;             // Corner rounding
        float borderWidth = 1.5f;          // Border thickness
        ImU32 fillColor = IM_COL32(180, 180, 180, 255);
        ImU32 borderColor = IM_COL32(255, 255, 255, 200);
    };
    
    FlowPinRenderer(const Config& config = Config())
        : m_Config(config)
    {
    }
    
    // Main rendering method
    void Render(
        ed::PinId pinId,
        ed::PinKind kind,
        const Pin& pin,
        App* app,
        const Config* overrideConfig = nullptr);
    
    // PinRendererBase interface
    void BeginPin(ed::PinId id, ed::PinKind kind) override;
    void EndPin() override;
    
    ImVec2 GetPivotPosition() const override { return m_LastPivotPosition; }
    ImRect GetRenderBounds() const override { return m_LastRenderBounds; }
    ImVec2 GetRelativeOffset() const override { return m_RelativeOffset; }
    
    // Configuration
    void SetConfig(const Config& config) { m_Config = config; }
    const Config& GetConfig() const { return m_Config; }
    
private:
    Config m_Config;
    ImVec2 m_RelativeOffset;
    bool m_IsActive = false;
    
    // Internal layout calculation
    struct Layout
    {
        ImRect visualRect;    // Where square is drawn (offset from dummy)
        ImVec2 pivotPoint;    // Center of visual rect (link connection)
    };
    
    Layout CalculateLayout(
        ed::PinKind kind,
        float pinSize,
        float edgeOffset,
        const ImVec2& dummyPos);
    
    void DrawSquare(const ImRect& rect, float rounding, float borderWidth, 
                    ImU32 fillColor, ImU32 borderColor, int alpha);
};

