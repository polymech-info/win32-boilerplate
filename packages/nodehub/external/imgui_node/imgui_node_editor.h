//------------------------------------------------------------------------------
// VERSION 0.9.1
//
// LICENSE
//   This software is dual-licensed to the public domain and under the following
//   license: you are granted a perpetual, irrevocable license to copy, modify,
//   publish, and distribute this file as you see fit.
//
// CREDITS
//   Written by Michal Cichon
//------------------------------------------------------------------------------
# ifndef __IMGUI_NODE_EDITOR_H__
# define __IMGUI_NODE_EDITOR_H__
# pragma once


//------------------------------------------------------------------------------
# include <imgui.h>
# include <cstdint> // std::uintXX_t
# include <utility> // std::move


//------------------------------------------------------------------------------
# define IMGUI_NODE_EDITOR_VERSION      "0.9.4"
# define IMGUI_NODE_EDITOR_VERSION_NUM  000904


//------------------------------------------------------------------------------
#ifndef IMGUI_NODE_EDITOR_API
#define IMGUI_NODE_EDITOR_API
#endif


//------------------------------------------------------------------------------
/// @defgroup NodeEditor ImGui Node Editor
/// @brief Node graph editor for Dear ImGui
/// @{

namespace ax {
namespace NodeEditor {

//------------------------------------------------------------------------------
/// @defgroup CoreTypes Core Types
/// @brief Type-safe IDs and fundamental enums
/// @{

struct NodeId;
struct LinkId;
struct PinId;

/// @brief Pin direction (input or output)
enum class PinKind
{
    Input,   ///< Input pin (receives connections)
    Output   ///< Output pin (creates connections)
};

/// @brief Flow animation direction
enum class FlowDirection
{
    Forward,   ///< Animate from start to end pin
    Backward   ///< Animate from end to start pin
};

/// @brief Link routing mode
enum class LinkMode
{
    Auto,      ///< Automatic bezier curve (default)
    Straight,  ///< Simple straight line
    Guided,    ///< User-defined control points (waypoints)
};

/// @brief Canvas resize behavior
enum class CanvasSizeMode
{
    FitVerticalView,        ///< Scale to fit new view on Y axis
    FitHorizontalView,      ///< Scale to fit new view on X axis
    CenterOnly,             ///< Center on new view without scaling
};

/// @} // CoreTypes


//------------------------------------------------------------------------------
/// @defgroup Configuration Configuration
/// @brief Editor configuration and persistence
/// @{

/// @brief Flags indicating why settings are being saved
enum class SaveReasonFlags: uint32_t
{
    None       = 0x00000000,  ///< No specific reason
    Navigation = 0x00000001,  ///< Canvas navigation changed
    Position   = 0x00000002,  ///< Node position changed
    Size       = 0x00000004,  ///< Node size changed
    Selection  = 0x00000008,  ///< Selection changed
    AddNode    = 0x00000010,  ///< Node added
    RemoveNode = 0x00000020,  ///< Node removed
    User       = 0x00000040   ///< User-triggered save
};

inline SaveReasonFlags operator |(SaveReasonFlags lhs, SaveReasonFlags rhs) { return static_cast<SaveReasonFlags>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs)); }
inline SaveReasonFlags operator &(SaveReasonFlags lhs, SaveReasonFlags rhs) { return static_cast<SaveReasonFlags>(static_cast<uint32_t>(lhs) & static_cast<uint32_t>(rhs)); }

/// @brief Callback to save editor settings
using ConfigSaveSettings     = bool   (*)(const char* data, size_t size, SaveReasonFlags reason, void* userPointer);
/// @brief Callback to load editor settings
using ConfigLoadSettings     = size_t (*)(char* data, void* userPointer);

/// @brief Callback to save per-node settings
using ConfigSaveNodeSettings = bool   (*)(NodeId nodeId, const char* data, size_t size, SaveReasonFlags reason, void* userPointer);
/// @brief Callback to load per-node settings
using ConfigLoadNodeSettings = size_t (*)(NodeId nodeId, char* data, void* userPointer);

/// @brief Callback for save session begin/end
using ConfigSession          = void   (*)(void* userPointer);

/// @brief Callback to get container node IDs for interaction processing
/// Returns number of node IDs, populates nodeIdsOut array (up to maxNodes)
/// If nodeIdsOut is nullptr, returns total count
using ConfigGetContainerNodeIds = int (*)(void* userPointer, NodeId* nodeIdsOut, int maxNodes);

/// @brief Callback to get container link IDs for interaction processing  
/// Returns number of link IDs, populates linkIdsOut array (up to maxLinks)
/// If linkIdsOut is nullptr, returns total count
using ConfigGetContainerLinkIds = int (*)(void* userPointer, LinkId* linkIdsOut, int maxLinks);

