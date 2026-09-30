#pragma once
#include "container.h"
#include "../types.h"
#include <string>
#include <map>

namespace ed = ax::NodeEditor;

class App;
struct Pin;

/**
 * RootContainer - Owns actual Node and Link objects for its graph
 * 
 * Each root container (one per file) owns all nodes and links in its graph.
 * This provides proper ownership and separation between different graph files.
 */
class RootContainer : public Container
{
public:
    RootContainer(const std::string& filename, int id);
    virtual ~RootContainer();
    
    // Root-specific
    const std::string& GetFilename() const { return m_Filename; }
    void SetFilename(const std::string& filename) { m_Filename = filename; }
    bool IsDirty() const { return m_IsDirty; }
    void SetDirty(bool dirty) { m_IsDirty = dirty; }
    
    // Node/Link ownership - RootContainer owns the actual objects
    std::map<ed::NodeId, Node, NodeIdLess> m_Nodes;  // Node storage indexed by ID
    std::map<ed::LinkId, Link, LinkIdLess> m_Links;   // Link storage indexed by ID
    
    // Node/Link lookup (search this container only)
    Node* FindNode(ed::NodeId id);
    Link* FindLink(ed::LinkId id);
    Pin* FindPin(ed::PinId id, App* app);  // App* needed for nested container lookup
    
    // Direct access to all nodes/links (for iteration)
    std::vector<Node*> GetAllNodes() const;
    std::vector<Link*> GetAllLinks() const;
    
    // Node/Link management
    Node* AddNode(const Node& node);
    bool RemoveNode(ed::NodeId id);
    Link* AddLink(const Link& link);
    bool RemoveLink(ed::LinkId id);
    
    // Root has no interface pins
    void Run(App* app) override;  // Execute all blocks in root (only if active)
    void Render(App* app, Pin* newLinkPin) override;  // Render root + children (only if not hidden)
    
private:
    std::string m_Filename;
    bool m_IsDirty = false;
};

