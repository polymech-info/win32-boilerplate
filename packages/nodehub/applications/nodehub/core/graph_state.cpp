#include "graph_state.h"
#include "../app.h"
#include "../containers/container.h"
#include <algorithm>

GraphState::GraphState()
    : m_NextId(1)
    , m_ActiveRootContainer(nullptr)
{
}

GraphState::~GraphState()
{
    // Clean up root containers
    for (RootContainer* container : m_RootContainers)
    {
        delete container;
    }
    m_RootContainers.clear();
}

int GraphState::GetNextId()
{
    return m_NextId++;
}

ed::LinkId GraphState::GetNextLinkId()
{
    return ed::LinkId(GetNextId());
}

Node* GraphState::FindNode(ed::NodeId id)
{
    if (!id)
        return nullptr;
    
    // Search all root containers (nodes are owned by containers, not stored globally)
    for (RootContainer* container : m_RootContainers)
    {
        if (Node* node = container->FindNode(id))
            return node;
    }
    
    return nullptr;
}

Link* GraphState::FindLink(ed::LinkId id)
{
    if (!id)
        return nullptr;
    
    // Search all root containers (links are owned by containers, not stored globally)
    for (RootContainer* container : m_RootContainers)
    {
        if (Link* link = container->FindLink(id))
            return link;
    }
    
    return nullptr;
}

// Note: FindPin removed from GraphState - it requires App* for container lookup
// Use App::FindPin instead

Node* GraphState::AddNode(const Node& node)
{
    // Add node to active root container (nodes are owned by containers)
    if (!m_ActiveRootContainer)
        return nullptr;
    
    return m_ActiveRootContainer->AddNode(node);
}

bool GraphState::RemoveNode(ed::NodeId id)
{
    // Remove node from the container that owns it
    for (RootContainer* container : m_RootContainers)
    {
        if (container->FindNode(id))
        {
            return container->RemoveNode(id);
        }
    }
    return false;
}

Link* GraphState::AddLink(const Link& link)
{
    // Add link to active root container (links are owned by containers)
    if (!m_ActiveRootContainer)
        return nullptr;
    
    return m_ActiveRootContainer->AddLink(link);
}

bool GraphState::RemoveLink(ed::LinkId id)
{
    // Remove link from the container that owns it
    for (RootContainer* container : m_RootContainers)
    {
        if (container->FindLink(id))
        {
            return container->RemoveLink(id);
        }
    }
    return false;
}

RootContainer* GraphState::AddRootContainer(const std::string& filename)
{
    int containerId = GetNextId();
    RootContainer* container = new RootContainer(filename, containerId);
    m_RootContainers.push_back(container);
    
    // Set as active if it's the first one
    if (!m_ActiveRootContainer)
    {
        m_ActiveRootContainer = container;
    }
    
    return container;
}

void GraphState::RemoveRootContainer(RootContainer* container)
{
    if (!container)
        return;
    
    // Remove from vector
    auto it = std::find(m_RootContainers.begin(), m_RootContainers.end(), container);
    if (it != m_RootContainers.end())
    {
        m_RootContainers.erase(it);
    }
    
    // Clear active if it was removed
    if (m_ActiveRootContainer == container)
    {
        m_ActiveRootContainer = m_RootContainers.empty() ? nullptr : m_RootContainers[0];
    }
    
    delete container;
}

void GraphState::SetActiveRootContainer(RootContainer* container)
{
    // Verify container is in our list
    auto it = std::find(m_RootContainers.begin(), m_RootContainers.end(), container);
    if (it != m_RootContainers.end() || container == nullptr)
    {
        m_ActiveRootContainer = container;
    }
}

Container* GraphState::FindContainerForNode(ed::NodeId nodeId)
{
    if (!nodeId)
        return nullptr;
    
    // Search all root containers recursively
    for (RootContainer* rootContainer : m_RootContainers)
    {
        Container* container = rootContainer->FindContainer(nodeId);
        if (container)
            return container;
    }
    
    return nullptr;
}

std::vector<Node*> GraphState::GetAllNodes() const
{
    std::vector<Node*> nodes;
    
    // Collect nodes from all root containers
    for (const RootContainer* container : m_RootContainers)
    {
        auto containerNodes = container->GetAllNodes();
        nodes.insert(nodes.end(), containerNodes.begin(), containerNodes.end());
    }
    
    return nodes;
}

std::vector<Link*> GraphState::GetAllLinks() const
{
    std::vector<Link*> links;
    
    // Collect links from all root containers
    for (const RootContainer* container : m_RootContainers)
    {
        auto containerLinks = container->GetAllLinks();
        links.insert(links.end(), containerLinks.begin(), containerLinks.end());
    }
    
    return links;
}

bool GraphState::IsPinLinked(ed::PinId id) const
{
    if (!id)
        return false;

    // Search through all links in all containers
    for (const RootContainer* container : m_RootContainers)
    {
        auto links = container->GetAllLinks();
        for (const Link* link : links)
        {
            if (link && (link->StartPinID == id || link->EndPinID == id))
                return true;
        }
    }

    return false;
}

bool GraphState::IsLinkDuplicate(ed::PinId startPinId, ed::PinId endPinId) const
{
    if (!startPinId || !endPinId)
        return false;

    // Check links from active root container if available, otherwise check all containers
    const RootContainer* container = m_ActiveRootContainer;
    
    if (container)
    {
        auto links = container->GetAllLinks();
        for (const Link* link : links)
        {
            if (!link) continue;
            if ((link->StartPinID == startPinId && link->EndPinID == endPinId) ||
                (link->StartPinID == endPinId && link->EndPinID == startPinId))
            {
                return true;
            }
        }
    }
    else
    {
        // Check all containers
        for (const RootContainer* container : m_RootContainers)
        {
            auto links = container->GetAllLinks();
            for (const Link* link : links)
            {
                if (!link) continue;
                if ((link->StartPinID == startPinId && link->EndPinID == endPinId) ||
                    (link->StartPinID == endPinId && link->EndPinID == startPinId))
                {
                    return true;
                }
            }
        }
    }

    return false;
}

Link* GraphState::FindLinkConnectedToPin(ed::PinId pinId) const
{
    if (!pinId)
        return nullptr;

    // Check links from active root container if available, otherwise check all containers
    const RootContainer* container = m_ActiveRootContainer;
    
    if (container)
    {
        auto links = container->GetAllLinks();
        for (Link* link : links)
        {
            if (link && (link->StartPinID == pinId || link->EndPinID == pinId))
                return link;
        }
    }
    else
    {
        // Check all containers
        for (const RootContainer* container : m_RootContainers)
        {
            auto links = container->GetAllLinks();
            for (Link* link : links)
            {
                if (link && (link->StartPinID == pinId || link->EndPinID == pinId))
                    return link;
            }
        }
    }

    return nullptr;
}

void GraphState::SaveGraph(const std::string& filename, App* app)
{
    // TODO: Move SaveGraph implementation from App
    // For now, this is a placeholder
    (void)filename;
    (void)app;
}

void GraphState::LoadGraph(const std::string& filename, App* app)
{
    // TODO: Move LoadGraph implementation from App
    // For now, this is a placeholder
    (void)filename;
    (void)app;
}