/// @brief Callback to mark link as user-manipulated (preserve waypoints, disable auto-adjustment)
using ConfigMarkLinkUserManipulated = void (*)(LinkId linkId, void* userPointer);

/// @brief Callback when block display mode changes (triggers link auto-adjustment)
using ConfigOnBlockDisplayModeChanged = void (*)(NodeId nodeId, void* userPointer);

/// @brief Editor configuration structure
struct Config
{
    using CanvasSizeModeAlias = ax::NodeEditor::CanvasSizeMode;

    const char*             SettingsFile;
    ConfigSession           BeginSaveSession;
    ConfigSession           EndSaveSession;
    ConfigSaveSettings      SaveSettings;
    ConfigLoadSettings      LoadSettings;
    ConfigSaveNodeSettings  SaveNodeSettings;
    ConfigLoadNodeSettings  LoadNodeSettings;
    ConfigGetContainerNodeIds GetContainerNodeIds;  // Optional: get nodes from container
    ConfigGetContainerLinkIds GetContainerLinkIds;  // Optional: get links from container
    ConfigMarkLinkUserManipulated MarkLinkUserManipulated;  // Optional: mark link as user-manipulated
    ConfigOnBlockDisplayModeChanged OnBlockDisplayModeChanged;  // Optional: callback when block display mode changes
    void*                   UserPointer;
    ImVector<float>         CustomZoomLevels;
    CanvasSizeModeAlias     CanvasSizeMode;
    int                     DragButtonIndex;        // Mouse button index drag action will react to (0-left, 1-right, 2-middle)
    int                     SelectButtonIndex;      // Mouse button index select action will react to (0-left, 1-right, 2-middle)
    int                     NavigateButtonIndex;    // Mouse button index navigate action will react to (0-left, 1-right, 2-middle)
    int                     ContextMenuButtonIndex; // Mouse button index context menu action will react to (0-left, 1-right, 2-middle)
    bool                    EnableSmoothZoom;
    float                   SmoothZoomPower;

    Config()
        : SettingsFile("NodeEditor.json")
        , BeginSaveSession(nullptr)
        , EndSaveSession(nullptr)
        , SaveSettings(nullptr)
        , LoadSettings(nullptr)
        , SaveNodeSettings(nullptr)
        , LoadNodeSettings(nullptr)
        , GetContainerNodeIds(nullptr)
        , GetContainerLinkIds(nullptr)
        , MarkLinkUserManipulated(nullptr)
        , OnBlockDisplayModeChanged(nullptr)
        , UserPointer(nullptr)
        , CustomZoomLevels()
        , CanvasSizeMode(CanvasSizeModeAlias::FitVerticalView)
        , DragButtonIndex(0)
        , SelectButtonIndex(0)
        , NavigateButtonIndex(1)
        , ContextMenuButtonIndex(1)
        , EnableSmoothZoom(false)
# ifdef __APPLE__
        , SmoothZoomPower(1.1f)
# else
        , SmoothZoomPower(1.3f)
# endif
    {
    }
};

/// @} // Configuration


//------------------------------------------------------------------------------
/// @defgroup Style Style
/// @brief Visual styling and theming
/// @{

/// @brief Style color identifiers
enum StyleColor
{
    StyleColor_Bg,                      ///< Editor background
    StyleColor_Grid,                    ///< Background grid
    StyleColor_NodeBg,                  ///< Node background
    StyleColor_NodeBorder,              ///< Node border
    StyleColor_HovNodeBorder,           ///< Hovered node border
    StyleColor_SelNodeBorder,           ///< Selected node border
    StyleColor_NodeSelRect,             ///< Node selection rectangle fill
    StyleColor_NodeSelRectBorder,       ///< Node selection rectangle border
    StyleColor_HovLinkBorder,           ///< Hovered link border
    StyleColor_SelLinkBorder,           ///< Selected link border
    StyleColor_HighlightLinkBorder,     ///< Highlighted link border
    StyleColor_LinkSelRect,             ///< Link selection rectangle fill
    StyleColor_LinkSelRectBorder,       ///< Link selection rectangle border
    StyleColor_PinRect,                 ///< Pin rectangle fill
    StyleColor_PinRectBorder,           ///< Pin rectangle border
    StyleColor_Flow,                    ///< Flow animation color
    StyleColor_FlowMarker,              ///< Flow marker color
    StyleColor_GroupBg,                 ///< Group background
    StyleColor_GroupBorder,             ///< Group border

    StyleColor_Count                    ///< Total number of colors
};

