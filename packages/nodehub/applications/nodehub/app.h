#pragma once
#include <base.h>
#include "types.h"
#include "blocks/parameter_node.h"
#include "utilities/edge_editing.h"
#include "utilities/style_manager.h"
#include "utilities/uuid_generator.h"
#include "utilities/uuid_id_manager.h"
#include "containers/container.h"
#include "containers/root_container.h"
#include "core/graph_state.h"
#include <map>

#include "app-types.h"

#include "core/Context.h"

namespace ed = ax::NodeEditor;

class App: public Application
{
public:

    App();
    App(const char* name);
    App(const char* name, const ArgsMap& args);

    using Application::Application;

    // Application lifecycle
    void OnStart() override;
    void OnStop() override;
    void OnFrame(float deltaTime) override;

    // ID generation (delegates to GraphState)
    int GetNextId();
    ed::LinkId GetNextLinkId();
    
    // UUID generation (32-bit)
    uint32_t GenerateRandomUuid();     // Generate random UUID (e.g., 0x50626ea)
    uint32_t GenerateSequentialUuid(); // Generate sequential UUID
    std::string UuidToHexString(uint32_t uuid);  // Convert UUID to hex string
    uint32_t HexStringToUuid(const std::string& hexString);  // Parse hex string to UUID
    
    // UUID generation (64-bit using dual 32-bit words for microcontroller compatibility)
    Uuid64 GenerateRandomUuid64();     // Generate random 64-bit UUID
    Uuid64 GenerateSequentialUuid64(); // Generate sequential 64-bit UUID
    std::string UuidToHexString64(const Uuid64& uuid);  // Convert 64-bit UUID to hex string
    Uuid64 HexStringToUuid64(const std::string& hexString);  // Parse hex string to 64-bit UUID
    
    // Standard UUID format conversion (RFC4122 with dashes)
    std::string UuidToStandardString(const Uuid64& uuid);  // Convert to "00000000-0000-0000-HHHHHHHH-LLLLLLLL"
    Uuid64 StandardStringToUuid(const std::string& uuidStr, bool takeLast64 = true);  // Parse "9b1deb4d-3b7d-4bad-9bdd-2b0d7b3dcb6d"
    
    UuidGenerator& GetUuidGenerator() { return m_UuidGenerator; }  // Direct access to generator

    // Node/Pin/Link lookups
    Node* FindNode(ed::NodeId id);
    Link* FindLink(ed::LinkId id);
    Pin* FindPin(ed::PinId id);
    
    // Node state management
    void TouchNode(ed::NodeId id);
    float GetTouchProgress(ed::NodeId id);
    void UpdateTouch();
    
    // Graph persistence (nodes, links, positions, control points)
    void SaveGraph(const std::string& filename, RootContainer* container);
    void LoadGraph(const std::string& filename, RootContainer* container);
    
    // View settings persistence (scroll, zoom, selection only)
    size_t LoadViewSettings(char* data);
    bool SaveViewSettings(const char* data, size_t size);

    // Validation
    bool IsPinLinked(ed::PinId id);
    bool CanCreateLink(Pin* a, Pin* b);
    bool IsLinkDuplicate(ed::PinId startPinId, ed::PinId endPinId);
    Link* FindLinkConnectedToPin(ed::PinId pinId);  // Find link where pin is StartPinID or EndPinID
    
    // Runtime execution interface (for imgui_node_editor_runtime.cpp)
    bool ExecuteRuntimeStep();  // Returns true if any work was processed this step
    
    // Visualization helpers (only called when rendering)
    void VisualizeRuntimeExecution(const std::vector<Node*>& executedNodes, 
                                   const std::vector<ed::LinkId>& affectedLinks);

    // Node building
    void BuildNode(Node* node);
    void BuildNodes();

    // Rendering
    void DrawPinIcon(const Pin& pin, bool connected, int alpha);
    void ShowStyleEditor(bool* show = nullptr);
    void ShowLeftPane(float paneWidth);
    ImColor GetIconColor(PinType type);
    
    // OnFrame helpers (split from massive OnFrame function)
    void RenderDebugInfo();
    void HandleKeyboardShortcuts(std::vector<ed::NodeId>& selectedNodes, std::vector<ed::LinkId>& selectedLinks);
    void RenderNodes(Pin* newLinkPin);
    void RenderLinks();
    
    // Link rendering functions for each link type
    void RenderAutoLink(Link& link, ImColor linkColor, float thickness);
    void RenderStraightLink(Link& link, ImColor linkColor, float thickness);
    void RenderGuidedLink(Link& link, ImColor linkColor, float thickness);
    void RenderLinkDelay();  // Show delay on hover and render edit box
    void StartEditLinkDelay(ed::LinkId linkId);  // Start editing delay for a link (called from keyboard shortcut or context menu)
    
