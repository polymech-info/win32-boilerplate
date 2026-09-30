#pragma once
#include <imgui.h>
#include <vector>
#include <string>

// Forward declarations to avoid circular includes
namespace ax::NodeEditor { namespace Detail { struct EditorContext; } }
class Container;

// Forward declare enums (actual definitions in types.h)
enum class PinType;
enum class PinKind;
enum class NodeType;

namespace PathFinding
{
    // Obstacle bounds (min/max corners of a node) - must be defined first
    struct Obstacle
    {
        ImVec2 min;  // Top-left
        ImVec2 max;  // Bottom-right
        
        bool IntersectsSegment(const ImVec2& p1, const ImVec2& p2) const;
        bool IntersectsHorizontalLine(float y, float x1, float x2) const;
        bool IntersectsVerticalLine(float x, float y1, float y2) const;
    };
    
    // Pin context - contains all information about a pin
    struct PinContext
    {
        ImVec2 Position;          // Pin position
        ImVec2 Direction;         // Flow direction (normalized)
        PinType Type;             // Pin type (flow, parameter, etc.)
        PinKind Kind;             // Input or Output
        bool IsFlowPin;           // Flow control pin (vs parameter)
        bool IsParameterPin;      // Parameter pin (data)
    };
    
    // Node context - contains all information about a node
    struct NodeContext
    {
        ImVec2 Min;               // Top-left corner
        ImVec2 Max;               // Bottom-right corner
        NodeType Type;            // Node type (Blueprint, Parameter, etc.)
        std::string BlockType;    // Block type name (if applicable)
        bool IsGroup;             // Is this a group node?
        float ZPosition;         // Z-order
    };
    
    // Forward declare for callback
    struct RoutingContext;
    
    // Waypoint refinement callback function type
    // Takes waypoints from previous pass and full context, returns refined waypoints
    // Can access obstacles, container, links, and editor state for advanced optimization
    using WaypointRefinementCallback = std::vector<ImVec2>(*)(
        const std::vector<ImVec2>& inputWaypoints,
        const RoutingContext& ctx,
        void* userData
    );
    
    // Waypoint refinement pass - pairs a callback with its user data
    struct WaypointRefinementPass
    {
        WaypointRefinementCallback Callback;
        void* UserData;
        const char* Name; // Optional name for debugging
        
        WaypointRefinementPass(
            WaypointRefinementCallback callback = nullptr,
            void* userData = nullptr,
            const char* name = nullptr)
            : Callback(callback), UserData(userData), Name(name) {}
    };
    
    // Routing context - complete context for pathfinding
    struct RoutingContext
    {
        PinContext StartPin;       // Source pin context
        PinContext EndPin;         // Target pin context
        NodeContext StartNode;     // Source node context
        NodeContext EndNode;        // Target node context
        float Margin;             // Clearance margin
        std::vector<Obstacle> Obstacles;  // Other nodes to avoid
        ax::NodeEditor::Detail::EditorContext* EditorContext;  // Editor context
        Container* Container;      // Active container
        
        // Multi-pass waypoint refinement pipeline
        // Each pass processes the output of the previous pass
        std::vector<WaypointRefinementPass> RefinementPasses;
        
        // Helper method to add a refinement pass
        void AddRefinementPass(WaypointRefinementCallback callback, void* userData = nullptr, const char* name = nullptr)
        {
            RefinementPasses.push_back(WaypointRefinementPass(callback, userData, name));
        }
    };
    
    // Routing strategy enum
    enum class RoutingStrategy
    {
        SameBlock,          // Same-block routing
        ZShape,             // Simple Z-shape
        ZShapeAboveBlock,   // Z-shape with horizontal above block
        ZShapeBelowBlock,   // Z-shape with horizontal below block
        UShapeAbove,        // U-shape above blocks
        UShapeBelow,        // U-shape below blocks
        HorizontalFlow,     // Side-to-side flow
        AroundObstacles,   // Complex routing around obstacles
        LShape,             // Simple L-shape fallback
        Direct              // Straight line (no waypoints)
    };
    
    //-----------------------------------------------------------------------------
    // Helper Functions (Geometry and Detection)
    //-----------------------------------------------------------------------------
    
    // Geometry helpers
    bool IsSameBlock(const NodeContext& start, const NodeContext& end);
    bool NodesOverlapX(const NodeContext& start, const NodeContext& end);
    bool EndIsBelowStart(const RoutingContext& ctx);
    bool PinIsAtTop(const PinContext& pin, const NodeContext& node);
    bool PinIsOnLeft(const PinContext& pin, const NodeContext& node);
    