/// @brief Style variable identifiers
enum StyleVar
{
    StyleVar_NodePadding,               ///< Node padding (ImVec4)
    StyleVar_NodeRounding,              ///< Node corner rounding (float)
    StyleVar_NodeBorderWidth,           ///< Node border width (float)
    StyleVar_HoveredNodeBorderWidth,    ///< Hovered node border width (float)
    StyleVar_SelectedNodeBorderWidth,   ///< Selected node border width (float)
    StyleVar_PinRounding,               ///< Pin corner rounding (float)
    StyleVar_PinBorderWidth,            ///< Pin border width (float)
    StyleVar_LinkStrength,              ///< Link curve strength (float)
    StyleVar_SourceDirection,           ///< Link source direction (ImVec2)
    StyleVar_TargetDirection,           ///< Link target direction (ImVec2)
    StyleVar_ScrollDuration,            ///< Navigation scroll duration (float)
    StyleVar_FlowMarkerDistance,        ///< Distance between flow markers (float)
    StyleVar_FlowSpeed,                 ///< Flow animation speed (float)
    StyleVar_FlowDuration,              ///< Flow animation duration (float)
    StyleVar_PivotAlignment,            ///< Pin pivot alignment (ImVec2)
    StyleVar_PivotSize,                 ///< Pin pivot size (ImVec2)
    StyleVar_PivotScale,                ///< Pin pivot scale (ImVec2)
    StyleVar_PinCorners,                ///< Pin corner flags (float)
    StyleVar_PinRadius,                 ///< Pin radius (float)
    StyleVar_PinArrowSize,              ///< Pin arrow size (float)
    StyleVar_PinArrowWidth,             ///< Pin arrow width (float)
    StyleVar_GroupRounding,             ///< Group corner rounding (float)
    StyleVar_GroupBorderWidth,          ///< Group border width (float)
    StyleVar_HighlightConnectedLinks,   ///< Highlight connected links (float)
    StyleVar_SnapLinkToPinDir,          ///< Snap link to pin direction (float)
    StyleVar_HoveredNodeBorderOffset,   ///< Hovered node border offset (float)
    StyleVar_SelectedNodeBorderOffset,  ///< Selected node border offset (float)

    StyleVar_Count                      ///< Total number of style variables
};

/// @brief Editor visual style configuration
struct Style
{
    ImVec4  NodePadding;
    float   NodeRounding;
    float   NodeBorderWidth;
    float   HoveredNodeBorderWidth;
    float   HoverNodeBorderOffset;
    float   SelectedNodeBorderWidth;
    float   SelectedNodeBorderOffset;
    float   PinRounding;
    float   PinBorderWidth;
    float   LinkStrength;
    ImVec2  SourceDirection;
    ImVec2  TargetDirection;
    float   ScrollDuration;
    float   FlowMarkerDistance;
    float   FlowSpeed;
    float   FlowDuration;
    ImVec2  PivotAlignment;
    ImVec2  PivotSize;
    ImVec2  PivotScale;
    float   PinCorners;
    float   PinRadius;
    float   PinArrowSize;
    float   PinArrowWidth;
    float   GroupRounding;
    float   GroupBorderWidth;
    float   HighlightConnectedLinks;
    float   SnapLinkToPinDir; // when true link will start on the line defined by pin direction
    float   WaypointRadius;   // Base radius for waypoints (scales with zoom)
    float   WaypointBorderWidth; // Border width for waypoints
    ImU32   WaypointColor;    // Waypoint fill color
    ImU32   WaypointBorderColor; // Waypoint border color
    ImU32   WaypointColorHovered; // Waypoint color when hovered
    ImU32   WaypointColorSelected; // Waypoint color when selected
    ImVec4  Colors[StyleColor_Count];

