#include "root_container.h"
#include "../app.h"
#include "../types.h"
#include "../blocks/block.h"
#include "../blocks/parameter_node.h"
#include <imgui_node_editor.h>

namespace ed = ax::NodeEditor;

RootContainer::RootContainer(const std::string& filename, int id)
    : Container(id, "Root"), m_Filename(filename), m_IsDirty(false)
{
}

RootContainer::~RootContainer()
{
    // Clean up BlockInstance and ParameterInstance for all nodes BEFORE maps are destroyed
    // This prevents memory leaks and potential double-free issues
    // NOTE: Nodes may have already had their instances deleted via DeleteNodeAndInstances,
    // so we check for nullptr and validate pointers to avoid double-free
    for (auto& pair : m_Nodes)
    {
        Node& node = pair.second;
        
        // Clean up BlockInstance (for block-based nodes)
        // Only delete if pointer is valid and not already deleted
        if (node.BlockInstance)
        {
            // Validate pointer is not corrupted or already freed
            uintptr_t ptrValue = reinterpret_cast<uintptr_t>(node.BlockInstance);
            if (ptrValue != 0xFFFFFFFFFFFFFFFFULL && ptrValue > 0x1000) // Basic sanity check
            {
                try {
                    delete node.BlockInstance;
                } catch (...) {
                    // Silently ignore exceptions during shutdown cleanup
                }
            }
            node.BlockInstance = nullptr;
        }
        
        // Clean up ParameterInstance (for parameter nodes)
        // Only delete if pointer is valid and not already deleted
        if (node.ParameterInstance)
        {
            // Validate pointer is not corrupted or already freed
            uintptr_t ptrValue = reinterpret_cast<uintptr_t>(node.ParameterInstance);
            if (ptrValue != 0xFFFFFFFFFFFFFFFFULL && ptrValue > 0x1000) // Basic sanity check
            {
                try {
                    delete node.ParameterInstance;
                } catch (...) {
                    // Silently ignore exceptions during shutdown cleanup
                }
            }
            node.ParameterInstance = nullptr;
        }
    }
    
    // Clear maps explicitly (though they'll be destroyed automatically after this)
    // This makes the order of destruction clear
    m_Nodes.clear();
    m_Links.clear();
    
    // Base Container destructor will clean up child containers
}

Node* RootContainer::FindNode(ed::NodeId id)
{
    if (!id)
        return nullptr;
    
    auto it = m_Nodes.find(id);
    if (it != m_Nodes.end())
        return &it->second;
    
    return nullptr;
}

Link* RootContainer::FindLink(ed::LinkId id)
{
    if (!id)
        return nullptr;
    
    auto it = m_Links.find(id);
    if (it != m_Links.end())
        return &it->second;
    
    return nullptr;
}

Pin* RootContainer::FindPin(ed::PinId id, App* app)
{
    if (!id)
        return nullptr;
    
    // Search all nodes in this container
    for (auto& pair : m_Nodes)
    {
        Node& node = pair.second;
        
        // Validate node pointer isn't corrupted before accessing pins
        uintptr_t nodePtrValue = reinterpret_cast<uintptr_t>(&node);
        if (nodePtrValue < 0x1000 || nodePtrValue == 0xFFFFFFFFFFFFFFFFULL)
            continue;
        
        // Check inputs
        for (auto& pin : node.Inputs)
        {
            if (pin.ID == id)
            {
                // Ensure pin's Node pointer is correct (should be set by BuildNode after AddNode)
                if (pin.Node != &node)
                {
                    pin.Node = &node;  // Fix to point to actual node in container
                }
                return &pin;
            }
        }
        
        // Check outputs
        for (auto& pin : node.Outputs)
        {
            if (pin.ID == id)
            {
                // Ensure pin's Node pointer is correct (should be set by BuildNode after AddNode)
                if (pin.Node != &node)
                {
                    printf("[FindPin] FIXING pin %d - Node pointer incorrect (was %p, setting to %p)\n",
                           id.Get(), (void*)pin.Node, (void*)&node);
                    fflush(stdout);
                    pin.Node = &node;  // Fix to point to actual node in container
                }
                return &pin;
            }
        }
    }
    
    // Also search in child containers recursively
    for (Container* child : m_Children)
    {
        if (Pin* pin = child->FindPin(id, app))
            return pin;
    }
    
    return nullptr;
}

