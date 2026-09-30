#include "container.h"
#include "../app.h"
#include "../types.h"
#include <algorithm>

Container::Container(int id, const char* name)
    : m_ID(id), m_Name(name ? name : ""), m_Parent(nullptr), m_Flags(0), m_NextId(1)
{
    // m_NextId initialized in member initializer list
}

Container::~Container()
{
    // Clean up child containers (delete them before this container is destroyed)
    for (Container* child : m_Children)
    {
        delete child;
    }
    m_Children.clear();
}

// Container flags/state
bool Container::IsHidden() const
{
    return (m_Flags & ContainerFlag_Hidden) != 0;
}

void Container::SetHidden(bool hidden)
{
    SetFlag(ContainerFlag_Hidden, hidden);
}

bool Container::IsActive() const
{
    return !IsDisabled();
}

bool Container::IsDisabled() const
{
    return (m_Flags & ContainerFlag_Disabled) != 0;
}

void Container::SetActive(bool active)
{
    SetDisabled(!active);
}

void Container::SetDisabled(bool disabled)
{
    SetFlag(ContainerFlag_Disabled, disabled);
}

bool Container::IsRunning() const
{
    return (m_Flags & ContainerFlag_Running) != 0;
}

void Container::SetRunning(bool running)
{
    SetFlag(ContainerFlag_Running, running);
}

bool Container::HasError() const
{
    return (m_Flags & ContainerFlag_Error) != 0;
}

void Container::SetError(bool error)
{
    SetFlag(ContainerFlag_Error, error);
}

void Container::SetFlag(uint32_t flag, bool value)
{
    if (value)
        m_Flags |= flag;
    else
        m_Flags &= ~flag;
}

// Management
void Container::AddNode(Node* node)
{
    if (!node || ContainsNode(node->ID))
        return;
    
    // Store ID instead of pointer to avoid invalidation when m_Nodes vector reallocates
    m_NodeIds.push_back(node->ID);
    
    // DO NOT update deprecated m_Nodes vector - it contains invalidated pointers after reallocation
    // All code should use GetNodes(App*) instead, which resolves IDs to fresh pointers
    // For backward compatibility, we'll try to keep m_Nodes in sync, but it's unreliable
    // TODO: Remove deprecated m_Nodes/m_Links vectors entirely
}

std::vector<Node*> Container::GetNodes(App* app) const
{
    std::vector<Node*> result;
    
    // For root containers, nodes are stored directly in RootContainer::m_Nodes
    // For nested containers (groups), we need to resolve via root container
    if (auto* rootContainer = GetRootContainer())
    {
        // Root container - get nodes directly
        for (auto nodeId : m_NodeIds)
        {
            if (auto* node = rootContainer->FindNode(nodeId))
            {
                result.push_back(node);
            }
        }
    }
    else if (app)
    {
        // Nested container - resolve via App (which will search all root containers)
        for (auto nodeId : m_NodeIds)
        {
            if (auto* node = app->FindNode(nodeId))
            {
                result.push_back(node);
            }
        }
    }
    
    return result;
}

std::vector<Link*> Container::GetLinks(App* app) const
{
    std::vector<Link*> result;
    
    // For root containers, links are stored directly in RootContainer::m_Links
    // For nested containers (groups), we need to resolve via root container
    if (auto* rootContainer = GetRootContainer())
    {
        // Root container - get links directly
        for (auto linkId : m_LinkIds)
        {
            if (auto* link = rootContainer->FindLink(linkId))
            {
                result.push_back(link);
            }
        }
    }
    else if (app)
    {
        // Nested container - resolve via App (which will search all root containers)
        for (auto linkId : m_LinkIds)
        {
            if (auto* link = app->FindLink(linkId))
            {
                result.push_back(link);
            }
        }
    }
    
    return result;
}

RootContainer* Container::GetRootContainer() const
{
    // Walk up the parent chain to find root container
    const Container* current = this;
    while (current->m_Parent)
    {
        current = current->m_Parent;
    }
    
    // Cast to RootContainer (should be safe if this is a root or child of root)
    return dynamic_cast<RootContainer*>(const_cast<Container*>(current));
}