    Style()
    {
        NodePadding              = ImVec4(8, 8, 8, 8);
        NodeRounding             = 12.0f;
        NodeBorderWidth          = 1.5f;
        HoveredNodeBorderWidth   = 3.5f;
        HoverNodeBorderOffset    = 0.0f;
        SelectedNodeBorderWidth  = 3.5f;
        SelectedNodeBorderOffset = 0.0f;
        PinRounding              = 4.0f;
        PinBorderWidth           = 0.0f;
        LinkStrength             = 100.0f;
        SourceDirection          = ImVec2(1.0f, 0.0f);
        TargetDirection          = ImVec2(-1.0f, 0.0f);
        ScrollDuration           = 0.35f;
        FlowMarkerDistance       = 30.0f;
        FlowSpeed                = 150.0f;
        FlowDuration             = 2.0f;
        PivotAlignment           = ImVec2(0.5f, 0.5f);
        PivotSize                = ImVec2(0.0f, 0.0f);
        PivotScale               = ImVec2(1, 1);
#if IMGUI_VERSION_NUM > 18101
        PinCorners               = ImDrawFlags_RoundCornersAll;
#else
        PinCorners               = ImDrawCornerFlags_All;
#endif
        PinRadius                = 0.0f;
        PinArrowSize             = 0.0f;
        PinArrowWidth            = 0.0f;
        GroupRounding            = 6.0f;
        GroupBorderWidth         = 1.0f;
        HighlightConnectedLinks  = 0.0f;
        SnapLinkToPinDir         = 0.0f;
        WaypointRadius           = 6.0f;
        WaypointBorderWidth      = 2.0f;
        WaypointColor            = IM_COL32(255, 200, 100, 255);
        WaypointBorderColor      = IM_COL32(0, 0, 0, 200);
        WaypointColorHovered     = IM_COL32(255, 220, 120, 255);
        WaypointColorSelected    = IM_COL32(255, 255, 150, 255);

        Colors[StyleColor_Bg]                 = ImColor( 60,  60,  70, 200);
        Colors[StyleColor_Grid]               = ImColor(120, 120, 120,  40);
        Colors[StyleColor_NodeBg]             = ImColor( 32,  32,  32, 200);
        Colors[StyleColor_NodeBorder]         = ImColor(255, 255, 255,  96);
        Colors[StyleColor_HovNodeBorder]      = ImColor( 50, 176, 255, 255);
        Colors[StyleColor_SelNodeBorder]      = ImColor(255, 176,  50, 255);
        Colors[StyleColor_NodeSelRect]        = ImColor(  5, 130, 255,  64);
        Colors[StyleColor_NodeSelRectBorder]  = ImColor(  5, 130, 255, 128);
        Colors[StyleColor_HovLinkBorder]      = ImColor( 50, 176, 255, 255);
        Colors[StyleColor_SelLinkBorder]      = ImColor(255, 176,  50, 255);
        Colors[StyleColor_HighlightLinkBorder]= ImColor(204, 105,   0, 255);
        Colors[StyleColor_LinkSelRect]        = ImColor(  5, 130, 255,  64);
        Colors[StyleColor_LinkSelRectBorder]  = ImColor(  5, 130, 255, 128);
        Colors[StyleColor_PinRect]            = ImColor( 60, 180, 255, 100);
        Colors[StyleColor_PinRectBorder]      = ImColor( 60, 180, 255, 128);
        Colors[StyleColor_Flow]               = ImColor(255, 128,  64, 255);
        Colors[StyleColor_FlowMarker]         = ImColor(255, 128,  64, 255);
        Colors[StyleColor_GroupBg]            = ImColor(  0,   0,   0, 160);
        Colors[StyleColor_GroupBorder]        = ImColor(255, 255, 255,  32);
    }
};

/// @} // Style


//------------------------------------------------------------------------------
/// @defgroup EditorContext Editor Context
/// @brief Editor creation, destruction, and lifecycle
/// @{

struct EditorContext;

IMGUI_NODE_EDITOR_API void SetCurrentEditor(EditorContext* ctx);          ///< Set active editor context
IMGUI_NODE_EDITOR_API EditorContext* GetCurrentEditor();                  ///< Get active editor context
IMGUI_NODE_EDITOR_API EditorContext* CreateEditor(const Config* config = nullptr);  ///< Create new editor context
IMGUI_NODE_EDITOR_API void DestroyEditor(EditorContext* ctx);             ///< Destroy editor context
IMGUI_NODE_EDITOR_API const Config& GetConfig(EditorContext* ctx = nullptr);  ///< Get editor configuration

IMGUI_NODE_EDITOR_API Style& GetStyle();                                  ///< Get editor style
IMGUI_NODE_EDITOR_API const char* GetStyleColorName(StyleColor colorIndex);  ///< Get style color name

IMGUI_NODE_EDITOR_API void PushStyleColor(StyleColor colorIndex, const ImVec4& color);  ///< Push style color
IMGUI_NODE_EDITOR_API void PopStyleColor(int count = 1);                  ///< Pop style color(s)

IMGUI_NODE_EDITOR_API void PushStyleVar(StyleVar varIndex, float value);  ///< Push style variable (float)
IMGUI_NODE_EDITOR_API void PushStyleVar(StyleVar varIndex, const ImVec2& value);  ///< Push style variable (ImVec2)
IMGUI_NODE_EDITOR_API void PushStyleVar(StyleVar varIndex, const ImVec4& value);  ///< Push style variable (ImVec4)
IMGUI_NODE_EDITOR_API void PopStyleVar(int count = 1);                    ///< Pop style variable(s)

/// @} // EditorContext


//------------------------------------------------------------------------------
/// @defgroup Canvas Canvas and Frame
/// @brief Editor frame begin/end and suspend/resume
/// @{