    void ApplyPendingGuidedLinks();
    void AutoAdjustLinkWaypoints();
    void UpdateLastNodePositions();
    void UpdateLastNodeSize(ed::NodeId nodeId);  // Update last known size for a node (used when display mode changes)
    void UpdateGuidedLinks();
    void MarkLinkUserManipulated(ed::LinkId linkId);  // Mark link as user-manipulated (preserve waypoints)
    void HandleLinkCreationAndDeletion();
    void RenderDeferredTooltips();
    void RenderContextMenus();
    void RenderOrdinals();

    // Root container management
    // RootContainer is defined in containers/root_container.h (included via containers/container.h)
    RootContainer* GetActiveRootContainer();
    RootContainer* AddRootContainer(const std::string& filename);
    void RemoveRootContainer(RootContainer* container);
    void SetActiveRootContainer(RootContainer* container);
    Container* FindContainerForNode(ed::NodeId nodeId);  // Search all root containers
    
    // NOTE: Nodes and Links are owned by RootContainer objects, managed by GraphState
    // All container and graph data access goes through m_GraphState
    GraphState        m_GraphState;  // Central graph data management
    ed::StyleManager  m_StyleManager;  // Style management with JSON persistence
    UuidGenerator     m_UuidGenerator;  // UUID generation for unique identifiers
    UuidIdManager     m_UuidIdManager;  // UUID <-> Runtime ID mapping
    const int         m_PinIconSize = 24;
    ImTextureID       m_HeaderBackground = nullptr;
    ImTextureID       m_SaveIcon = nullptr;
    ImTextureID       m_RestoreIcon = nullptr;
    
    // Style accessor for other classes
    ed::StyleManager& GetStyleManager() { return m_StyleManager; }
    
    // Node spawning helpers
    Node* SpawnBlockNode(const char* blockType, int nodeId = -1);
    Node* SpawnParameterNode(PinType paramType, int nodeId = -1, ParameterDisplayMode displayMode = ParameterDisplayMode::NameAndValue);
    bool  m_ShowOrdinals = false;
    
    EdgeEditor m_EdgeEditor;  // Edge dragging system
    
    // Deferred tooltip system (tooltips must be rendered in ed::Suspend() to avoid coordinate issues)
    Pin* m_HoveredPin = nullptr;
    std::string m_HoveredPinTooltip;
    
    // Helper to safely delete a node and its owned instances
    void DeleteNodeAndInstances(Node& node);
    
    // Screenshot functionality
    void TakeScreenshot(const char* filename = nullptr);
    void RenderScreenshotNotification(float deltaTime);
    std::string m_ScreenshotMessage;
    float m_ScreenshotMessageTime = 0.0f;
    
    // Graph filename (can be set via CLI --graph argument)
    std::string m_GraphFilename = "BlueprintsGraph.json";
    
    // Node highlighting for Run() visualization (red borders)
    // Made public so blocks can access it for visualization
    std::map<ed::NodeId, float, NodeIdLess> m_RunningNodes;  // Node ID -> expiration time
    
    NH_Context* GetContext();

private:
    void initLogger();
    // Container management delegated to GraphState - see m_GraphState
    // Link highlighting for Run() visualization
    std::map<ed::LinkId, float, LinkIdLess> m_HighlightedLinks;  // Link ID -> expiration time
    
    // Initial zoom flag - zoom to content on first frame after load
    bool m_NeedsInitialZoom = true;

private:
    const float          m_TouchTime = 1.0f;
    std::map<ed::NodeId, float, NodeIdLess> m_NodeTouchTime;
    std::map<ed::LinkId, std::vector<ImVec2>, LinkIdLess> m_PendingGuidedLinks;  // Links waiting for guided mode setup
    std::map<ed::LinkId, ax::NodeEditor::LinkMode, LinkIdLess> m_PendingLinkModes;  // Links waiting for mode setup (Straight/Guided)
    std::map<ed::NodeId, ImVec2, NodeIdLess> m_LastNodePositions;  // Track node positions for movement detection
    std::map<ed::NodeId, ImVec2, NodeIdLess> m_LastNodeSizes;      // Track node sizes for size change detection
    
    // UI state (moved from OnFrame static variables)
    ed::NodeId m_ContextNodeId = 0;
    ed::LinkId m_ContextLinkId = 0;
    ed::PinId  m_ContextPinId = 0;
    bool       m_CreateNewNode = false;
    Pin*       m_NewNodeLinkPin = nullptr;
    Pin*       m_NewLinkPin = nullptr;
    float      m_LeftPaneWidth = 400.0f;
    float      m_RightPaneWidth = 800.0f;

    NH_Context *m_Context;

};