void Container::RemoveNode(Node* node)
{
    if (!node)
    {
        printf("[DELETE] Container::RemoveNode: Null node pointer\n");
        fflush(stdout);
        return;
    }
    
    int nodeId = -1;
    try {
        nodeId = ToRuntimeId(node->ID);
    } catch (...) {
        printf("[DELETE] Container::RemoveNode: Cannot access node ID (corrupted pointer %p)\n", (void*)node);
        fflush(stdout);
        return;  // Cannot remove without valid ID
    }
    
    printf("[DELETE] Container::RemoveNode: Removing node %d (ptr=%p) from container\n", 
           nodeId, (void*)node);
    fflush(stdout);
    
    // Remove from ID list only - actual node is stored in RootContainer::m_Nodes
    auto idIt = std::find(m_NodeIds.begin(), m_NodeIds.end(), node->ID);
    if (idIt != m_NodeIds.end())
    {
        printf("[DELETE] Container::RemoveNode: Node %d found in ID list, erasing\n", nodeId);
        fflush(stdout);
        m_NodeIds.erase(idIt);
    }
    else
    {
        printf("[DELETE] Container::RemoveNode: Node %d NOT found in container ID list\n", nodeId);
        fflush(stdout);
    }
}

void Container::AddLink(Link* link)
{
    if (!link)
    {
        printf("[LINK_DRAG] Container::AddLink: Null link pointer!\n");
        fflush(stdout);
        return;
    }
    
    const int linkId = ToRuntimeId(link->ID);

    if (ContainsLink(link->ID))
    {
        printf("[LINK_DRAG] Container::AddLink: Link ID %lld already exists in container, REJECTING!\n", 
               static_cast<long long>(linkId));
        printf("[LINK_DRAG] Container::AddLink: Current link IDs in container: ");
        for (auto id : m_LinkIds)
            printf("%lld ", (long long)id.Get());
        printf("\n");
        fflush(stdout);
        return;
    }
    
    printf("[LINK_DRAG] Container::AddLink: Adding link ID=%lld to container\n", static_cast<long long>(linkId));
    fflush(stdout);
    
    // Store ID - actual link is stored in RootContainer::m_Links
    m_LinkIds.push_back(link->ID);
    
    printf("[LINK_DRAG] Container::AddLink: Successfully added. Container now has %zu link IDs\n", m_LinkIds.size());
    fflush(stdout);
}

void Container::RemoveLink(Link* link)
{
    if (!link)
        return;
    
    // Remove from ID list only - actual link is stored in RootContainer::m_Links
    auto idIt = std::find(m_LinkIds.begin(), m_LinkIds.end(), link->ID);
    if (idIt != m_LinkIds.end())
        m_LinkIds.erase(idIt);
}

void Container::AddChildContainer(Container* container)
{
    if (!container)
        return;
    
    // Check if already a child
    auto it = std::find(m_Children.begin(), m_Children.end(), container);
    if (it != m_Children.end())
        return;
    
    container->m_Parent = this;
    m_Children.push_back(container);
}

void Container::RemoveChildContainer(Container* container)
{
    if (!container)
        return;
    
    auto it = std::find(m_Children.begin(), m_Children.end(), container);
    if (it != m_Children.end())
    {
        (*it)->m_Parent = nullptr;
        m_Children.erase(it);
    }
}

// Query
Node* Container::FindNode(ed::NodeId nodeId)
{
    // Check if this container owns this node ID
    if (std::find(m_NodeIds.begin(), m_NodeIds.end(), nodeId) == m_NodeIds.end())
    {
        // Not in this container, check children
        for (auto* child : m_Children)
        {
            if (auto* node = child->FindNode(nodeId))
                return node;
        }
        return nullptr;
    }
    
    // This container owns the node ID - resolve via root container
    if (auto* rootContainer = GetRootContainer())
    {
        return rootContainer->FindNode(nodeId);
    }
    
    // No root container found - caller should use App::FindNode() directly
    return nullptr;
}