IMGUI_NODE_EDITOR_API void Begin(const char* id, const ImVec2& size = ImVec2(0, 0));  ///< Begin editor frame
IMGUI_NODE_EDITOR_API void End();                                         ///< End editor frame (coordinates rendering)

IMGUI_NODE_EDITOR_API void Suspend();                                     ///< Suspend editor (allows ImGui rendering)
IMGUI_NODE_EDITOR_API void Resume();                                      ///< Resume editor after suspension
IMGUI_NODE_EDITOR_API bool IsSuspended();                                 ///< Check if editor is suspended

IMGUI_NODE_EDITOR_API bool IsActive();                                    ///< Check if editor is active

/// @} // Canvas


//------------------------------------------------------------------------------
/// @defgroup NodeAPI Node API
/// @brief Node creation and rendering
/// @{

IMGUI_NODE_EDITOR_API void BeginNode(NodeId id);                          ///< Begin node definition
IMGUI_NODE_EDITOR_API void EndNode();                                     ///< End node definition
IMGUI_NODE_EDITOR_API void Group(const ImVec2& size);                     ///< Make node resizable (group/comment node)

IMGUI_NODE_EDITOR_API bool BeginGroupHint(NodeId nodeId);                 ///< Begin group hint rendering
IMGUI_NODE_EDITOR_API ImVec2 GetGroupMin();                               ///< Get group minimum bounds
IMGUI_NODE_EDITOR_API ImVec2 GetGroupMax();                               ///< Get group maximum bounds
IMGUI_NODE_EDITOR_API ImDrawList* GetHintForegroundDrawList();            ///< Get hint foreground draw list
IMGUI_NODE_EDITOR_API ImDrawList* GetHintBackgroundDrawList();            ///< Get hint background draw list
IMGUI_NODE_EDITOR_API void EndGroupHint();                                ///< End group hint rendering

IMGUI_NODE_EDITOR_API ImDrawList* GetNodeBackgroundDrawList(NodeId nodeId);  ///< Get node background draw list

/// @} // NodeAPI


//------------------------------------------------------------------------------
/// @defgroup PinAPI Pin API
/// @brief Pin creation and configuration
/// @{

IMGUI_NODE_EDITOR_API void BeginPin(PinId id, PinKind kind);              ///< Begin pin definition
IMGUI_NODE_EDITOR_API void EndPin();                                      ///< End pin definition
IMGUI_NODE_EDITOR_API void PinRect(const ImVec2& a, const ImVec2& b);    ///< Set pin interaction rectangle
IMGUI_NODE_EDITOR_API void PinPivotRect(const ImVec2& a, const ImVec2& b);  ///< Set pin pivot rectangle
IMGUI_NODE_EDITOR_API void PinPivotSize(const ImVec2& size);              ///< Set pin pivot size
IMGUI_NODE_EDITOR_API void PinPivotScale(const ImVec2& scale);            ///< Set pin pivot scale
IMGUI_NODE_EDITOR_API void PinPivotAlignment(const ImVec2& alignment);    ///< Set pin pivot alignment

/// @} // PinAPI


//------------------------------------------------------------------------------
/// @defgroup LinkAPI Link API
/// @brief Link rendering and animation
/// @{

IMGUI_NODE_EDITOR_API bool Link(LinkId id, PinId startPinId, PinId endPinId, const ImVec4& color = ImVec4(1, 1, 1, 1), float thickness = 1.0f);  ///< Draw link between pins

IMGUI_NODE_EDITOR_API void Flow(LinkId linkId, FlowDirection direction = FlowDirection::Forward);  ///< Trigger flow animation on link

IMGUI_NODE_EDITOR_API void NotifyBlockDisplayModeChanged(NodeId nodeId);  ///< Notify that block display mode changed (triggers link auto-adjustment)

/// @} // LinkAPI


//------------------------------------------------------------------------------
/// @defgroup Creation Creation and Deletion
/// @brief Interactive object creation and deletion
/// @{

IMGUI_NODE_EDITOR_API bool BeginCreate(const ImVec4& color = ImVec4(1, 1, 1, 1), float thickness = 1.0f);  ///< Begin creation mode
IMGUI_NODE_EDITOR_API bool QueryNewLink(PinId* startId, PinId* endId);                      ///< Query link being created
IMGUI_NODE_EDITOR_API bool QueryNewLink(PinId* startId, PinId* endId, const ImVec4& color, float thickness = 1.0f);  ///< Query link with custom style
IMGUI_NODE_EDITOR_API bool QueryNewNode(PinId* pinId);                                       ///< Query node being created from pin
IMGUI_NODE_EDITOR_API bool QueryNewNode(PinId* pinId, const ImVec4& color, float thickness = 1.0f);  ///< Query node with custom style
IMGUI_NODE_EDITOR_API bool AcceptNewItem();                                                   ///< Accept creation
IMGUI_NODE_EDITOR_API bool AcceptNewItem(const ImVec4& color, float thickness = 1.0f);      ///< Accept creation with custom style
IMGUI_NODE_EDITOR_API void RejectNewItem();                                                   ///< Reject creation
IMGUI_NODE_EDITOR_API void RejectNewItem(const ImVec4& color, float thickness = 1.0f);      ///< Reject creation with custom style
IMGUI_NODE_EDITOR_API void EndCreate();                                                       ///< End creation mode

