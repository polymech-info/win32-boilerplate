#ifndef IMGUI_DEFINE_MATH_OPERATORS
#   define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "imgui_node_editor_internal.h"
#include "imgui_bezier_math.h"
#include "imgui_extra_math.h"
#include <algorithm>
#include <cmath>

namespace ax {
namespace NodeEditor {
namespace Detail {

//------------------------------------------------------------------------------
// GuidedLink Implementation
//------------------------------------------------------------------------------

std::vector<ImCubicBezierPoints> GuidedLink::GetCurveSegments(
    const ImVec2& start, 
    const ImVec2& end,
    const ImVec2& startDir,
    const ImVec2& endDir,
    float startStrength,
    float endStrength) const
{
    std::vector<ImCubicBezierPoints> segments;
    
    if (Mode != LinkMode::Guided || ControlPoints.empty())
    {
        // Return empty - caller will use automatic curve
        return segments;
    }
    
    // Build point chain: start → cp[0] → cp[1] → ... → cp[n] → end
    std::vector<ImVec2> points;
    points.reserve(ControlPoints.size() + 2);
    points.push_back(start);
    for (const auto& cp : ControlPoints)
        points.push_back(cp.Position);
    points.push_back(end);
    
    // Generate cubic bezier segments using Catmull-Rom interpolation
    for (size_t i = 0; i < points.size() - 1; ++i)
    {
        const ImVec2& p0 = points[i];
        const ImVec2& p1 = points[i + 1];
        
        ImVec2 m0, m1;  // Tangents at p0 and p1
        
        // Calculate tangent at p0
        if (i == 0)
        {
            // First segment: use pin direction with alignment adjustment
            const auto delta = p1 - p0;
            const float hDist = ImFabs(delta.x);
            const float vDist = ImFabs(delta.y);
            
            const bool startIsVertical = ImFabs(startDir.y) > ImFabs(startDir.x);
            const bool startIsHorizontal = ImFabs(startDir.x) > ImFabs(startDir.y);
            
            ImVec2 adjustedDir = startDir;
            
            // Horizontal alignment with vertical pins: blend towards horizontal
            if (vDist < hDist * 0.25f && startIsVertical)
            {
                const float sign = delta.x >= 0.0f ? 1.0f : -1.0f;
                const float blend = ImMin(1.0f, hDist / (startStrength + 1.0f));
                adjustedDir = ImNormalized(ImVec2(
                    startDir.x + sign * blend * 2.0f,
                    startDir.y * (1.0f - blend * 0.5f)
                ));
            }
            // Vertical alignment with horizontal pins: blend towards vertical
            else if (hDist < vDist * 0.25f && startIsHorizontal)
            {
                const float sign = delta.y >= 0.0f ? 1.0f : -1.0f;
                const float blend = ImMin(1.0f, vDist / (startStrength + 1.0f));
                adjustedDir = ImNormalized(ImVec2(
                    startDir.x * (1.0f - blend * 0.5f),
                    startDir.y + sign * blend * 2.0f
                ));
            }
            
            m0 = adjustedDir * startStrength;
        }
        else
        {
            // Catmull-Rom tangent: (p[i+1] - p[i-1]) / 2
            m0 = (p1 - points[i - 1]) * 0.5f;
        }
        
        // Calculate tangent at p1
        if (i == points.size() - 2)
        {
            // Last segment: use pin direction with alignment adjustment
            const auto delta = p1 - p0;
            const float hDist = ImFabs(delta.x);
            const float vDist = ImFabs(delta.y);
            
            const bool endIsVertical = ImFabs(endDir.y) > ImFabs(endDir.x);
            const bool endIsHorizontal = ImFabs(endDir.x) > ImFabs(endDir.y);
            
            ImVec2 adjustedDir = endDir;
            
            // Horizontal alignment with vertical pins: blend towards horizontal
            if (vDist < hDist * 0.25f && endIsVertical)
            {
                const float sign = delta.x >= 0.0f ? 1.0f : -1.0f;
                const float blend = ImMin(1.0f, hDist / (endStrength + 1.0f));
                adjustedDir = ImNormalized(ImVec2(
                    endDir.x - sign * blend * 2.0f,
                    endDir.y * (1.0f - blend * 0.5f)
                ));
            }
            // Vertical alignment with horizontal pins: blend towards vertical
            else if (hDist < vDist * 0.25f && endIsHorizontal)
            {
                const float sign = delta.y >= 0.0f ? 1.0f : -1.0f;
                const float blend = ImMin(1.0f, vDist / (endStrength + 1.0f));
                adjustedDir = ImNormalized(ImVec2(
                    endDir.x * (1.0f - blend * 0.5f),
                    endDir.y - sign * blend * 2.0f
                ));
            }
            
            m1 = adjustedDir * endStrength;
        }
        else
        {
            // Catmull-Rom tangent: (p[i+2] - p[i]) / 2
            m1 = (points[i + 2] - p0) * 0.5f;
        }
        
        // Convert Hermite to Bezier control points
        // Hermite: P(t) = h00(t)*p0 + h10(t)*m0 + h01(t)*p1 + h11(t)*m1
        // Bezier:  P(t) = (1-t)³*P0 + 3(1-t)²t*P1 + 3(1-t)t²*P2 + t³*P3
        // Conversion: P1 = p0 + m0/3, P2 = p1 - m1/3
        
        ImCubicBezierPoints bezier;
        bezier.P0 = p0;
        bezier.P1 = p0 + m0 / 3.0f;
        bezier.P2 = p1 - m1 / 3.0f;
        bezier.P3 = p1;
        
        segments.push_back(bezier);
    }
    
    return segments;
}

int GuidedLink::FindControlPoint(const ImVec2& position, float threshold) const
{
    float thresholdSq = threshold * threshold;
    int closestIndex = -1;
    float closestDistSq = thresholdSq;
    
    for (int i = 0; i < static_cast<int>(ControlPoints.size()); ++i)
    {
        const ImVec2 delta = ControlPoints[i].Position - position;
        const float distSq = delta.x * delta.x + delta.y * delta.y;
        
        if (distSq < closestDistSq)
        {
            closestDistSq = distSq;
            closestIndex = i;
        }
    }
    
    return closestIndex;
}

void GuidedLink::AddControlPoint(const ImVec2& position, int insertIndex)
{
    ImVec2 finalPos = EnableSnapping ? SnapToGrid(position) : position;
    
    if (insertIndex < 0 || insertIndex >= static_cast<int>(ControlPoints.size()))
    {
        // Append to end
        ControlPoints.emplace_back(finalPos, true);
    }
    else
    {
        // Insert at specific index
        ControlPoints.insert(ControlPoints.begin() + insertIndex, ControlPoint(finalPos, true));
    }
}

void GuidedLink::RemoveControlPoint(int index)
{
    if (index >= 0 && index < static_cast<int>(ControlPoints.size()))
    {
        ControlPoints.erase(ControlPoints.begin() + index);
        
        // Note: Don't automatically reset mode to Auto when control points are empty
        // Straight mode has no control points by design, and Guided mode can be empty too
    }
}

void GuidedLink::ClearControlPoints()
{
    ControlPoints.clear();
    // Note: Don't reset mode - Straight mode uses GuidedData but has no control points
}

ImVec2 GuidedLink::SnapToGrid(const ImVec2& position) const
{
    if (!EnableSnapping || SnapGridSize <= 0.0f)
        return position;
    
    return ImVec2(
        std::round(position.x / SnapGridSize) * SnapGridSize,
        std::round(position.y / SnapGridSize) * SnapGridSize
    );
}

//------------------------------------------------------------------------------
// LinkSettings Implementation
//------------------------------------------------------------------------------

void LinkSettings::ClearDirty()
{
    m_IsDirty = false;
    m_DirtyReason = SaveReasonFlags::None;
}

void LinkSettings::MakeDirty(SaveReasonFlags reason)
{
    m_IsDirty = true;
    m_DirtyReason = m_DirtyReason | reason;
}

json::value LinkSettings::Serialize()
{
    json::value result;
    
    // Save mode
    switch (m_Mode)
    {
        case LinkMode::Auto:     result["mode"] = "auto"; break;
        case LinkMode::Straight: result["mode"] = "straight"; break;
        case LinkMode::Guided:   result["mode"] = "guided"; break;
        default:                 result["mode"] = "auto"; break;
    }
    
    // Save control points if in guided mode
    if (m_Mode == LinkMode::Guided && !m_ControlPoints.empty())
    {
        json::value points = json::array();
        for (const auto& pt : m_ControlPoints)
        {
            json::value point;
            point["x"] = pt.x;
            point["y"] = pt.y;
            points.push_back(point);
        }
        result["control_points"] = points;
    }
    
    // Save snapping settings
    if (m_EnableSnapping)
    {
        result["enable_snapping"] = true;
        result["snap_grid_size"] = m_SnapGridSize;
    }
    
    return result;
}

bool LinkSettings::Parse(const json::value& data, LinkSettings& result)
{
    if (!data.is_object())
        return false;
    
    // Parse mode
    if (data.contains("mode") && data["mode"].is_string())
    {
        json::string mode = data["mode"].get<json::string>();
        if (mode == "straight")
            result.m_Mode = LinkMode::Straight;
        else if (mode == "guided")
            result.m_Mode = LinkMode::Guided;
        else
            result.m_Mode = LinkMode::Auto;
    }
    
    // Parse control points
    if (data.contains("control_points") && data["control_points"].is_array())
    {
        result.m_ControlPoints.clear();
        const auto pointsArray = data["control_points"].get<json::array>();
        
        for (const auto& pointValue : pointsArray)
        {
            if (pointValue.is_object() && 
                pointValue.contains("x") && pointValue["x"].is_number() &&
                pointValue.contains("y") && pointValue["y"].is_number())
            {
                ImVec2 pt;
                pt.x = static_cast<float>(pointValue["x"].get<json::number>());
                pt.y = static_cast<float>(pointValue["y"].get<json::number>());
                result.m_ControlPoints.push_back(pt);
            }
        }
    }
    
    // Parse snapping settings
    if (data.contains("enable_snapping") && data["enable_snapping"].is_boolean())
    {
        result.m_EnableSnapping = data["enable_snapping"].get<bool>();
    }
    
    if (data.contains("snap_grid_size") && data["snap_grid_size"].is_number())
    {
        result.m_SnapGridSize = static_cast<float>(data["snap_grid_size"].get<double>());
    }
    
    return true;
}

} // namespace Detail
} // namespace NodeEditor
} // namespace ax

