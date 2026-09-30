//------------------------------------------------------------------------------
// ImGui Node Editor - Guided Links Extension
// 
// Provides user-controllable waypoints/control points for custom link routing
//
// NOTE: This file is meant to be included INSIDE the ax::NodeEditor::Detail namespace
//------------------------------------------------------------------------------
#pragma once

//------------------------------------------------------------------------------
// Link Modes
//------------------------------------------------------------------------------
enum class LinkMode
{
    Auto,      // Automatic bezier curve (default)
    Straight,  // Simple straight line
    Guided,    // User-defined control points
};

//------------------------------------------------------------------------------
// Control Point - Single waypoint on a guided link
//------------------------------------------------------------------------------
struct ControlPoint
{
    ImVec2 Position;        // Canvas space coordinates
    bool   IsManual;        // True if user-placed, false if auto-calculated
    
    ControlPoint(const ImVec2& pos = ImVec2(0, 0), bool manual = true)
        : Position(pos)
        , IsManual(manual)
    {
    }
};

//------------------------------------------------------------------------------
// Guided Link Data - Extended link information
//------------------------------------------------------------------------------
struct GuidedLink
{
    LinkId                    ID;
    LinkMode                  Mode;
    std::vector<ControlPoint> ControlPoints;
    
    // Settings
    bool  EnableSnapping;
    float SnapGridSize;
    
    GuidedLink(LinkId id)
        : ID(id)
        , Mode(LinkMode::Auto)
        , EnableSnapping(false)
        , SnapGridSize(16.0f)
    {
    }
    
    // Generate curve segments from control points
    std::vector<ImCubicBezierPoints> GetCurveSegments(
        const ImVec2& start, 
        const ImVec2& end,
        const ImVec2& startDir,
        const ImVec2& endDir,
        float startStrength,
        float endStrength) const;
    
    // Find closest control point to position
    int FindControlPoint(const ImVec2& position, float threshold = 10.0f) const;
    
    // Add control point at position
    void AddControlPoint(const ImVec2& position, int insertIndex = -1);
    
    // Remove control point by index
    void RemoveControlPoint(int index);
    
    // Clear all control points
    void ClearControlPoints();
    
    // Snap position to grid
    ImVec2 SnapToGrid(const ImVec2& position) const;
};

//------------------------------------------------------------------------------
// Link Settings - Persistence for guided links
//------------------------------------------------------------------------------
struct LinkSettings
{
    LinkId                 m_ID;
    LinkMode               m_Mode;
    std::vector<ImVec2>    m_ControlPoints;  // Only manual points
    bool                   m_EnableSnapping;
    float                  m_SnapGridSize;
    
    bool                   m_Saved;
    bool                   m_IsDirty;
    SaveReasonFlags        m_DirtyReason;
    
    LinkSettings(LinkId id)
        : m_ID(id)
        , m_Mode(LinkMode::Auto)
        , m_EnableSnapping(false)
        , m_SnapGridSize(16.0f)
        , m_Saved(false)
        , m_IsDirty(false)
        , m_DirtyReason(SaveReasonFlags::None)
    {
    }
    
    void ClearDirty();
    void MakeDirty(SaveReasonFlags reason);
    
    json::value Serialize();
    
    static bool Parse(const json::value& data, LinkSettings& result);
};