IMGUI_NODE_EDITOR_API bool BeginDelete();                                                     ///< Begin deletion mode
IMGUI_NODE_EDITOR_API bool QueryDeletedLink(LinkId* linkId, PinId* startId = nullptr, PinId* endId = nullptr);  ///< Query link being deleted
IMGUI_NODE_EDITOR_API bool QueryDeletedNode(NodeId* nodeId);                                 ///< Query node being deleted
IMGUI_NODE_EDITOR_API bool AcceptDeletedItem(bool deleteDependencies = true);                ///< Accept deletion
IMGUI_NODE_EDITOR_API void RejectDeletedItem();                                               ///< Reject deletion
IMGUI_NODE_EDITOR_API void EndDelete();                                                       ///< End deletion mode

/// @} // Creation


//------------------------------------------------------------------------------
/// @defgroup NodeManipulation Node Manipulation
/// @brief Node positioning, sizing, and Z-order
/// @{

IMGUI_NODE_EDITOR_API void SetNodePosition(NodeId nodeId, const ImVec2& editorPosition);  ///< Set node position
IMGUI_NODE_EDITOR_API void SetGroupSize(NodeId nodeId, const ImVec2& size);  ///< Set group size
IMGUI_NODE_EDITOR_API ImVec2 GetNodePosition(NodeId nodeId);                  ///< Get node position
IMGUI_NODE_EDITOR_API ImVec2 GetNodeSize(NodeId nodeId);                      ///< Get node size
IMGUI_NODE_EDITOR_API void CenterNodeOnScreen(NodeId nodeId);                 ///< Center node on screen
IMGUI_NODE_EDITOR_API void SetNodeZPosition(NodeId nodeId, float z);          ///< Set node Z-order (higher = front)
IMGUI_NODE_EDITOR_API float GetNodeZPosition(NodeId nodeId);                  ///< Get node Z-order

IMGUI_NODE_EDITOR_API void RestoreNodeState(NodeId nodeId);                   ///< Restore saved node state

/// @} // NodeManipulation


//------------------------------------------------------------------------------
/// @defgroup Selection Selection
/// @brief Object selection management
/// @{

IMGUI_NODE_EDITOR_API bool HasSelectionChanged();                             ///< Check if selection changed this frame
IMGUI_NODE_EDITOR_API int  GetSelectedObjectCount();                          ///< Get total selected object count
IMGUI_NODE_EDITOR_API int  GetSelectedNodes(NodeId* nodes, int size);         ///< Get selected node IDs
IMGUI_NODE_EDITOR_API int  GetSelectedLinks(LinkId* links, int size);         ///< Get selected link IDs
IMGUI_NODE_EDITOR_API bool IsNodeSelected(NodeId nodeId);                     ///< Check if node is selected
IMGUI_NODE_EDITOR_API bool IsLinkSelected(LinkId linkId);                     ///< Check if link is selected
IMGUI_NODE_EDITOR_API void ClearSelection();                                  ///< Clear all selection
IMGUI_NODE_EDITOR_API void SelectNode(NodeId nodeId, bool append = false);    ///< Select node (append or replace)
IMGUI_NODE_EDITOR_API void SelectLink(LinkId linkId, bool append = false);    ///< Select link (append or replace)
IMGUI_NODE_EDITOR_API void DeselectNode(NodeId nodeId);                       ///< Deselect specific node
IMGUI_NODE_EDITOR_API void DeselectLink(LinkId linkId);                       ///< Deselect specific link

/// @} // Selection


//------------------------------------------------------------------------------
/// @defgroup Deletion Programmatic Deletion
/// @brief Delete objects programmatically
/// @{

IMGUI_NODE_EDITOR_API bool DeleteNode(NodeId nodeId);                         ///< Delete node (returns success)
IMGUI_NODE_EDITOR_API bool DeleteLink(LinkId linkId);                         ///< Delete link (returns success)

IMGUI_NODE_EDITOR_API bool HasAnyLinks(NodeId nodeId);                        ///< Check if node has any links
IMGUI_NODE_EDITOR_API bool HasAnyLinks(PinId pinId);                          ///< Check if pin has any links
IMGUI_NODE_EDITOR_API int BreakLinks(NodeId nodeId);                          ///< Break all node links (returns count)
IMGUI_NODE_EDITOR_API int BreakLinks(PinId pinId);                            ///< Break all pin links (returns count)