    // Clearance calculation helpers
    float CalculateHorizontalClearanceY(const RoutingContext& ctx, bool aboveBlock);
    float CalculateVerticalClearanceX(const RoutingContext& ctx, bool leftSide);
    
    // Obstacle detection helpers
    bool HorizontalSegmentIntersectsNode(const NodeContext& node, float y, float x1, float x2);
    bool SegmentIntersectsObstacles(const ImVec2& p1, const ImVec2& p2, const RoutingContext& ctx);
    
    //-----------------------------------------------------------------------------
    // Routing Strategy Functions
    //-----------------------------------------------------------------------------
    
    // Strategy selection
    RoutingStrategy SelectStrategy(const RoutingContext& ctx);
    bool DeterminePreferredSide(const RoutingContext& ctx);  // Returns true for left, false for right
    
    // Individual routing strategies
    std::vector<ImVec2> RouteSameBlock(const RoutingContext& ctx);
    std::vector<ImVec2> RouteZShape(const RoutingContext& ctx, float horizontalY);
    std::vector<ImVec2> RouteUShape(const RoutingContext& ctx, bool routeAbove);
    std::vector<ImVec2> RouteHorizontalFlow(const RoutingContext& ctx);
    std::vector<ImVec2> RouteLShape(const RoutingContext& ctx);
    std::vector<ImVec2> RouteAroundObstacles(const RoutingContext& ctx, bool preferLeft);
    
    //-----------------------------------------------------------------------------
    // Main Entry Points
    //-----------------------------------------------------------------------------
    
    // Generate waypoints using context structure (new API)
    // Generate waypoints for automatic rectangular routing
    // Returns list of waypoints (not including start/end)
    // If ctx.RefinementPasses is populated, applies multi-pass refinement
    std::vector<ImVec2> GenerateWaypoints(const RoutingContext& ctx);
    
    // Simple straight path (no waypoints needed)
    bool NeedsWaypoints(const ImVec2& startPos, const ImVec2& endPos, const ImVec2& startDir, const ImVec2& endDir);
    
    //-----------------------------------------------------------------------------
    // Multi-Pass Waypoint Refinement (Stub/Example Implementations)
    //-----------------------------------------------------------------------------
    
    // Pass 1: Link intersection avoidance (STUB)
    // Detects and avoids crossing other existing links
    std::vector<ImVec2> RefinementPass_AvoidLinkIntersections(
        const std::vector<ImVec2>& inputWaypoints,
        const RoutingContext& ctx,
        void* userData
    );
    
    // Pass 2: Link bundling (STUB)
    // Groups parallel links together for cleaner visual appearance
    std::vector<ImVec2> RefinementPass_BundleParallelLinks(
        const std::vector<ImVec2>& inputWaypoints,
        const RoutingContext& ctx,
        void* userData
    );
    
    // Pass 3: Path smoothing / Link fitting
    // IMPLEMENTED when ENABLE_LINK_FITTING is defined:
    //   - Aligns horizontal segments that are within 100 units vertically
    //   - Creates cleaner visual appearance for nearly-parallel links
    // When ENABLE_LINK_FITTING is not defined: returns waypoints unchanged (stub)
    std::vector<ImVec2> RefinementPass_SmoothPath(
        const std::vector<ImVec2>& inputWaypoints,
        const RoutingContext& ctx,
        void* userData
    );
    
    // Pass 4: Obstacle optimization (STUB)
    // Dynamically adjusts waypoints based on real-time obstacle positions
    std::vector<ImVec2> RefinementPass_OptimizeObstacles(
        const std::vector<ImVec2>& inputWaypoints,
        const RoutingContext& ctx,
        void* userData
    );
    
    // Pass 5: Auto-collapse to straight mode
    // IMPLEMENTED when ENABLE_LINK_AUTO_COLLAPSE is defined:
    //   - Detects when waypoints are too close together (within 100 units)
    //   - Returns empty waypoints to collapse to straight line
    // When ENABLE_LINK_AUTO_COLLAPSE is not defined: returns waypoints unchanged (stub)
    std::vector<ImVec2> RefinementPass_AutoCollapse(
        const std::vector<ImVec2>& inputWaypoints,
        const RoutingContext& ctx,
        void* userData
    );
    
} // namespace PathFinding

