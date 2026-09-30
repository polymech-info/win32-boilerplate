#define IMGUI_DEFINE_MATH_OPERATORS
#include "pathfinding.h"
#include "../types.h"  // For PinType, PinKind, NodeType enum definitions
#include "../containers/root_container.h"  // For RootContainer::GetAllLinks()
#include "../Logging.h"
#include <imgui_node_editor.h>  // For ed::GetLinkControlPointCount, ed::GetLinkControlPoints
#include <spdlog/spdlog.h>
#include <cmath>
#include <algorithm>

// Pathfinding feature flags (configure here)
// #define WAYPOINT_DEBUG                // Enable debug logging
// #define ENABLE_LINK_FITTING          // Align horizontal segments within 100 units
// #define ENABLE_LINK_AUTO_COLLAPSE    // Collapse waypoints to straight when within 100 units

namespace {

std::shared_ptr<spdlog::logger> GetPathfindingLogger()
{
    if (!g_logger)
        return nullptr;

    auto logger = spdlog::get("pathfinding");
    if (!logger)
    {
        logger = std::make_shared<spdlog::logger>("pathfinding", g_logger->sinks().begin(), g_logger->sinks().end());
        logger->set_level(g_logger->level());
        logger->flush_on(g_logger->flush_level());
        logger->set_pattern("[%H:%M:%S:%e] [%^%n-%l%$] %v");
        spdlog::register_logger(logger);
    }
    else if (logger->level() != g_logger->level())
    {
        logger->set_level(g_logger->level());
    }

    return logger;
}

template <typename... Args>
void PathfindingLog(spdlog::level::level_enum level, const char* fmt, Args&&... args)
{
    if (auto logger = GetPathfindingLogger())
    {
        logger->log(level, fmt, std::forward<Args>(args)...);
    }
}

#ifdef WAYPOINT_DEBUG
template <typename... Args>
void PathfindingDebug(const char* fmt, Args&&... args)
{
    PathfindingLog(spdlog::level::debug, fmt, std::forward<Args>(args)...);
}
#else
template <typename... Args>
void PathfindingDebug(const char*, Args&&...)
{
}
#endif

template <typename... Args>
void PathfindingInfo(const char* fmt, Args&&... args)
{
    PathfindingLog(spdlog::level::info, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void PathfindingWarn(const char* fmt, Args&&... args)
{
    PathfindingLog(spdlog::level::warn, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void PathfindingError(const char* fmt, Args&&... args)
{
    PathfindingLog(spdlog::level::err, fmt, std::forward<Args>(args)...);
}

} // namespace

namespace PathFinding
{

//-----------------------------------------------------------------------------
// Tunable Constants (adjust these for different behaviors)
//-----------------------------------------------------------------------------

// Link Fitting (horizontal segment alignment)
static constexpr float HORIZONTAL_ALIGNMENT_TOLERANCE = 100.0f;  // Y-axis tolerance for grouping segments

// Auto-Collapse (waypoint simplification)
static constexpr float COLLAPSE_RADIUS = 20.0f;        // Bounding box size threshold (units)
static constexpr float COLLINEAR_TOLERANCE = 30.0f;    // Perpendicular distance threshold (pixels)

// Obstacle intersection methods
bool Obstacle::IntersectsSegment(const ImVec2& p1, const ImVec2& p2) const
{
    // Check if segment intersects with obstacle bounds
    // Using axis-aligned bounding box intersection
    float segMinX = std::min(p1.x, p2.x);
    float segMaxX = std::max(p1.x, p2.x);
    float segMinY = std::min(p1.y, p2.y);
    float segMaxY = std::max(p1.y, p2.y);
    
    // Quick AABB rejection test
    if (segMaxX < min.x || segMinX > max.x || segMaxY < min.y || segMinY > max.y)
        return false;
    
    // If segment is horizontal or vertical, simple check
    if (p1.x == p2.x) // Vertical segment
        return IntersectsVerticalLine(p1.x, segMinY, segMaxY);
    if (p1.y == p2.y) // Horizontal segment
        return IntersectsHorizontalLine(p1.y, segMinX, segMaxX);
    
    // For diagonal segments, check if any point is inside or if segment crosses bounds
    // Simple approximation: if endpoints are outside, check if segment crosses through
    return true; // Conservative: if AABB overlaps, consider it intersecting
}

bool Obstacle::IntersectsHorizontalLine(float y, float x1, float x2) const
{
    // Check if the horizontal line's Y is within obstacle's Y range
    // Lines AT the edge (for pin connections) are NOT considered intersections
    const float EDGE_CLEARANCE = 5.0f;  // Must be inside by at least 5 pixels to count as intersection
    if (y < (min.y + EDGE_CLEARANCE) || y > (max.y - EDGE_CLEARANCE))
        return false;  // Line is outside or too close to edge - not a blocking intersection
    
    // Check if the horizontal segment overlaps with obstacle's X range
    float segMinX = std::min(x1, x2);
    float segMaxX = std::max(x1, x2);
    
    // Intersection: segment overlaps with obstacle horizontally
    bool horizontalOverlap = segMaxX >= min.x && segMinX <= max.x;
    return horizontalOverlap;
}

bool Obstacle::IntersectsVerticalLine(float x, float y1, float y2) const
{
    if (x < min.x || x > max.x)
        return false;
    float segMinY = std::min(y1, y2);
    float segMaxY = std::max(y1, y2);
    return segMaxY >= min.y && segMinY <= max.y;
}

// Clearance constants for pathfinding
namespace Constants
{
    // Minimum distance for edges parallel to target node (horizontal segments above/below blocks)
    static constexpr float MIN_PARALLEL_CLEARANCE = 20.0f;
    
    // Minimum distance above block's top edge for horizontal segments connecting to top pins
    static constexpr float MIN_ABOVE_BLOCK_CLEARANCE = 20.0f;
    
    // Minimum distance below start node for horizontal segments
    static constexpr float MIN_BELOW_START_CLEARANCE = 15.0f;
    
    // Minimum distance from block edges for same-block routing (waypoints)
    static constexpr float MIN_SAME_BLOCK_CLEARANCE = 25.0f;
    
    // Vertical tolerance for pin alignment detection
    static constexpr float PIN_ALIGNMENT_TOLERANCE = 5.0f;
    
    // Height difference threshold for horizontal flows (avoid waypoints for small differences)
    static constexpr float HORIZONTAL_FLOW_HEIGHT_THRESHOLD = 10.0f;
    
    // Position tolerance for same-block detection
    static constexpr float SAME_BLOCK_POSITION_TOLERANCE = 1.0f;
}

bool NeedsWaypoints(const ImVec2& startPos, const ImVec2& endPos, const ImVec2& startDir, const ImVec2& endDir)
{
    // Check if pins are flowing in compatible directions (can use straight/simple path)
    
    // If flowing downward (0,1) to upward (0,-1) and end is below start → direct path
    if (startDir.y > 0 && endDir.y < 0 && endPos.y > startPos.y)
    {
        // Simple case: output flows down, input accepts from above, and input is below output
        return false;  // Direct connection works
    }
    
    // All other cases need waypoints
    return true;
}

// Helper: Check if a segment would intersect any obstacles
static bool SegmentIntersectsObstacles(const ImVec2& p1, const ImVec2& p2, 
                                        const ImVec2& startNodeMin, const ImVec2& startNodeMax,
                                        const ImVec2& endNodeMin, const ImVec2& endNodeMax,
                                        const std::vector<Obstacle>& obstacles)
{
    for (const auto& obstacle : obstacles)
    {
        // Skip if this obstacle is the start or end node (we already handle those separately)
        bool isStartNode = (std::abs(obstacle.min.x - startNodeMin.x) < 1.0f &&
                           std::abs(obstacle.min.y - startNodeMin.y) < 1.0f &&
                           std::abs(obstacle.max.x - startNodeMax.x) < 1.0f &&
                           std::abs(obstacle.max.y - startNodeMax.y) < 1.0f);
        bool isEndNode = (std::abs(obstacle.min.x - endNodeMin.x) < 1.0f &&
                         std::abs(obstacle.min.y - endNodeMin.y) < 1.0f &&
                         std::abs(obstacle.max.x - endNodeMax.x) < 1.0f &&
                         std::abs(obstacle.max.y - endNodeMax.y) < 1.0f);
        
        if (isStartNode || isEndNode)
            continue;
        
        if (obstacle.IntersectsSegment(p1, p2))
            return true;
    }
    return false;
}

//-----------------------------------------------------------------------------
// Helper Functions (Geometry and Detection)
//-----------------------------------------------------------------------------

bool IsSameBlock(const NodeContext& start, const NodeContext& end)
{
    const float SAME_BLOCK_POSITION_TOLERANCE = 1.0f;
    return (std::abs(start.Min.x - end.Min.x) < SAME_BLOCK_POSITION_TOLERANCE &&
            std::abs(start.Min.y - end.Min.y) < SAME_BLOCK_POSITION_TOLERANCE &&
            std::abs(start.Max.x - end.Max.x) < SAME_BLOCK_POSITION_TOLERANCE &&
            std::abs(start.Max.y - end.Max.y) < SAME_BLOCK_POSITION_TOLERANCE);
}

bool NodesOverlapX(const NodeContext& start, const NodeContext& end)
{
    return (start.Max.x > end.Min.x && start.Min.x < end.Max.x);
}

bool EndIsBelowStart(const RoutingContext& ctx)
{
    return ctx.EndNode.Min.y > ctx.StartNode.Max.y;
}

bool PinIsAtTop(const PinContext& pin, const NodeContext& node)
{
    float nodeCenterY = (node.Min.y + node.Max.y) * 0.5f;
    return pin.Position.y < nodeCenterY;
}

bool PinIsOnLeft(const PinContext& pin, const NodeContext& node)
{
    float distanceToLeft = std::abs(pin.Position.x - node.Min.x);
    float distanceToRight = std::abs(pin.Position.x - node.Max.x);
    return distanceToLeft < distanceToRight;
}

float CalculateHorizontalClearanceY(const RoutingContext& ctx, bool aboveBlock)
{
    if (aboveBlock)
    {
        return ctx.EndNode.Min.y - Constants::MIN_ABOVE_BLOCK_CLEARANCE;
    }
    else
    {
        float waypointY = ctx.StartNode.Max.y + Constants::MIN_PARALLEL_CLEARANCE;
        float midY = (ctx.StartPin.Position.y + ctx.EndPin.Position.y) * 0.5f;
        if (midY > waypointY)
            waypointY = midY;
        return std::max(waypointY, ctx.StartNode.Max.y + Constants::MIN_BELOW_START_CLEARANCE);
    }
}

float CalculateVerticalClearanceX(const RoutingContext& ctx, bool leftSide)
{
    if (leftSide)
    {
        return std::min(ctx.EndNode.Min.x, ctx.StartNode.Min.x) - ctx.Margin;
    }
    else
    {
        return std::max(ctx.EndNode.Max.x, ctx.StartNode.Max.x) + ctx.Margin;
    }
}

bool HorizontalSegmentIntersectsNode(const NodeContext& node, float y, float x1, float x2)
{
    Obstacle obstacle = {node.Min, node.Max};
    float segMinX = std::min(x1, x2);
    float segMaxX = std::max(x1, x2);
    return obstacle.IntersectsHorizontalLine(y, segMinX, segMaxX);
}

bool SegmentIntersectsObstacles(const ImVec2& p1, const ImVec2& p2, const RoutingContext& ctx)
{
    for (auto it = ctx.Obstacles.begin(); it != ctx.Obstacles.end(); ++it)
    {
        const auto& obstacle = *it;
        
        // Skip if this obstacle is the start or end node
        bool isStartNode = (std::abs(obstacle.min.x - ctx.StartNode.Min.x) < 1.0f &&
                           std::abs(obstacle.min.y - ctx.StartNode.Min.y) < 1.0f &&
                           std::abs(obstacle.max.x - ctx.StartNode.Max.x) < 1.0f &&
                           std::abs(obstacle.max.y - ctx.StartNode.Max.y) < 1.0f);
        bool isEndNode = (std::abs(obstacle.min.x - ctx.EndNode.Min.x) < 1.0f &&
                         std::abs(obstacle.min.y - ctx.EndNode.Min.y) < 1.0f &&
                         std::abs(obstacle.max.x - ctx.EndNode.Max.x) < 1.0f &&
                         std::abs(obstacle.max.y - ctx.EndNode.Max.y) < 1.0f);
        
        if (isStartNode || isEndNode)
            continue;
        
        if (obstacle.IntersectsSegment(p1, p2))
            return true;
    }
    return false;
}

bool DeterminePreferredSide(const RoutingContext& ctx)
{
    bool pinOnLeft = PinIsOnLeft(ctx.EndPin, ctx.EndNode);
    bool startIsLeft = ctx.StartPin.Position.x < ctx.EndNode.Min.x;
    bool startIsRight = ctx.StartPin.Position.x > ctx.EndNode.Max.x;
    
    if (startIsLeft && pinOnLeft)
        return true;  // Both on left - route left
    else if (startIsRight && !pinOnLeft)
        return false; // Both on right - route right
    else
        return pinOnLeft; // Mismatch - use pin side
}

//-----------------------------------------------------------------------------
// Strategy Selection
//-----------------------------------------------------------------------------

RoutingStrategy SelectStrategy(const RoutingContext& ctx)
{
#ifdef WAYPOINT_DEBUG
    PathfindingDebug("[SelectStrategy] StartDir=({:.1f},{:.1f}) EndDir=({:.1f},{:.1f}) EndIsBelowStart={}",
                     ctx.StartPin.Direction.x, ctx.StartPin.Direction.y,
                     ctx.EndPin.Direction.x, ctx.EndPin.Direction.y,
                     EndIsBelowStart(ctx));
#endif
    
    // Same-block routing check
    if (IsSameBlock(ctx.StartNode, ctx.EndNode))
    {
#ifdef WAYPOINT_DEBUG
        PathfindingDebug("[SelectStrategy] Selected: SameBlock");
#endif
        return RoutingStrategy::SameBlock;
    }
    
    // Flowing downward to upward (e.g. parameter output to block parameter input)
    if (ctx.StartPin.Direction.y > 0 && ctx.EndPin.Direction.y < 0)
    {
        // For parameter connections, try Z-shape even if nodes are at similar Y levels
        // (not just when end is strictly below start)
        bool endStrictlyBelow = EndIsBelowStart(ctx);
        bool endAtSimilarLevel = !endStrictlyBelow && (ctx.EndPin.Position.y >= ctx.StartPin.Position.y);
        
#ifdef WAYPOINT_DEBUG
        PathfindingDebug("[SelectStrategy] endStrictlyBelow={}, endAtSimilarLevel={} (startPin.y={:.1f}, endPin.y={:.1f})",
                         endStrictlyBelow, endAtSimilarLevel, ctx.StartPin.Position.y, ctx.EndPin.Position.y);
#endif
        
        if (endStrictlyBelow || endAtSimilarLevel)
        {
            // Check if simple Z-shape is possible (horizontal segment below start)
            float horizontalY = CalculateHorizontalClearanceY(ctx, false);
            bool intersectsNode = HorizontalSegmentIntersectsNode(ctx.EndNode, horizontalY, ctx.StartPin.Position.x, ctx.EndPin.Position.x);
            bool intersectsObstacles = SegmentIntersectsObstacles(ImVec2(ctx.StartPin.Position.x, horizontalY), ImVec2(ctx.EndPin.Position.x, horizontalY), ctx);
            
#ifdef WAYPOINT_DEBUG
            if (intersectsNode || intersectsObstacles)
            {
                PathfindingDebug("[SelectStrategy] ZShape below failed: horizontalY={:.1f}, endNode.Min.y={:.1f}, intersectsNode={}, intersectsObstacles={}",
                                  horizontalY, ctx.EndNode.Min.y, intersectsNode, intersectsObstacles);
            }
#endif
            
            if (!intersectsNode && !intersectsObstacles)
            {
#ifdef WAYPOINT_DEBUG
                PathfindingDebug("[SelectStrategy] Selected: ZShape (below)");
#endif
                return RoutingStrategy::ZShape;
            }
            
            // Try above block
            float aboveY = CalculateHorizontalClearanceY(ctx, true);
            intersectsNode = HorizontalSegmentIntersectsNode(ctx.EndNode, aboveY, ctx.StartPin.Position.x, ctx.EndPin.Position.x);
            intersectsObstacles = SegmentIntersectsObstacles(ImVec2(ctx.StartPin.Position.x, aboveY), ImVec2(ctx.EndPin.Position.x, aboveY), ctx);
            
#ifdef WAYPOINT_DEBUG
            if (intersectsNode || intersectsObstacles)
            {
                PathfindingDebug("[SelectStrategy] ZShape above failed: aboveY={:.1f}, endNode.Min.y={:.1f}, intersectsNode={}, intersectsObstacles={}",
                                  aboveY, ctx.EndNode.Min.y, intersectsNode, intersectsObstacles);
            }
            else
            {
                PathfindingDebug("[SelectStrategy] ZShape above succeeded: aboveY={:.1f}, endNode.Min.y={:.1f}",
                                  aboveY, ctx.EndNode.Min.y);
            }
#endif
            
            if (!intersectsNode && !intersectsObstacles)
            {
#ifdef WAYPOINT_DEBUG
                PathfindingDebug("[SelectStrategy] Selected: ZShapeAboveBlock");
#endif
                return RoutingStrategy::ZShapeAboveBlock;
            }
            
#ifdef WAYPOINT_DEBUG
            PathfindingDebug("[SelectStrategy] Selected: AroundObstacles (fallback from Z-shape checks)");
#endif
            return RoutingStrategy::AroundObstacles;
        }
        else
        {
            // End is above or same level - need U-shape
            if (PinIsAtTop(ctx.EndPin, ctx.EndNode) && !NodesOverlapX(ctx.StartNode, ctx.EndNode))
            {
                return RoutingStrategy::UShapeAbove;
            }
            return RoutingStrategy::UShapeBelow;
        }
    }
    
    // Horizontal flow (side-to-side)
    if (std::abs(ctx.StartPin.Direction.x) > 0.5f && std::abs(ctx.EndPin.Direction.x) > 0.5f)
    {
        return RoutingStrategy::HorizontalFlow;
    }
    
    // Check if direct path is possible
    if (!NeedsWaypoints(ctx.StartPin.Position, ctx.EndPin.Position, ctx.StartPin.Direction, ctx.EndPin.Direction))
    {
        return RoutingStrategy::Direct;
    }
    
    // Default: L-shape
    return RoutingStrategy::LShape;
}

//-----------------------------------------------------------------------------
// Routing Strategy Functions
//-----------------------------------------------------------------------------

std::vector<ImVec2> RouteSameBlock(const RoutingContext& ctx)
{
    // Route around the same block - handles all pin edge combinations
    // Detect which edges the pins are on based on their directions
    std::vector<ImVec2> waypoints;
    
    // Detect pin edge locations based on direction vectors
    enum class PinEdge { Top, Bottom, Left, Right };
    
    auto detectEdge = [](const ImVec2& dir) -> PinEdge {
        if (std::abs(dir.y) > std::abs(dir.x)) {
            return (dir.y > 0) ? PinEdge::Bottom : PinEdge::Top;
        } else {
            return (dir.x > 0) ? PinEdge::Right : PinEdge::Left;
        }
    };
    
    PinEdge startEdge = detectEdge(ctx.StartPin.Direction);
    PinEdge endEdge = detectEdge(ctx.EndPin.Direction);
    
    // Define clearance values
    float topClearY = ctx.StartNode.Min.y - Constants::MIN_SAME_BLOCK_CLEARANCE;
    float bottomClearY = ctx.StartNode.Max.y + Constants::MIN_SAME_BLOCK_CLEARANCE;
    float leftClearX = ctx.StartNode.Min.x - Constants::MIN_SAME_BLOCK_CLEARANCE;
    float rightClearX = ctx.StartNode.Max.x + Constants::MIN_SAME_BLOCK_CLEARANCE;
    
    // Handle different edge combinations
    // Case 1: Bottom output → Top input (most common for parameters)
    if (startEdge == PinEdge::Bottom && endEdge == PinEdge::Top)
    {
        // Route around the side based on which side the input pin is closer to
        bool inputOnLeft = PinIsOnLeft(ctx.EndPin, ctx.EndNode);
        
        if (inputOnLeft)
        {
            // Route around LEFT side: down → left → up
            waypoints.push_back(ImVec2(ctx.StartPin.Position.x, bottomClearY));   // 1. Down from output
            waypoints.push_back(ImVec2(leftClearX, bottomClearY));                // 2. Left to clear block
            waypoints.push_back(ImVec2(leftClearX, topClearY));                   // 3. Up to clearance above block
            waypoints.push_back(ImVec2(ctx.EndPin.Position.x, topClearY));        // 4. Right to input pin
        }
        else
        {
            // Route around RIGHT side: down → right → up
            waypoints.push_back(ImVec2(ctx.StartPin.Position.x, bottomClearY));   // 1. Down from output
            waypoints.push_back(ImVec2(rightClearX, bottomClearY));               // 2. Right to clear block
            waypoints.push_back(ImVec2(rightClearX, topClearY));                  // 3. Up to clearance above block
            waypoints.push_back(ImVec2(ctx.EndPin.Position.x, topClearY));        // 4. Left to input pin
        }
    }
    // Case 2: Right output → Left input (common for flow pins)
    else if (startEdge == PinEdge::Right && endEdge == PinEdge::Left)
    {
        // Determine if we route above or below based on pin positions
        bool routeAbove = (ctx.StartPin.Position.y < (ctx.StartNode.Min.y + ctx.StartNode.Max.y) * 0.5f);
        
        if (routeAbove)
        {
            // Route around TOP: right → up → left
            waypoints.push_back(ImVec2(rightClearX, ctx.StartPin.Position.y));   // 1. Right from output
            waypoints.push_back(ImVec2(rightClearX, topClearY));                 // 2. Up to clearance above block
            waypoints.push_back(ImVec2(leftClearX, topClearY));                  // 3. Left across top
            waypoints.push_back(ImVec2(leftClearX, ctx.EndPin.Position.y));      // 4. Down to input pin
        }
        else
        {
            // Route around BOTTOM: right → down → left
            waypoints.push_back(ImVec2(rightClearX, ctx.StartPin.Position.y));   // 1. Right from output
            waypoints.push_back(ImVec2(rightClearX, bottomClearY));              // 2. Down to clearance below block
            waypoints.push_back(ImVec2(leftClearX, bottomClearY));               // 3. Left across bottom
            waypoints.push_back(ImVec2(leftClearX, ctx.EndPin.Position.y));      // 4. Up to input pin
        }
    }
    // Case 3: Left output → Right input (reverse flow)
    else if (startEdge == PinEdge::Left && endEdge == PinEdge::Right)
    {
        // Determine if we route above or below
        bool routeAbove = (ctx.StartPin.Position.y < (ctx.StartNode.Min.y + ctx.StartNode.Max.y) * 0.5f);
        
        if (routeAbove)
        {
            // Route around TOP: left → up → right
            waypoints.push_back(ImVec2(leftClearX, ctx.StartPin.Position.y));    // 1. Left from output
            waypoints.push_back(ImVec2(leftClearX, topClearY));                  // 2. Up to clearance
            waypoints.push_back(ImVec2(rightClearX, topClearY));                 // 3. Right across top
            waypoints.push_back(ImVec2(rightClearX, ctx.EndPin.Position.y));     // 4. Down to input
        }
        else
        {
            // Route around BOTTOM: left → down → right
            waypoints.push_back(ImVec2(leftClearX, ctx.StartPin.Position.y));    // 1. Left from output
            waypoints.push_back(ImVec2(leftClearX, bottomClearY));               // 2. Down to clearance
            waypoints.push_back(ImVec2(rightClearX, bottomClearY));              // 3. Right across bottom
            waypoints.push_back(ImVec2(rightClearX, ctx.EndPin.Position.y));     // 4. Up to input
        }
    }
    // Case 4: Top output → Bottom input (upward flow)
    else if (startEdge == PinEdge::Top && endEdge == PinEdge::Bottom)
    {
        // Route around the side
        bool inputOnLeft = PinIsOnLeft(ctx.EndPin, ctx.EndNode);
        
        if (inputOnLeft)
        {
            // Route around LEFT side: up → left → down
            waypoints.push_back(ImVec2(ctx.StartPin.Position.x, topClearY));      // 1. Up from output
            waypoints.push_back(ImVec2(leftClearX, topClearY));                   // 2. Left to clear block
            waypoints.push_back(ImVec2(leftClearX, bottomClearY));                // 3. Down below block
            waypoints.push_back(ImVec2(ctx.EndPin.Position.x, bottomClearY));     // 4. Right to input
        }
        else
        {
            // Route around RIGHT side: up → right → down
            waypoints.push_back(ImVec2(ctx.StartPin.Position.x, topClearY));      // 1. Up from output
            waypoints.push_back(ImVec2(rightClearX, topClearY));                  // 2. Right to clear block
            waypoints.push_back(ImVec2(rightClearX, bottomClearY));               // 3. Down below block
            waypoints.push_back(ImVec2(ctx.EndPin.Position.x, bottomClearY));     // 4. Left to input
        }
    }
    // Case 5: Mixed cases (e.g., side to top/bottom, or same-side connections)
    else
    {
        // For other combinations, use a generic routing strategy
        // Determine routing based on which way is shorter/cleaner
        bool routeHorizontalFirst = (startEdge == PinEdge::Left || startEdge == PinEdge::Right);
        
        if (routeHorizontalFirst)
        {
            // Start from side, route around top or bottom
            bool routeAbove = (ctx.EndPin.Position.y < ctx.StartPin.Position.y);
            float clearY = routeAbove ? topClearY : bottomClearY;
            float clearX = (startEdge == PinEdge::Right) ? rightClearX : leftClearX;
            
            waypoints.push_back(ImVec2(clearX, ctx.StartPin.Position.y));   // 1. Out from side
            waypoints.push_back(ImVec2(clearX, clearY));                    // 2. Up/down to clearance
            waypoints.push_back(ImVec2(ctx.EndPin.Position.x, clearY));     // 3. Across to target X
        }
        else
        {
            // Start from top/bottom, route around left or right
            bool routeLeft = (ctx.EndPin.Position.x < ctx.StartPin.Position.x);
            float clearX = routeLeft ? leftClearX : rightClearX;
            float clearY = (startEdge == PinEdge::Bottom) ? bottomClearY : topClearY;
            
            waypoints.push_back(ImVec2(ctx.StartPin.Position.x, clearY));   // 1. Out from top/bottom
            waypoints.push_back(ImVec2(clearX, clearY));                    // 2. Left/right to clearance
            waypoints.push_back(ImVec2(clearX, ctx.EndPin.Position.y));     // 3. Up/down to target Y
        }
    }
    
    return waypoints;
}

std::vector<ImVec2> RouteZShape(const RoutingContext& ctx, float horizontalY)
{
    // Simple Z-shape: down from start -> across at horizontalY -> up to end
    // Check if horizontal segment would intersect end node or obstacles
    ImVec2 waypoint1(ctx.StartPin.Position.x, horizontalY);
    ImVec2 waypoint2(ctx.EndPin.Position.x, horizontalY);
    
    // Check if the horizontal segment intersects the end node
    if (HorizontalSegmentIntersectsNode(ctx.EndNode, horizontalY, waypoint1.x, waypoint2.x))
    {
        // Would intersect - need to route around
        return RouteAroundObstacles(ctx, DeterminePreferredSide(ctx));
    }
    
    // Check if horizontal segment intersects any obstacles
    if (SegmentIntersectsObstacles(waypoint1, waypoint2, ctx))
    {
        // Would intersect obstacles - need to route around
        return RouteAroundObstacles(ctx, DeterminePreferredSide(ctx));
    }
    
    // Simple Z-shape is safe
    std::vector<ImVec2> result;
    result.push_back(waypoint1); // 1. Down from start
    result.push_back(waypoint2); // 2. Across to end's X
    return result;
}

std::vector<ImVec2> RouteUShape(const RoutingContext& ctx, bool routeAbove)
{
    std::vector<ImVec2> waypoints;
    
    if (routeAbove)
    {
        // Route above blocks
        bool pinAtTop = PinIsAtTop(ctx.EndPin, ctx.EndNode);
        bool nodesOverlapX = NodesOverlapX(ctx.StartNode, ctx.EndNode);
        
        if (pinAtTop && !nodesOverlapX)
        {
            // Pin is at top and nodes are side by side - route around to approach from above
            bool routeLeft = DeterminePreferredSide(ctx);
            float horizontalY = ctx.EndNode.Min.y - Constants::MIN_ABOVE_BLOCK_CLEARANCE;
            
            if (routeLeft)
            {
                // Route around LEFT side: down -> left -> up -> right
                float clearX = CalculateVerticalClearanceX(ctx, true);
                float clearY = std::max(ctx.StartNode.Max.y + Constants::MIN_PARALLEL_CLEARANCE, ctx.EndNode.Max.y + ctx.Margin);
                
                waypoints.push_back(ImVec2(ctx.StartPin.Position.x, clearY));  // 1. Down from start
                waypoints.push_back(ImVec2(clearX, clearY));                    // 2. Left to clear both nodes
                waypoints.push_back(ImVec2(clearX, horizontalY));                // 3. Up to horizontal clearance level
                
                // Check if final horizontal segment would intersect start node, end node, or obstacles
                Obstacle startObstacle = {ctx.StartNode.Min, ctx.StartNode.Max};
                Obstacle endObstacle = {ctx.EndNode.Min, ctx.EndNode.Max};
                ImVec2 finalSegStart(clearX, horizontalY);
                ImVec2 finalSegEnd(ctx.EndPin.Position.x, horizontalY);
                float finalSegMinX = std::min(clearX, ctx.EndPin.Position.x);
                float finalSegMaxX = std::max(clearX, ctx.EndPin.Position.x);
                bool intersectsStart = startObstacle.IntersectsHorizontalLine(horizontalY, finalSegMinX, finalSegMaxX);
                bool intersectsEnd = endObstacle.IntersectsHorizontalLine(horizontalY, finalSegMinX, finalSegMaxX);
                bool intersectsObstacles = SegmentIntersectsObstacles(finalSegStart, finalSegEnd, ctx);
                
                if (intersectsStart || intersectsEnd || intersectsObstacles)
                {
                    // Need to clear start/end node - add intermediate waypoint
                    if (intersectsEnd)
                    {
                        // Route around end node: go to the right of end node first
                        float intermediateX = ctx.EndNode.Max.x + ctx.Margin;
                        waypoints.push_back(ImVec2(intermediateX, horizontalY));  // 4. Right to clear end node
                        waypoints.push_back(ImVec2(ctx.EndPin.Position.x, horizontalY)); // 5. Left to end pin
                    }
                    else if (intersectsStart)
                    {
                        // Only start node intersects - clear it
                        float intermediateX = ctx.StartNode.Max.x + ctx.Margin;
                        waypoints.push_back(ImVec2(intermediateX, horizontalY));  // 4. Right to clear start node
                        waypoints.push_back(ImVec2(ctx.EndPin.Position.x, horizontalY)); // 5. Left to end pin
                    }
                    else
                    {
                        // Only obstacles intersect - route around them
                        float intermediateX = ctx.StartNode.Max.x + ctx.Margin;
                        waypoints.push_back(ImVec2(intermediateX, horizontalY));  // 4. Right to clear obstacles
                        waypoints.push_back(ImVec2(ctx.EndPin.Position.x, horizontalY)); // 5. Left to end pin
                    }
                }
                else
                {
                    waypoints.push_back(ImVec2(ctx.EndPin.Position.x, horizontalY)); // 4. Right to end pin
                }
            }
            else
            {
                // Route around RIGHT side: down -> right -> up -> left
                float clearX = CalculateVerticalClearanceX(ctx, false);
                float clearY = std::max(ctx.StartNode.Max.y + Constants::MIN_PARALLEL_CLEARANCE, ctx.EndNode.Max.y + ctx.Margin);
                
                waypoints.push_back(ImVec2(ctx.StartPin.Position.x, clearY));  // 1. Down from start
                waypoints.push_back(ImVec2(clearX, clearY));                    // 2. Right to clear both nodes
                waypoints.push_back(ImVec2(clearX, horizontalY));               // 3. Up to horizontal clearance level
                
                // Check if final horizontal segment would intersect
                Obstacle startObstacle = {ctx.StartNode.Min, ctx.StartNode.Max};
                Obstacle endObstacle = {ctx.EndNode.Min, ctx.EndNode.Max};
                ImVec2 finalSegStart(clearX, horizontalY);
                ImVec2 finalSegEnd(ctx.EndPin.Position.x, horizontalY);
                float finalSegMinX = std::min(clearX, ctx.EndPin.Position.x);
                float finalSegMaxX = std::max(clearX, ctx.EndPin.Position.x);
                bool intersectsStart = startObstacle.IntersectsHorizontalLine(horizontalY, finalSegMinX, finalSegMaxX);
                bool intersectsEnd = endObstacle.IntersectsHorizontalLine(horizontalY, finalSegMinX, finalSegMaxX);
                bool intersectsObstacles = SegmentIntersectsObstacles(finalSegStart, finalSegEnd, ctx);
                
                if (intersectsStart || intersectsEnd || intersectsObstacles)
                {
                    if (intersectsEnd)
                    {
                        float intermediateX = ctx.EndNode.Min.x - ctx.Margin;
                        waypoints.push_back(ImVec2(intermediateX, horizontalY));  // 4. Left to clear end node
                        waypoints.push_back(ImVec2(ctx.EndPin.Position.x, horizontalY)); // 5. Right to end pin
                    }
                    else if (intersectsStart)
                    {
                        float intermediateX = ctx.StartNode.Min.x - ctx.Margin;
                        waypoints.push_back(ImVec2(intermediateX, horizontalY));  // 4. Left to clear start node
                        waypoints.push_back(ImVec2(ctx.EndPin.Position.x, horizontalY)); // 5. Right to end pin
                    }
                    else
                    {
                        float intermediateX = ctx.StartNode.Min.x - ctx.Margin;
                        waypoints.push_back(ImVec2(intermediateX, horizontalY));  // 4. Left to clear obstacles
                        waypoints.push_back(ImVec2(ctx.EndPin.Position.x, horizontalY)); // 5. Right to end pin
                    }
                }
                else
                {
                    waypoints.push_back(ImVec2(ctx.EndPin.Position.x, horizontalY)); // 4. Left to end pin
                }
            }
        }
        else
        {
            // Default: route below both blocks (U-shape)
            float bottomY = std::max(ctx.StartNode.Max.y, ctx.EndNode.Max.y) + ctx.Margin;
            waypoints.push_back(ImVec2(ctx.StartPin.Position.x, bottomY));   // Down from start
            waypoints.push_back(ImVec2(ctx.EndPin.Position.x, bottomY));     // Across below both blocks
            // Then naturally goes up to end
        }
    }
    else
    {
        // Route below both blocks (U-shape)
        float bottomY = std::max(ctx.StartNode.Max.y, ctx.EndNode.Max.y) + ctx.Margin;
        waypoints.push_back(ImVec2(ctx.StartPin.Position.x, bottomY));   // Down from start
        waypoints.push_back(ImVec2(ctx.EndPin.Position.x, bottomY));     // Across below both blocks
    }
    
    return waypoints;
}

std::vector<ImVec2> RouteHorizontalFlow(const RoutingContext& ctx)
{
    // Horizontal flow (e.g. flow pins on block sides)
    bool startFlowsRight = ctx.StartPin.Direction.x > 0;
    bool endAcceptsFromLeft = ctx.EndPin.Direction.x < 0;
    
    std::vector<ImVec2> waypoints;
    
    if (startFlowsRight && endAcceptsFromLeft)
    {
        // Right output → Left input
        if (ctx.EndPin.Position.x > ctx.StartPin.Position.x)
        {
            // Simple case: end is to the right
            if (std::abs(ctx.EndPin.Position.y - ctx.StartPin.Position.y) > Constants::HORIZONTAL_FLOW_HEIGHT_THRESHOLD)
            {
                // Different heights - use simple Z-shape with 2 waypoints
                float midX = (ctx.StartPin.Position.x + ctx.EndPin.Position.x) * 0.5f;
                waypoints.push_back(ImVec2(midX, ctx.StartPin.Position.y));  // 1. Horizontal right from start
                waypoints.push_back(ImVec2(midX, ctx.EndPin.Position.y));    // 2. Vertical to end height
                // Final segment from waypoint 2 to endPos is automatic and horizontal
            }
            // else: straight line, no waypoints needed (return empty)
        }
        else
        {
            // Complex case: end is to the LEFT (backward flow)
            // Need proper U-shape with 4 waypoints that routes AROUND the blocks
            
            // Find the rightmost and leftmost edges of BOTH nodes
            float rightX = std::max(ctx.StartNode.Max.x, ctx.EndNode.Max.x) + ctx.Margin;
            float leftX = std::min(ctx.StartNode.Min.x, ctx.EndNode.Min.x) - ctx.Margin;
            
            // Determine U height (go above or below both nodes)
            float topY = std::min(ctx.StartNode.Min.y, ctx.EndNode.Min.y) - ctx.Margin;    // Above both blocks
            float bottomY = std::max(ctx.StartNode.Max.y, ctx.EndNode.Max.y) + ctx.Margin; // Below both blocks
            
            // Choose top or bottom U based on available space
            float uY = bottomY;  // Default to bottom U-shape (prefer bottom line routing)
            
            // 4 waypoints for clean U-shape with proper clearance:
            waypoints.push_back(ImVec2(rightX, ctx.StartPin.Position.y));  // 1. Go right from start (clear of right block)
            waypoints.push_back(ImVec2(rightX, uY));                        // 2. Go up/down to U height (clear of both blocks)
            waypoints.push_back(ImVec2(leftX, uY));                         // 3. Go across the top/bottom (clear path)
            waypoints.push_back(ImVec2(leftX, ctx.EndPin.Position.y));      // 4. Go down/up to end (clear of left block)
        }
    }
    else
    {
        // Other horizontal cases (left→right, etc.)
        float sideX = (startFlowsRight) 
            ? std::max(ctx.StartPin.Position.x, ctx.EndPin.Position.x) + ctx.Margin
            : std::min(ctx.StartPin.Position.x, ctx.EndPin.Position.x) - ctx.Margin;
        
        waypoints.push_back(ImVec2(sideX, ctx.StartPin.Position.y));  // Out to side
        waypoints.push_back(ImVec2(sideX, ctx.EndPin.Position.y));    // Along side
    }
    
    return waypoints;
}

std::vector<ImVec2> RouteLShape(const RoutingContext& ctx)
{
    // Simple L-shape fallback
    float midY = (ctx.StartPin.Position.y + ctx.EndPin.Position.y) * 0.5f;
    std::vector<ImVec2> result;
    result.push_back(ImVec2(ctx.StartPin.Position.x, midY));
    result.push_back(ImVec2(ctx.EndPin.Position.x, midY));
    return result;
}

std::vector<ImVec2> RouteAroundObstacles(const RoutingContext& ctx, bool preferLeft)
{
    // Complex routing around obstacles
    // This handles cases where a simple Z-shape or U-shape would intersect nodes/obstacles
    
    std::vector<ImVec2> waypoints;
    
    // Check if start is within end node's X range
    bool startWithinEndNodeX = (ctx.StartPin.Position.x >= ctx.EndNode.Min.x && 
                                 ctx.StartPin.Position.x <= ctx.EndNode.Max.x);
    
    // Check if pin is at top of block
    bool pinAtTop = PinIsAtTop(ctx.EndPin, ctx.EndNode);
    
    if (startWithinEndNodeX && !pinAtTop)
    {
        // Start is within end node's X range AND pin is at bottom
        // Route below both nodes (simple 2-waypoint path)
        float clearY = std::max(ctx.StartNode.Max.y, ctx.EndNode.Max.y) + ctx.Margin;
        waypoints.push_back(ImVec2(ctx.StartPin.Position.x, clearY));  // 1. Down from start
        waypoints.push_back(ImVec2(ctx.EndPin.Position.x, clearY));    // 2. Across to end's X
    }
    else
    {
        // Start is outside end node's X range - route around the side
        bool routeLeft = preferLeft;
        float horizontalY = ctx.EndNode.Min.y - Constants::MIN_ABOVE_BLOCK_CLEARANCE;
        
        if (routeLeft)
        {
            // Route around LEFT side
            // Ensure clearX clears BOTH start and end nodes
            float clearX = std::min(ctx.EndNode.Min.x, ctx.StartNode.Min.x) - ctx.Margin;
            
            if (pinAtTop)
            {
                // Pin is at top: path - down -> left -> up -> [check start node] -> right
                float clearY = std::max(ctx.StartNode.Max.y + Constants::MIN_PARALLEL_CLEARANCE, ctx.EndNode.Max.y + ctx.Margin);
                waypoints.push_back(ImVec2(ctx.StartPin.Position.x, clearY));      // 1. Down from start
                waypoints.push_back(ImVec2(clearX, clearY));                       // 2. Left to clear both nodes' left edges
                waypoints.push_back(ImVec2(clearX, horizontalY));                   // 3. Up along left edge to horizontal clearance level
                
                // Check if horizontal segment from clearX to endPos.x would intersect start node or obstacles
                ImVec2 finalSegStart(clearX, horizontalY);
                ImVec2 finalSegEnd(ctx.EndPin.Position.x, horizontalY);
                bool intersectsStart = HorizontalSegmentIntersectsNode(ctx.StartNode, horizontalY, clearX, ctx.EndPin.Position.x);
                bool intersectsObstacles = SegmentIntersectsObstacles(finalSegStart, finalSegEnd, ctx);
                
                if (intersectsStart || intersectsObstacles)
                {
                    // Final horizontal segment would pass through start node - need to clear it first
                    float intermediateX = ctx.StartNode.Max.x + ctx.Margin;
                    waypoints.push_back(ImVec2(intermediateX, horizontalY));  // 4. Right to clear start node
                    waypoints.push_back(ImVec2(ctx.EndPin.Position.x, horizontalY)); // 5. Left to end pin's X
                }
                else
                {
                    // No intersection - can go directly to end pin
                    waypoints.push_back(ImVec2(ctx.EndPin.Position.x, horizontalY)); // 4. Right to end pin's X
                }
            }
            else
            {
                // Pin is at bottom: route below the block
                float clearY = std::max(ctx.StartNode.Max.y, ctx.EndNode.Max.y) + ctx.Margin;
                waypoints.push_back(ImVec2(ctx.StartPin.Position.x, clearY));      // 1. Down from start
                waypoints.push_back(ImVec2(clearX, clearY));                       // 2. Left to clear both nodes' left edges
                waypoints.push_back(ImVec2(clearX, ctx.EndPin.Position.y));      // 3. Up to pin's Y level
                
                // Check if horizontal segment would intersect start node or obstacles
                ImVec2 finalSegStart(clearX, ctx.EndPin.Position.y);
                ImVec2 finalSegEnd(ctx.EndPin.Position.x, ctx.EndPin.Position.y);
                bool intersectsStart = HorizontalSegmentIntersectsNode(ctx.StartNode, ctx.EndPin.Position.y, clearX, ctx.EndPin.Position.x);
                bool intersectsObstacles = SegmentIntersectsObstacles(finalSegStart, finalSegEnd, ctx);
                
                if (intersectsStart || intersectsObstacles)
                {
                    // Need to clear start node
                    float intermediateX = ctx.StartNode.Max.x + ctx.Margin;
                    waypoints.push_back(ImVec2(intermediateX, ctx.EndPin.Position.y));    // 4. Right to clear start node
                    waypoints.push_back(ImVec2(ctx.EndPin.Position.x, ctx.EndPin.Position.y)); // 5. Left to end pin's X
                }
                else
                {
                    waypoints.push_back(ImVec2(ctx.EndPin.Position.x, ctx.EndPin.Position.y)); // 4. Right to end pin's X
                }
            }
        }
        else
        {
            // Route around RIGHT side
            float clearX = std::max(ctx.EndNode.Max.x, ctx.StartNode.Max.x) + ctx.Margin;
            
            if (pinAtTop)
            {
                // Pin is at top: path - down -> right -> up -> [check start node] -> left
                float clearY = std::max(ctx.StartNode.Max.y + Constants::MIN_PARALLEL_CLEARANCE, ctx.EndNode.Max.y + ctx.Margin);
                waypoints.push_back(ImVec2(ctx.StartPin.Position.x, clearY));      // 1. Down from start
                waypoints.push_back(ImVec2(clearX, clearY));                       // 2. Right to clear both nodes' right edges
                waypoints.push_back(ImVec2(clearX, horizontalY));                  // 3. Up along right edge to horizontal clearance level
                
                // Check if horizontal segment from clearX to endPos.x would intersect start node or obstacles
                ImVec2 finalSegStart(clearX, horizontalY);
                ImVec2 finalSegEnd(ctx.EndPin.Position.x, horizontalY);
                bool intersectsStart = HorizontalSegmentIntersectsNode(ctx.StartNode, horizontalY, clearX, ctx.EndPin.Position.x);
                bool intersectsObstacles = SegmentIntersectsObstacles(finalSegStart, finalSegEnd, ctx);
                
                if (intersectsStart || intersectsObstacles)
                {
                    // Final horizontal segment would pass through start node - need to clear it first
                    float intermediateX = ctx.StartNode.Min.x - ctx.Margin;
                    waypoints.push_back(ImVec2(intermediateX, horizontalY));  // 4. Left to clear start node
                    waypoints.push_back(ImVec2(ctx.EndPin.Position.x, horizontalY)); // 5. Right to end pin's X
                }
                else
                {
                    // No intersection - can go directly to end pin
                    waypoints.push_back(ImVec2(ctx.EndPin.Position.x, horizontalY)); // 4. Left to end pin's X
                }
            }
            else
            {
                // Pin is at bottom: route below the block
                float clearY = std::max(ctx.StartNode.Max.y, ctx.EndNode.Max.y) + ctx.Margin;
                waypoints.push_back(ImVec2(ctx.StartPin.Position.x, clearY));     // 1. Down from start
                waypoints.push_back(ImVec2(clearX, clearY));                       // 2. Right to clear both nodes' right edges
                waypoints.push_back(ImVec2(clearX, ctx.EndPin.Position.y));       // 3. Up to pin's Y level
                
                // Check if horizontal segment would intersect start node or obstacles
                ImVec2 finalSegStart(clearX, ctx.EndPin.Position.y);
                ImVec2 finalSegEnd(ctx.EndPin.Position.x, ctx.EndPin.Position.y);
                bool intersectsStart = HorizontalSegmentIntersectsNode(ctx.StartNode, ctx.EndPin.Position.y, clearX, ctx.EndPin.Position.x);
                bool intersectsObstacles = SegmentIntersectsObstacles(finalSegStart, finalSegEnd, ctx);
                
                if (intersectsStart || intersectsObstacles)
                {
                    // Need to clear start node
                    float intermediateX = ctx.StartNode.Min.x - ctx.Margin;
                    waypoints.push_back(ImVec2(intermediateX, ctx.EndPin.Position.y));     // 4. Left to clear start node
                    waypoints.push_back(ImVec2(ctx.EndPin.Position.x, ctx.EndPin.Position.y)); // 5. Right to end pin's X
                }
                else
                {
                    waypoints.push_back(ImVec2(ctx.EndPin.Position.x, ctx.EndPin.Position.y)); // 4. Left to end pin's X
                }
            }
        }
    }
    
    return waypoints;
}

//-----------------------------------------------------------------------------
// Main Entry Point (New Context-Based API)
//-----------------------------------------------------------------------------

std::vector<ImVec2> GenerateWaypoints(const RoutingContext& ctx)
{
    // Strategy selection
    auto strategy = SelectStrategy(ctx);
    
    // Dispatch to appropriate routing function (first pass)
    std::vector<ImVec2> waypoints;
    switch (strategy)
    {
        case RoutingStrategy::SameBlock:
            waypoints = RouteSameBlock(ctx);
            break;
        case RoutingStrategy::ZShape:
            waypoints = RouteZShape(ctx, CalculateHorizontalClearanceY(ctx, false));
            break;
        case RoutingStrategy::ZShapeAboveBlock:
            waypoints = RouteZShape(ctx, CalculateHorizontalClearanceY(ctx, true));
            break;
        case RoutingStrategy::UShapeAbove:
            waypoints = RouteUShape(ctx, true);
            break;
        case RoutingStrategy::UShapeBelow:
            waypoints = RouteUShape(ctx, false);
            break;
        case RoutingStrategy::HorizontalFlow:
            waypoints = RouteHorizontalFlow(ctx);
            break;
        case RoutingStrategy::AroundObstacles:
            waypoints = RouteAroundObstacles(ctx, DeterminePreferredSide(ctx));
            break;
        case RoutingStrategy::LShape:
            waypoints = RouteLShape(ctx);
            break;
        case RoutingStrategy::Direct:
            waypoints = {};  // No waypoints needed
            break;
        default:
            waypoints = RouteLShape(ctx);  // Fallback
            break;
    }
    
    // Multi-pass refinement: Apply each refinement pass in sequence
    for (size_t i = 0; i < ctx.RefinementPasses.size(); ++i)
    {
        const auto& pass = ctx.RefinementPasses[i];
        if (pass.Callback != nullptr)
        {
#ifdef WAYPOINT_DEBUG
            PathfindingDebug("[GenerateWaypoints] Applying refinement pass {}/{}: {} (waypoints before: {})",
                              i + 1, ctx.RefinementPasses.size(),
                              pass.Name ? pass.Name : "unnamed",
                              waypoints.size());
#endif
            waypoints = pass.Callback(waypoints, ctx, pass.UserData);
#ifdef WAYPOINT_DEBUG
            PathfindingDebug("[GenerateWaypoints] Refinement pass {} complete (waypoints after: {})",
                              i + 1, waypoints.size());
#endif
        }
    }
    
    return waypoints;
}

//-----------------------------------------------------------------------------
// Multi-Pass Waypoint Refinement (Stub Implementations)
//-----------------------------------------------------------------------------

// Example usage of multi-pass refinement:
//
//   // Define custom refinement functions
//   std::vector<ImVec2> MyPass1(const std::vector<ImVec2>& waypoints, const RoutingContext& ctx, void* userData)
//   {
//       // First pass: detect link crossings
//       return modifiedWaypoints;
//   }
//
//   std::vector<ImVec2> MyPass2(const std::vector<ImVec2>& waypoints, const RoutingContext& ctx, void* userData)
//   {
//       // Second pass: bundle parallel links
//       return modifiedWaypoints;
//   }
//
//   // Configure refinement pipeline
//   RoutingContext ctx;
//   ctx.AddRefinementPass(MyPass1, myData1, "Avoid Intersections");
//   ctx.AddRefinementPass(MyPass2, myData2, "Bundle Links");
//   ctx.AddRefinementPass(RefinementPass_SmoothPath, nullptr, "Smooth Path");
//   auto waypoints = GenerateWaypoints(ctx);
//
//   // The passes will run in order: Initial → MyPass1 → MyPass2 → SmoothPath → Final

std::vector<ImVec2> RefinementPass_AvoidLinkIntersections(
    const std::vector<ImVec2>& inputWaypoints,
    const RoutingContext& ctx,
    void* userData)
{
    // STUB: Link intersection avoidance
    // 
    // Implementation plan:
    // 1. Access ctx.Container->GetAllLinks() to get existing links
    // 2. For each segment in inputWaypoints, check if it crosses existing links
    // 3. If crossing detected, offset the waypoint perpendicular to the segment
    // 4. Adjust clearance based on link density in the area
    // 5. Maintain minimum distance from other links
    
#ifdef WAYPOINT_DEBUG
    PathfindingDebug("[RefinementPass_AvoidLinkIntersections] Processing {} waypoints", inputWaypoints.size());
    if (ctx.Container)
    {
        // Example: Access container data
        // PathfindingDebug("[RefinementPass_AvoidLinkIntersections] Container has links to analyze");
    }
#endif
    
    // For now, return unchanged
    return inputWaypoints;
}

std::vector<ImVec2> RefinementPass_BundleParallelLinks(
    const std::vector<ImVec2>& inputWaypoints,
    const RoutingContext& ctx,
    void* userData)
{
    // STUB: Link bundling
    // 
    // Implementation plan:
    // 1. Detect parallel links going between same node pairs
    // 2. Calculate offset direction perpendicular to flow
    // 3. Group links into bundles (e.g., 2-3 links per bundle)
    // 4. Offset each link in bundle by small amount (e.g., 8-10 pixels)
    // 5. Maintain consistent bundle ordering across graph
    
#ifdef WAYPOINT_DEBUG
    PathfindingDebug("[RefinementPass_BundleParallelLinks] Processing {} waypoints", inputWaypoints.size());
#endif
    
    // For now, return unchanged
    return inputWaypoints;
}

std::vector<ImVec2> RefinementPass_SmoothPath(
    const std::vector<ImVec2>& inputWaypoints,
    const RoutingContext& ctx,
    void* userData)
{
#ifdef ENABLE_LINK_FITTING
    // Link fitting: Align horizontal segments that are close together
    // This is CONTAINER-AWARE: analyzes ALL links to find alignment opportunities
    
    std::vector<ImVec2> refinedWaypoints = inputWaypoints;
    
    // Build full path including start and end pins for segment analysis
    std::vector<ImVec2> fullPath;
    fullPath.push_back(ctx.StartPin.Position);
    fullPath.insert(fullPath.end(), refinedWaypoints.begin(), refinedWaypoints.end());
    fullPath.push_back(ctx.EndPin.Position);
    
    // Identify horizontal segments (segments where Y is constant, X changes)
    struct HorizontalSegment {
        int pathIdx;   // Which path: -1 = current link, >= 0 = other link index
        int startIdx;  // Index in path
        int endIdx;    // Index in path
        float y;       // Y coordinate
        float xMin, xMax;
        ImVec2 start, end;  // Actual positions
    };
    
    std::vector<HorizontalSegment> allHorizontalSegments;
    
    // Add horizontal segments from CURRENT link being processed
    for (size_t i = 0; i < fullPath.size() - 1; ++i)
    {
        const ImVec2& p1 = fullPath[i];
        const ImVec2& p2 = fullPath[i + 1];
        
        // Check if segment is horizontal (same Y, different X)
        if (std::abs(p1.y - p2.y) < 1.0f && std::abs(p1.x - p2.x) > 1.0f)
        {
            HorizontalSegment seg;
            seg.pathIdx = -1;  // Current link
            seg.startIdx = static_cast<int>(i);
            seg.endIdx = static_cast<int>(i + 1);
            seg.y = (p1.y + p2.y) * 0.5f;
            seg.xMin = std::min(p1.x, p2.x);
            seg.xMax = std::max(p1.x, p2.x);
            seg.start = p1;
            seg.end = p2;
            allHorizontalSegments.push_back(seg);
        }
    }
    
    // Add horizontal segments from OTHER links in container (context-aware!)
    if (ctx.Container)
    {
        // Get RootContainer to access GetAllLinks()
        auto* rootContainer = ctx.Container->GetRootContainer();
        if (rootContainer)
        {
            // Access all links via GetAllLinks() method
            auto allLinks = rootContainer->GetAllLinks();
            
            for (auto* linkPtr : allLinks)
        {
            if (!linkPtr) continue;
            
            // Get waypoints for this link
            int wpCount = ed::GetLinkControlPointCount(linkPtr->ID);
            if (wpCount <= 0) continue;
            
            std::vector<ImVec2> otherWaypoints(wpCount);
            ed::GetLinkControlPoints(linkPtr->ID, otherWaypoints.data(), wpCount);
            
            // Build path for this link (need to find its pins)
            // For now, just analyze waypoint-to-waypoint segments
            for (int i = 0; i < wpCount - 1; ++i)
            {
                const ImVec2& p1 = otherWaypoints[i];
                const ImVec2& p2 = otherWaypoints[i + 1];
                
                if (std::abs(p1.y - p2.y) < 1.0f && std::abs(p1.x - p2.x) > 1.0f)
                {
                    HorizontalSegment seg;
                    seg.pathIdx = static_cast<int>(allLinks.size());  // Mark as other link
                    seg.startIdx = i;
                    seg.endIdx = i + 1;
                    seg.y = (p1.y + p2.y) * 0.5f;
                    seg.xMin = std::min(p1.x, p2.x);
                    seg.xMax = std::max(p1.x, p2.x);
                    seg.start = p1;
                    seg.end = p2;
                    allHorizontalSegments.push_back(seg);
                }
            }
        }
        }
    }
    
#ifdef WAYPOINT_DEBUG
    PathfindingDebug("[RefinementPass_SmoothPath] Found {} horizontal segments (current + other links)",
                     allHorizontalSegments.size());
#endif
    
    // Group horizontal segments that are within tolerance
    std::vector<bool> processed(allHorizontalSegments.size(), false);
    
    for (size_t i = 0; i < allHorizontalSegments.size(); ++i)
    {
        if (processed[i])
            continue;
        
        // Only process segments from CURRENT link (we can't modify other links)
        if (allHorizontalSegments[i].pathIdx != -1)
        {
            processed[i] = true;
            continue;
        }
        
        std::vector<size_t> group;
        group.push_back(i);
        processed[i] = true;
        
        // Find all segments (from ANY link) within tolerance
        for (size_t j = 0; j < allHorizontalSegments.size(); ++j)
        {
            if (processed[j] || i == j)
                continue;
            
            // Check if segments are within vertical tolerance
            float yDiff = std::abs(allHorizontalSegments[i].y - allHorizontalSegments[j].y);
            if (yDiff <= HORIZONTAL_ALIGNMENT_TOLERANCE)
            {
                group.push_back(j);
                processed[j] = true;
            }
        }
        
        // If we have multiple segments in the group, align them
        if (group.size() >= 2)
        {
            // Use EXISTING link's Y as anchor (don't use average!)
            // If there are segments from other links, align to the first one found
            // Otherwise, use the current link's Y
            float targetY = allHorizontalSegments[group[0]].y;
            
            // Find first segment from an EXISTING link (not current link) to use as anchor
            for (size_t idx : group)
            {
                if (allHorizontalSegments[idx].pathIdx != -1)  // Existing link
                {
                    targetY = allHorizontalSegments[idx].y;
                    break;  // Use first existing link as anchor
                }
            }
            
#ifdef WAYPOINT_DEBUG
            PathfindingDebug("[RefinementPass_SmoothPath] Aligning {} horizontal segments to Y={:.1f}",
                              group.size(), targetY);
            for (size_t idx : group)
            {
                PathfindingDebug("  - Segment {}: Y={:.1f} (current link: {})",
                                  idx, allHorizontalSegments[idx].y,
                                  allHorizontalSegments[idx].pathIdx == -1 ? "YES" : "no");
            }
#endif
            
            // Apply alignment ONLY to waypoints in current link (fullPath)
            for (size_t idx : group)
            {
                const HorizontalSegment& seg = allHorizontalSegments[idx];
                if (seg.pathIdx == -1)  // Only modify current link
                {
                    fullPath[seg.startIdx].y = targetY;
                    fullPath[seg.endIdx].y = targetY;
                }
            }
        }
    }
    
    // Extract waypoints back (exclude start/end pins)
    refinedWaypoints.clear();
    for (size_t i = 1; i < fullPath.size() - 1; ++i)
    {
        refinedWaypoints.push_back(fullPath[i]);
    }
    
    return refinedWaypoints;
    
#else
    // Link fitting disabled - return unchanged
    
#ifdef WAYPOINT_DEBUG
    PathfindingDebug("[RefinementPass_SmoothPath] Processing {} waypoints (ENABLE_LINK_FITTING not defined)",
                     inputWaypoints.size());
#endif
    
    return inputWaypoints;
#endif
}

std::vector<ImVec2> RefinementPass_OptimizeObstacles(
    const std::vector<ImVec2>& inputWaypoints,
    const RoutingContext& ctx,
    void* userData)
{
    // STUB: Dynamic obstacle optimization
    // 
    // Implementation plan:
    // 1. Use ctx.Obstacles to get real-time node positions
    // 2. For each waypoint, check if it's too close to any obstacle
    // 3. If too close, push waypoint away maintaining path connectivity
    // 4. Use ctx.EditorContext to access zoom level for scale-aware adjustments
    // 5. Optimize clearance based on available space
    
#ifdef WAYPOINT_DEBUG
    PathfindingDebug("[RefinementPass_OptimizeObstacles] Processing {} waypoints, {} obstacles",
                     inputWaypoints.size(), ctx.Obstacles.size());
#endif
    
    // For now, return unchanged
    return inputWaypoints;
}

std::vector<ImVec2> RefinementPass_AutoCollapse(
    const std::vector<ImVec2>& inputWaypoints,
    const RoutingContext& ctx,
    void* userData)
{
#ifdef ENABLE_LINK_AUTO_COLLAPSE
    // Auto-collapse: Remove waypoints when they're too close together
    // This simplifies the link to a straight line when waypoints don't add value
    
    // Need at least 1 waypoint to consider collapsing
    if (inputWaypoints.empty())
        return inputWaypoints;
    
#ifdef WAYPOINT_DEBUG
    PathfindingDebug("[RefinementPass_AutoCollapse] Input: {} waypoints, collapse radius: {:.1f}",
                     inputWaypoints.size(), COLLAPSE_RADIUS);
#endif
    
    // Check if all waypoints are within the collapse radius
    // Calculate bounding box of all waypoints
    ImVec2 minBounds = inputWaypoints[0];
    ImVec2 maxBounds = inputWaypoints[0];
    
    for (const auto& wp : inputWaypoints)
    {
        minBounds.x = std::min(minBounds.x, wp.x);
        minBounds.y = std::min(minBounds.y, wp.y);
        maxBounds.x = std::max(maxBounds.x, wp.x);
        maxBounds.y = std::max(maxBounds.y, wp.y);
    }
    
    // Calculate bounding box dimensions
    float width = maxBounds.x - minBounds.x;
    float height = maxBounds.y - minBounds.y;
    float maxDimension = std::max(width, height);
    
    // If all waypoints fit within the collapse radius, remove them (collapse to straight)
    if (maxDimension <= COLLAPSE_RADIUS)
    {
#ifdef WAYPOINT_DEBUG
        PathfindingDebug("[RefinementPass_AutoCollapse] Collapsing {} waypoints (max dimension: {:.1f} <= {:.1f})",
                         inputWaypoints.size(), maxDimension, COLLAPSE_RADIUS);
        PathfindingDebug("[RefinementPass_AutoCollapse] Bounding box: ({:.1f}, {:.1f}) to ({:.1f}, {:.1f})",
                         minBounds.x, minBounds.y, maxBounds.x, maxBounds.y);
#endif
        
        // Return empty waypoints = straight line
        return std::vector<ImVec2>();
    }
    
    // Also check if waypoints are nearly collinear (all on a straight line)
    // If start pin, all waypoints, and end pin are collinear, collapse to straight
    if (inputWaypoints.size() >= 2)
    {
        // Build full path including pins
        std::vector<ImVec2> fullPath;
        fullPath.push_back(ctx.StartPin.Position);
        fullPath.insert(fullPath.end(), inputWaypoints.begin(), inputWaypoints.end());
        fullPath.push_back(ctx.EndPin.Position);
        
        // Check if all points are collinear (within tolerance)
        bool allCollinear = true;
        
        // Calculate direction from start to end
        ImVec2 overallDir = ctx.EndPin.Position - ctx.StartPin.Position;
        float overallLength = std::sqrt(overallDir.x * overallDir.x + overallDir.y * overallDir.y);
        
        if (overallLength > 0.1f)
        {
            overallDir.x /= overallLength;
            overallDir.y /= overallLength;
            
            // Check perpendicular distance from line for each waypoint
            for (const auto& wp : inputWaypoints)
            {
                ImVec2 toPoint = wp - ctx.StartPin.Position;
                
                // Calculate perpendicular distance from line (start -> end)
                // Distance = |cross product| = |toPoint x direction|
                float perpDist = std::abs(toPoint.x * overallDir.y - toPoint.y * overallDir.x);
                
                if (perpDist > COLLINEAR_TOLERANCE)
                {
                    allCollinear = false;
                    break;
                }
            }
            
            if (allCollinear)
            {
#ifdef WAYPOINT_DEBUG
                PathfindingDebug("[RefinementPass_AutoCollapse] Collapsing {} waypoints (collinear within {:.1f} pixels)",
                                 inputWaypoints.size(), COLLINEAR_TOLERANCE);
#endif
                // All waypoints are on the straight line - remove them
                return std::vector<ImVec2>();
            }
        }
    }
    
#ifdef WAYPOINT_DEBUG
    PathfindingDebug("[RefinementPass_AutoCollapse] Keeping {} waypoints (max dimension: {:.1f} > {:.1f})",
                     inputWaypoints.size(), maxDimension, COLLAPSE_RADIUS);
#endif
    
    // Keep waypoints unchanged
    return inputWaypoints;
    
#else
    // Auto-collapse disabled - return unchanged
    
#ifdef WAYPOINT_DEBUG
    PathfindingDebug("[RefinementPass_AutoCollapse] Processing {} waypoints (ENABLE_LINK_AUTO_COLLAPSE not defined)",
                     inputWaypoints.size());
#endif
    
    return inputWaypoints;
#endif
}

} // namespace PathFinding