/// @} // Deletion


//------------------------------------------------------------------------------
/// @defgroup Navigation Navigation
/// @brief Canvas navigation and camera control
/// @{

IMGUI_NODE_EDITOR_API void NavigateToContent(float duration = -1);            ///< Navigate to fit all content
IMGUI_NODE_EDITOR_API void NavigateToSelection(bool zoomIn = false, float duration = -1);  ///< Navigate to selected objects

IMGUI_NODE_EDITOR_API float GetCurrentZoom();                                  ///< Get current zoom level

IMGUI_NODE_EDITOR_API ImVec2 GetScreenSize();                                  ///< Get editor screen size
IMGUI_NODE_EDITOR_API ImVec2 ScreenToCanvas(const ImVec2& pos);                ///< Convert screen to canvas coordinates
IMGUI_NODE_EDITOR_API ImVec2 CanvasToScreen(const ImVec2& pos);                ///< Convert canvas to screen coordinates

/// @} // Navigation


//------------------------------------------------------------------------------
/// @defgroup ContextMenus Context Menus
/// @brief Context menu helpers
/// @{

IMGUI_NODE_EDITOR_API bool ShowNodeContextMenu(NodeId* nodeId);                ///< Show node context menu
IMGUI_NODE_EDITOR_API bool ShowPinContextMenu(PinId* pinId);                   ///< Show pin context menu
IMGUI_NODE_EDITOR_API bool ShowLinkContextMenu(LinkId* linkId);                ///< Show link context menu
IMGUI_NODE_EDITOR_API bool ShowBackgroundContextMenu();                        ///< Show background context menu

/// @} // ContextMenus


//------------------------------------------------------------------------------
/// @defgroup Shortcuts Shortcuts
/// @brief Keyboard shortcut handling
/// @{

IMGUI_NODE_EDITOR_API void EnableShortcuts(bool enable);                       ///< Enable/disable shortcuts
IMGUI_NODE_EDITOR_API bool AreShortcutsEnabled();                              ///< Check if shortcuts are enabled

IMGUI_NODE_EDITOR_API bool BeginShortcut();                                    ///< Begin shortcut handling
IMGUI_NODE_EDITOR_API bool AcceptCut();                                        ///< Accept cut shortcut
IMGUI_NODE_EDITOR_API bool AcceptCopy();                                       ///< Accept copy shortcut
IMGUI_NODE_EDITOR_API bool AcceptPaste();                                      ///< Accept paste shortcut
IMGUI_NODE_EDITOR_API bool AcceptDuplicate();                                  ///< Accept duplicate shortcut
IMGUI_NODE_EDITOR_API bool AcceptCreateNode();                                 ///< Accept create node shortcut
IMGUI_NODE_EDITOR_API int  GetActionContextSize();                             ///< Get action context size
IMGUI_NODE_EDITOR_API int  GetActionContextNodes(NodeId* nodes, int size);    ///< Get action context nodes
IMGUI_NODE_EDITOR_API int  GetActionContextLinks(LinkId* links, int size);    ///< Get action context links
IMGUI_NODE_EDITOR_API void EndShortcut();                                      ///< End shortcut handling

/// @} // Shortcuts


//------------------------------------------------------------------------------
/// @defgroup Queries Queries
/// @brief Query hover, clicks, and object states
/// @{

IMGUI_NODE_EDITOR_API NodeId GetHoveredNode();                                 ///< Get currently hovered node
IMGUI_NODE_EDITOR_API PinId GetHoveredPin();                                   ///< Get currently hovered pin
IMGUI_NODE_EDITOR_API LinkId GetHoveredLink();                                 ///< Get currently hovered link
IMGUI_NODE_EDITOR_API NodeId GetDoubleClickedNode();                           ///< Get double-clicked node this frame
IMGUI_NODE_EDITOR_API PinId GetDoubleClickedPin();                             ///< Get double-clicked pin this frame
IMGUI_NODE_EDITOR_API LinkId GetDoubleClickedLink();                           ///< Get double-clicked link this frame
IMGUI_NODE_EDITOR_API bool IsBackgroundClicked();                              ///< Check if background was clicked
IMGUI_NODE_EDITOR_API bool IsBackgroundDoubleClicked();                        ///< Check if background was double-clicked
IMGUI_NODE_EDITOR_API ImGuiMouseButton GetBackgroundClickButtonIndex();        ///< Get background click button (-1 if none)
IMGUI_NODE_EDITOR_API ImGuiMouseButton GetBackgroundDoubleClickButtonIndex();  ///< Get background double-click button (-1 if none)

