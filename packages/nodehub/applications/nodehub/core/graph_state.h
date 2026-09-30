#pragma once
#include "../types.h"
#include "../containers/root_container.h"
#include <map>
#include <vector>
#include <string>

namespace ed = ax::NodeEditor;

// Forward declarations
class App;

/**
 * GraphState - Pure graph data structure (no rendering, no UI dependencies)
 * 
 * Manages:
 * - Node and link storage (indexed by ID)
 * - Container hierarchy
 * - ID generation
 * - Graph data lookups
 */
class GraphState
{
public:
    GraphState();
    ~GraphState();
    
    // ID generation
    int GetNextId();
    ed::LinkId GetNextLinkId();
    
    // Node/Link/Pin lookups
    Node* FindNode(ed::NodeId id);
    Link* FindLink(ed::LinkId id);
    // Note: FindPin requires App* for container lookup, so it's kept in App class
    
    // Node/Link management
    Node* AddNode(const Node& node);
    bool RemoveNode(ed::NodeId id);
    Link* AddLink(const Link& link);
    bool RemoveLink(ed::LinkId id);
    
    // Container management
    RootContainer* GetActiveRootContainer() const { return m_ActiveRootContainer; }
    RootContainer* AddRootContainer(const std::string& filename);
    void RemoveRootContainer(RootContainer* container);
    void SetActiveRootContainer(RootContainer* container);
    Container* FindContainerForNode(ed::NodeId nodeId);  // Search all root containers
    
    // Graph data access - nodes/links are owned by containers, not stored here
    // GraphState provides unified lookup interface that searches all containers
    const std::vector<RootContainer*>& GetRootContainers() const { return m_RootContainers; }
    
    // Iteration helpers
    std::vector<Node*> GetAllNodes() const;
    std::vector<Link*> GetAllLinks() const;
    
    // Validation
    bool IsPinLinked(ed::PinId id) const;
    bool IsLinkDuplicate(ed::PinId startPinId, ed::PinId endPinId) const;
    Link* FindLinkConnectedToPin(ed::PinId pinId) const;
    
    // Graph persistence (will be moved here from App)
    void SaveGraph(const std::string& filename, App* app);
    void LoadGraph(const std::string& filename, App* app);

private:
    // Note: Nodes and Links are owned by RootContainer objects, not stored here
    // GraphState provides unified lookup that searches all containers
    
    // Container hierarchy
    std::vector<RootContainer*> m_RootContainers;
    RootContainer* m_ActiveRootContainer = nullptr;
    
    // ID generation (shared across all containers)
    int m_NextId = 1;
};