std::vector<Node*> RootContainer::GetAllNodes() const
{
    std::vector<Node*> nodes;
    nodes.reserve(m_Nodes.size());
    
    for (auto& pair : m_Nodes)
    {
        nodes.push_back(const_cast<Node*>(&pair.second));  // Const cast needed
    }
    
    return nodes;
}

std::vector<Link*> RootContainer::GetAllLinks() const
{
    std::vector<Link*> links;
    links.reserve(m_Links.size());
    
    for (auto& pair : m_Links)
    {
        links.push_back(const_cast<Link*>(&pair.second));  // Const cast needed
    }
    
    return links;
}

Node* RootContainer::AddNode(const Node& node)
{
    auto [it, inserted] = m_Nodes.emplace(node.ID, node);
    if (!inserted)
        return nullptr;  // Node with this ID already exists
    
    // Also add to container's ID list
    m_NodeIds.push_back(node.ID);
    
    return &it->second;
}

bool RootContainer::RemoveNode(ed::NodeId id)
{
    auto it = m_Nodes.find(id);
    if (it != m_Nodes.end())
    {
        m_Nodes.erase(it);
        
        // Remove from ID list
        auto idIt = std::find(m_NodeIds.begin(), m_NodeIds.end(), id);
        if (idIt != m_NodeIds.end())
            m_NodeIds.erase(idIt);
        
        return true;
    }
    return false;
}

Link* RootContainer::AddLink(const Link& link)
{
    auto [it, inserted] = m_Links.emplace(link.ID, link);
    if (!inserted)
        return nullptr;  // Link with this ID already exists
    
    // Also add to container's ID list
    m_LinkIds.push_back(link.ID);
    
    return &it->second;
}

bool RootContainer::RemoveLink(ed::LinkId id)
{
    auto it = m_Links.find(id);
    if (it != m_Links.end())
    {
        m_Links.erase(it);
        
        // Remove from ID list
        auto idIt = std::find(m_LinkIds.begin(), m_LinkIds.end(), id);
        if (idIt != m_LinkIds.end())
            m_LinkIds.erase(idIt);
        
        return true;
    }
    return false;
}

void RootContainer::Run(App* app)
{
    // Only execute if active (not disabled)
    if (IsDisabled())
        return;
    
    // Set running flag
    SetRunning(true);
    
    // Execute blocks in this container
    // Note: The actual execution logic is still in App::ExecuteRuntimeStep()
    // which will iterate through m_Nodes. We'll update that to use the container.
    // For now, this is a placeholder.
    
    // Execute child containers (groups) - only if active
    for (auto* child : m_Children)
    {
        if (child->IsDisabled())
            continue;
            
        // Child containers (groups) will be executed by runtime system
        // when their inputs are activated
    }
    
    // Clear running flag
    SetRunning(false);
}

void RootContainer::Render(App* app, Pin* newLinkPin)
{
    // Only render if not hidden
    if (IsHidden())
        return;
    
    // Render this container's nodes
    // Note: Actual rendering is still in App::RenderNodes()
    // which will iterate through m_Nodes. We'll update that to use the container.
    // For now, this is a placeholder.
    
    // Render links between nodes in this container
    // Note: Actual link rendering is still in App::RenderLinks()
    // We'll update that to use the container.
    
    // Render child containers (groups) - only if not hidden
    for (auto* child : m_Children)
    {
        if (child->IsHidden())
            continue;
            
        // Child containers (groups) will render themselves
        child->Render(app, newLinkPin);
    }
}
