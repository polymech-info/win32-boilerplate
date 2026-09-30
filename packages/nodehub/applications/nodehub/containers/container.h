#pragma once
#include "../types.h"
#include <string>
#include <vector>
#include <map>

namespace ed = ax::NodeEditor;

class App;
struct Link;
struct Pin;

// Forward declaration for crude_json
namespace crude_json {
    struct value;
}

class Container
{
public:
    // Identification
    ed::NodeId GetID() const { return m_ID; }
    const std::string& GetName() const { return m_Name; }
    void SetName(const std::string& name) { m_Name = name; }
    
    // ID generation - container provides unique IDs for its nodes
    // This ensures IDs are never reused even if nodes are deleted
    int GetNextId() { return m_NextId++; }
    int GetCurrentId() const { return m_NextId; }  // For checking what next ID will be
    
    // Container flags/state
    bool IsHidden() const;
    void SetHidden(bool hidden);
    bool IsActive() const;  // Not disabled
    bool IsDisabled() const;
    void SetActive(bool active);
    void SetDisabled(bool disabled);
    bool IsRunning() const;
    void SetRunning(bool running);
    bool HasError() const;
    void SetError(bool error);
    
    // Ownership
    // NOTE: Store IDs instead of pointers to avoid invalidation when m_Nodes vector reallocates
    std::vector<ed::NodeId> m_NodeIds;    // Node IDs owned by this container (look up via App::FindNode)
    std::vector<ed::LinkId> m_LinkIds;    // Link IDs owned by this container (look up via App::FindLink)
    
    // Convenience: Get actual node pointers (does lookup via App)
    // Pass App* to resolve IDs to pointers
    std::vector<Node*> GetNodes(App* app) const;
    std::vector<Link*> GetLinks(App* app) const;
    
    // NOTE: Nodes and Links are owned by RootContainer, not stored here
    // Container only stores IDs (m_NodeIds, m_LinkIds) and resolves via GetRootContainer()
    std::vector<Container*> m_Children;  // Nested containers (groups)
    Container* m_Parent;                  // Parent container (nullptr for root containers)
    
    // Management
    virtual void AddNode(Node* node);
    virtual void RemoveNode(Node* node);
    virtual void AddLink(Link* link);
    virtual void RemoveLink(Link* link);
    virtual void AddChildContainer(Container* container);
    virtual void RemoveChildContainer(Container* container);
    
    // Query
    Node* FindNode(ed::NodeId nodeId);
    Link* FindLink(ed::LinkId linkId);  // DEPRECATED: Uses deprecated m_Links
    Link* FindLink(ed::LinkId linkId, App* app);  // Preferred: Uses App::FindLink() to resolve IDs
    Pin* FindPin(ed::PinId pinId);  // DEPRECATED: Uses deprecated m_Nodes
    Pin* FindPin(ed::PinId pinId, App* app);  // Preferred: Uses GetNodes() to resolve IDs
    Container* FindContainer(ed::NodeId nodeId);  // Recursive search (this + children)
    bool ContainsNode(ed::NodeId nodeId) const;
    bool ContainsLink(ed::LinkId linkId) const;
    
    // Helper to get root container (walks up parent chain)
    class RootContainer* GetRootContainer() const;
    
    // Execution
    virtual void Run(App* app);  // Execute container contents (only if active)
    
    // Rendering
    virtual void Render(App* app, Pin* newLinkPin);  // Only if not hidden
    
    // Serialization
    virtual void Serialize(struct crude_json::value& json) const;
    virtual void Deserialize(const struct crude_json::value& json, App* app);
    
    // Validation
    virtual bool CanAddNode(Node* node) const { return true; }
    virtual bool CanAddLink(Link* link) const;  // Check if link is valid for this container (both pins must be in this container)
    
    // State management
    uint32_t GetFlags() const { return m_Flags; }
    void SetFlag(uint32_t flag, bool value);
    bool HasFlag(uint32_t flag) const { return (m_Flags & flag) != 0; }
    
protected:
    Container(int id, const char* name);
    virtual ~Container();
    
    ed::NodeId m_ID;
    std::string m_Name;
    int m_NextId = 1;  // Container-local ID generator (ensures no ID reuse)
    
    // Container flags
    enum ContainerFlags {
        ContainerFlag_Hidden   = 1 << 0,
        ContainerFlag_Disabled = 1 << 1,
        ContainerFlag_Running  = 1 << 2,
        ContainerFlag_Error    = 1 << 3
    };
    uint32_t m_Flags = 0;  // Bitmask of ContainerFlags
};

