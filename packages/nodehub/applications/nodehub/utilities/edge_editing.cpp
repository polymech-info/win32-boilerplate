#define IMGUI_DEFINE_MATH_OPERATORS
#include "edge_editing.h"
#include <imgui_node_editor.h>
#include <imgui_internal.h>
#include <cmath>
#include <vector>

namespace ed = ax::NodeEditor;

// Forward declaration
int FindHoveredEdge(ed::LinkId linkId, const ImVec2& mousePos, float threshold);

bool EdgeEditor::Process()
{
    auto& io = ImGui::GetIO();
    // During canvas frame, GetMousePos already returns canvas coordinates!
    auto mousePos = ImGui::GetMousePos();
    
    // Start dragging if clicked on an edge
    if (!IsDragging && ImGui::IsMouseClicked(0))
    {
        // Check all guided links for edge hits
        auto hoveredLink = ed::GetHoveredLink();
        
        
        if (hoveredLink && ed::IsLinkGuided(hoveredLink))
        {
            // Use larger threshold and canvas coordinates
            int edgeIndex = FindHoveredEdge(hoveredLink, mousePos, 20.0f);  // Increased threshold
            
           
            if (edgeIndex >= 0)
            {
                      EditingLinkId = hoveredLink;
                EditingEdgeIndex = edgeIndex;
                DragStartPos = mousePos;
                IsDragging = true;
                
                // Select the link for visual feedback
                ed::SelectLink(hoveredLink, false);
                
                return true;
            }
        }
    }
    
    // If currently dragging
    if (IsDragging)
    {
        if (ImGui::IsMouseDragging(0, 0.0f))
        {
            auto currentMousePos = ImGui::GetMousePos();
            auto dragDelta = currentMousePos - DragStartPos;
            DragStartPos = currentMousePos;
            
            // Get current waypoints
            int cpCount = ed::GetLinkControlPointCount(EditingLinkId);
            if (cpCount > 0 && EditingEdgeIndex >= 0 && EditingEdgeIndex < cpCount - 1)
            {
                std::vector<ImVec2> points(cpCount);
                ed::GetLinkControlPoints(EditingLinkId, points.data(), cpCount);
                
                // Determine if this is a horizontal or vertical edge
                ImVec2 p0 = points[EditingEdgeIndex];
                ImVec2 p1 = points[EditingEdgeIndex + 1];
                
                bool isHorizontal = std::abs(p1.y - p0.y) < std::abs(p1.x - p0.x);
                
                // Move both waypoints of this edge
                if (isHorizontal)
                {
                    // Horizontal edge: only allow vertical movement
                    points[EditingEdgeIndex].y += dragDelta.y;
                    points[EditingEdgeIndex + 1].y += dragDelta.y;
                }
                else
                {
                    // Vertical edge: only allow horizontal movement
                    points[EditingEdgeIndex].x += dragDelta.x;
                    points[EditingEdgeIndex + 1].x += dragDelta.x;
                }
                
                // Update waypoints
                ed::SetLinkControlPoints(EditingLinkId, points.data(), cpCount);
                
                // Mark link as user-manipulated (user moved waypoints)
                if (MarkLinkCallback)
                {
                    MarkLinkCallback(EditingLinkId, MarkLinkUserData);
                }
            }
            
            return true;  // Handled
        }
        else
        {
            // Mouse released - stop dragging
            // Mark link as user-manipulated when drag ends (user moved waypoints)
            if (EditingLinkId && MarkLinkCallback)
            {
                MarkLinkCallback(EditingLinkId, MarkLinkUserData);
            }
            
            IsDragging = false;
            EditingLinkId = 0;
            EditingEdgeIndex = -1;
        }
    }
    
    return false;
}

void EdgeEditor::DrawFeedback()
{
    // Draw visual feedback for hovered/selected edges
    if (IsDragging && EditingLinkId)
    {
        // Get the edge being dragged
        int cpCount = ed::GetLinkControlPointCount(EditingLinkId);
        if (cpCount > 0 && EditingEdgeIndex >= 0 && EditingEdgeIndex < cpCount - 1)
        {
            std::vector<ImVec2> points(cpCount);
            ed::GetLinkControlPoints(EditingLinkId, points.data(), cpCount);
            
            ImVec2 p0 = ed::CanvasToScreen(points[EditingEdgeIndex]);
            ImVec2 p1 = ed::CanvasToScreen(points[EditingEdgeIndex + 1]);
            
            // Draw highlighted edge
            auto drawList = ImGui::GetForegroundDrawList();
            drawList->AddLine(p0, p1, IM_COL32(255, 100, 255, 255), 6.0f);  // Thick magenta line
        }
    }
}

// Helper: Find which edge of a guided link the mouse is over
int FindHoveredEdge(ed::LinkId linkId, const ImVec2& mousePos, float threshold)
{
    int cpCount = ed::GetLinkControlPointCount(linkId);
     if (cpCount <= 1)
        return -1;
    
    // Get all control points (these are in CANVAS space)
    std::vector<ImVec2> points(cpCount);
    ed::GetLinkControlPoints(linkId, points.data(), cpCount);
      
    // Test each edge (segment between consecutive waypoints)
    for (int i = 0; i < cpCount - 1; ++i)
    {
        const ImVec2& p0 = points[i];
        const ImVec2& p1 = points[i + 1];
        
        // Distance from point to line segment
        ImVec2 delta = p1 - p0;
        float lengthSq = delta.x * delta.x + delta.y * delta.y;
        
        if (lengthSq < 0.0001f)
            continue;
        
        ImVec2 pointDelta = mousePos - p0;
        float t = (pointDelta.x * delta.x + pointDelta.y * delta.y) / lengthSq;
        t = ImClamp(t, 0.0f, 1.0f);
        
        ImVec2 closest = p0 + delta * t;
        ImVec2 diff = mousePos - closest;
        float distSq = diff.x * diff.x + diff.y * diff.y;
        float dist = sqrtf(distSq);
        
        if (distSq <= threshold * threshold)
            return i;  // Return edge index
    }
    
    return -1;
}