Link* Container::FindLink(ed::LinkId linkId)
{
    // Check if this container owns this link ID
    bool ownsLink = (std::find(m_LinkIds.begin(), m_LinkIds.end(), linkId) != m_LinkIds.end());
    
    if (ownsLink)
    {
        // This container owns the link ID - resolve via root container
        if (auto* rootContainer = GetRootContainer())
        {
            return rootContainer->FindLink(linkId);
        }
        // No root container found - caller should use App::FindLink() directly
        return nullptr;
    }
    
    // Not in this container, recursively search in child containers
    for (auto* child : m_Children)
    {
        if (auto* link = child->FindLink(linkId))
            return link;
    }
    
    return nullptr;
}

Pin* Container::FindPin(ed::PinId pinId)
{
    if (!pinId)
        return nullptr;
    
    // Note: Without App*, we cannot resolve node IDs to pointers
    // This method is deprecated - use FindPin(pinId, App*) instead
    // For now, return nullptr
    
    // Recursively search in child containers
    for (auto* child : m_Children)
    {
        if (auto* pin = child->FindPin(pinId))
            return pin;
    }
    
    return nullptr;
}

Pin* Container::FindPin(ed::PinId pinId, App* app)
{
    if (!pinId || !app)
        return nullptr;
    
    // Use GetNodes() to properly resolve IDs to pointers
    auto nodes = GetNodes(app);
    for (auto* node : nodes)
    {
        if (!node)
            continue;
        
        // Validate node pointer isn't corrupted
        uintptr_t nodePtrValue = reinterpret_cast<uintptr_t>(node);
        if (nodePtrValue < 0x1000 || nodePtrValue == 0xFFFFFFFFFFFFFFFFULL)
            continue;
            
        for (auto& pin : node->Inputs)
        {
            if (pin.ID == pinId)
            {
                // Ensure pin's Node pointer is correct (should be set by BuildNode after AddNode)
                if (pin.Node != node)
                {
                    pin.Node = node;  // Fix to point to actual node
                }
                return &pin;
            }
        }
        
        for (auto& pin : node->Outputs)
        {
            if (pin.ID == pinId)
            {
                // Ensure pin's Node pointer is correct (should be set by BuildNode after AddNode)
                if (pin.Node != node)
                {
                    pin.Node = node;  // Fix to point to actual node
                }
                return &pin;
            }
        }
    }
    
    // Recursively search in child containers
    for (auto* child : m_Children)
    {
        if (auto* pin = child->FindPin(pinId, app))
            return pin;
    }
    
    return nullptr;
}

Container* Container::FindContainer(ed::NodeId nodeId)
{
    // Check if node is in this container
    if (FindNode(nodeId))
        return this;
    
    // Recursively search children
    for (auto* child : m_Children)
    {
        if (auto* found = child->FindContainer(nodeId))
            return found;
    }
    
    return nullptr;
}

bool Container::ContainsNode(ed::NodeId nodeId) const
{
    // Check ID list
    return std::find(m_NodeIds.begin(), m_NodeIds.end(), nodeId) != m_NodeIds.end();
}

bool Container::ContainsLink(ed::LinkId linkId) const
{
    // Check ID list
    return std::find(m_LinkIds.begin(), m_LinkIds.end(), linkId) != m_LinkIds.end();
}

bool Container::CanAddLink(Link* link) const
{
    if (!link)
        return false;
    
    // Note: This method is const and cannot resolve node IDs to pointers without App*
    // The proper implementation would need App* to resolve pins to nodes
    // For now, we'll check if we can find the link by ID (basic validation)
    // Actual validation should be done at a higher level with App* available
    
    // Both pins must belong to nodes in this container
    // Without App*, we can't resolve pins to nodes, so we return true
    // (The caller should validate with proper context)
    return true;
}

// Execution (stub - overridden in derived classes)
void Container::Run(App* app)
{
    // Base implementation does nothing
    // Overridden in RootContainer and BehaviorGraph
}

// Rendering (stub - overridden in derived classes)
void Container::Render(App* app, Pin* newLinkPin)
{
    // Base implementation does nothing
    // Overridden in RootContainer and BehaviorGraph
}

// Serialization (stub - overridden in derived classes)
void Container::Serialize(crude_json::value& json) const
{
    // Base implementation does nothing
    // Overridden in RootContainer and BehaviorGraph
}

void Container::Deserialize(const crude_json::value& json, App* app)
{
    // Base implementation does nothing
    // Overridden in RootContainer and BehaviorGraph
}