IMGUI_NODE_EDITOR_API bool GetLinkPins(LinkId linkId, PinId* startPinId, PinId* endPinId);  ///< Get link endpoints (pass nullptr to skip)

IMGUI_NODE_EDITOR_API bool PinHadAnyLinks(PinId pinId);                        ///< Check if pin had links before deletion

IMGUI_NODE_EDITOR_API int GetNodeCount();                                      ///< Get number of submitted nodes
IMGUI_NODE_EDITOR_API int GetOrderedNodeIds(NodeId* nodes, int size);          ///< Get node IDs in draw order

/// @} // Queries


//------------------------------------------------------------------------------
/// @defgroup GuidedLinks Guided Links
/// @brief User-controllable link routing with waypoints
/// @{

IMGUI_NODE_EDITOR_API bool     IsLinkGuided(LinkId linkId);                    ///< Check if link is in guided mode
IMGUI_NODE_EDITOR_API bool     SetLinkGuided(LinkId linkId, bool guided = true);  ///< Convert link to/from guided mode
IMGUI_NODE_EDITOR_API LinkMode GetLinkMode(LinkId linkId);                     ///< Get link mode (Auto/Straight/Guided)
IMGUI_NODE_EDITOR_API bool     SetLinkMode(LinkId linkId, LinkMode mode);      ///< Set link mode
IMGUI_NODE_EDITOR_API int  GetLinkControlPointCount(LinkId linkId);            ///< Get number of waypoints
IMGUI_NODE_EDITOR_API bool GetLinkControlPoints(LinkId linkId, ImVec2* points, int maxCount);  ///< Get waypoint positions
IMGUI_NODE_EDITOR_API void SetLinkControlPoints(LinkId linkId, const ImVec2* points, int count);  ///< Set waypoint positions
IMGUI_NODE_EDITOR_API void AddLinkControlPoint(LinkId linkId, const ImVec2& position, int index = -1);  ///< Add waypoint (-1 = append)
IMGUI_NODE_EDITOR_API void RemoveLinkControlPoint(LinkId linkId, int index);   ///< Remove waypoint by index
IMGUI_NODE_EDITOR_API void ClearLinkControlPoints(LinkId linkId);              ///< Remove all waypoints
IMGUI_NODE_EDITOR_API void ResetLinkToAuto(LinkId linkId);                     ///< Reset link to automatic routing

/// @} // GuidedLinks






//------------------------------------------------------------------------------
namespace Details {

template <typename T, typename Tag>
struct SafeType
{
    SafeType(T t)
        : m_Value(std::move(t))
    {
    }

    SafeType(const SafeType&) = default;

    template <typename T2, typename Tag2>
    SafeType(
        const SafeType
        <
            typename std::enable_if<!std::is_same<T, T2>::value, T2>::type,
            typename std::enable_if<!std::is_same<Tag, Tag2>::value, Tag2>::type
        >&) = delete;

    SafeType& operator=(const SafeType&) = default;

    explicit operator T() const { return Get(); }

    T Get() const { return m_Value; }

private:
    T m_Value;
};


template <typename Tag>
struct SafePointerType
    : SafeType<uintptr_t, Tag>
{
    static const Tag Invalid;

    using SafeType<uintptr_t, Tag>::SafeType;

    SafePointerType()
        : SafePointerType(Invalid)
    {
    }

    template <typename T = void> explicit SafePointerType(T* ptr): SafePointerType(reinterpret_cast<uintptr_t>(ptr)) {}
    template <typename T = void> T* AsPointer() const { return reinterpret_cast<T*>(this->Get()); }

    explicit operator bool() const { return *this != Invalid; }
};

template <typename Tag>
const Tag SafePointerType<Tag>::Invalid = { 0 };

template <typename Tag>
inline bool operator==(const SafePointerType<Tag>& lhs, const SafePointerType<Tag>& rhs)
{
    return lhs.Get() == rhs.Get();
}

template <typename Tag>
inline bool operator!=(const SafePointerType<Tag>& lhs, const SafePointerType<Tag>& rhs)
{
    return lhs.Get() != rhs.Get();
}

} // namespace Details

/// @brief Type-safe node identifier
struct NodeId final: Details::SafePointerType<NodeId>
{
    using SafePointerType::SafePointerType;
};

/// @brief Type-safe link identifier
struct LinkId final: Details::SafePointerType<LinkId>
{
    using SafePointerType::SafePointerType;
};

/// @brief Type-safe pin identifier
struct PinId final: Details::SafePointerType<PinId>
{
    using SafePointerType::SafePointerType;
};

//------------------------------------------------------------------------------
} // namespace Editor
} // namespace ax

/// @} // NodeEditor

//------------------------------------------------------------------------------
# endif // __IMGUI_NODE_EDITOR_H__
