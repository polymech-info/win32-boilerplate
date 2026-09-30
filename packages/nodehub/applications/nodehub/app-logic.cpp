#include "app.h"
#include "blocks/block.h"
#include "blocks/parameter_node.h"
#include "containers/root_container.h"
#include "crude_json.h"
#include "types.h"
#include "Logging.h"
#include <fstream>
#include <algorithm>

namespace
{
    enum class SerializedPinType
    {
        InputParameter,
        OutputParameter,
        InputFlow,
        OutputFlow
    };

    inline SerializedPinType DetermineSerializedPinType(const Pin& pin)
    {
        const bool isFlow = (pin.Type == PinType::Flow);
        const bool isInput = (pin.Kind == PinKind::Input);

        if (isFlow)
            return isInput ? SerializedPinType::InputFlow : SerializedPinType::OutputFlow;

        return isInput ? SerializedPinType::InputParameter : SerializedPinType::OutputParameter;
    }

    inline const char* SerializedPinTypeToString(SerializedPinType type)
    {
        switch (type)
        {
            case SerializedPinType::InputParameter:  return "InputParameter";
            case SerializedPinType::OutputParameter: return "OutputParameter";
            case SerializedPinType::InputFlow:       return "InputFlow";
            case SerializedPinType::OutputFlow:      return "OutputFlow";
            default:                                 return "InputParameter";
        }
    }

    inline bool TryParseSerializedPinType(const std::string& value, SerializedPinType& outType)
    {
        if (value == "InputParameter")      { outType = SerializedPinType::InputParameter;  return true; }
        if (value == "OutputParameter")     { outType = SerializedPinType::OutputParameter; return true; }
        if (value == "InputFlow")           { outType = SerializedPinType::InputFlow;       return true; }
        if (value == "OutputFlow")          { outType = SerializedPinType::OutputFlow;      return true; }
        return false;
    }

    inline bool SerializedPinTypeIsFlow(SerializedPinType type)
    {
        return type == SerializedPinType::InputFlow || type == SerializedPinType::OutputFlow;
    }

    inline bool SerializedPinTypeIsInput(SerializedPinType type)
    {
        return type == SerializedPinType::InputFlow || type == SerializedPinType::InputParameter;
    }
}

int App::GetNextId()
{
    return m_GraphState.GetNextId();
}

ed::LinkId App::GetNextLinkId()
{
    return m_GraphState.GetNextLinkId();
}

void App::TouchNode(ed::NodeId id)
{
    m_NodeTouchTime[id] = m_TouchTime;
}

float App::GetTouchProgress(ed::NodeId id)
{
    auto it = m_NodeTouchTime.find(id);
    if (it != m_NodeTouchTime.end() && it->second > 0.0f)
        return (m_TouchTime - it->second) / m_TouchTime;
    else
        return 0.0f;
}

void App::UpdateTouch()
{
    const auto deltaTime = ImGui::GetIO().DeltaTime;
    for (auto& entry : m_NodeTouchTime)
    {
        if (entry.second > 0.0f)
            entry.second -= deltaTime;
    }
}

Node* App::FindNode(ed::NodeId id)
{
    // Delegate to GraphState which searches all root containers
    return m_GraphState.FindNode(id);
}

Link* App::FindLink(ed::LinkId id)
{
    // Delegate to GraphState which searches all root containers
    return m_GraphState.FindLink(id);
}

Pin* App::FindPin(ed::PinId id)
{
    if (!id)
        return nullptr;
    
    auto* activeRoot = m_GraphState.GetActiveRootContainer();
    if (!activeRoot)
        return nullptr;

    // Search in active root container (and recursively in child containers)
    // Use the App* overload to properly resolve IDs to node pointers
    return activeRoot->FindPin(id, this);
}

bool App::IsPinLinked(ed::PinId id)
{
    return m_GraphState.IsPinLinked(id);
}

bool App::CanCreateLink(Pin* a, Pin* b)
{
    if (!a || !b || a == b || a->Kind == b->Kind || a->Type != b->Type || a->Node == b->Node)
        return false;

    // Prevent connecting source nodes with their shortcuts
    Node* nodeA = a->Node;
    Node* nodeB = b->Node;
    
    if (nodeA && nodeB && nodeA->Type == NodeType::Parameter && nodeB->Type == NodeType::Parameter)
    {
        if (nodeA->ParameterInstance && nodeB->ParameterInstance)
        {
            // Check if nodeA is a source and nodeB is its shortcut
            if (nodeA->ParameterInstance->IsSource() && 
                nodeB->ParameterInstance->GetSourceID() == nodeA->ParameterInstance->GetID())
            {
                return false;  // Cannot connect source to its shortcut
            }
            
            // Check if nodeB is a source and nodeA is its shortcut
            if (nodeB->ParameterInstance->IsSource() && 
                nodeA->ParameterInstance->GetSourceID() == nodeB->ParameterInstance->GetID())
            {
                return false;  // Cannot connect shortcut to its source
            }
        }
    }

    return true;
}

bool App::IsLinkDuplicate(ed::PinId startPinId, ed::PinId endPinId)
{
    if (!startPinId || !endPinId)
        return false;

    // Check links from active root container if available, otherwise use m_Links
    auto* container = GetActiveRootContainer();
    std::vector<Link*> linksToCheck;
    
    if (container)
    {
        // Use GetLinks() to resolve IDs to pointers (safe from reallocation)
        linksToCheck = container->GetLinks(this);
    }
    else
    {
        // No container - get links from active root container
        auto* activeRoot = GetActiveRootContainer();
        if (activeRoot)
        {
            linksToCheck = activeRoot->GetAllLinks();
        }
    }

    for (auto* linkPtr : linksToCheck)
    {
        if (!linkPtr) continue;
        auto& link = *linkPtr;
        
        if ((link.StartPinID == startPinId && link.EndPinID == endPinId) ||
            (link.StartPinID == endPinId && link.EndPinID == startPinId))
        {
            return true;
        }
    }

    return false;
}

Link* App::FindLinkConnectedToPin(ed::PinId pinId)
{
    if (!pinId)
        return nullptr;

    // Check links from active root container if available
    auto* container = GetActiveRootContainer();
    std::vector<Link*> linksToCheck;
    
    if (container)
    {
        // Use GetLinks() to resolve IDs to pointers (safe from reallocation)
        linksToCheck = container->GetLinks(this);
    }
    else
    {
        // No container - get links from active root container
        auto* activeRoot = GetActiveRootContainer();
        if (activeRoot)
        {
            linksToCheck = activeRoot->GetAllLinks();
        }
    }

    for (auto* linkPtr : linksToCheck)
    {
        if (!linkPtr) continue;
        auto& link = *linkPtr;
        
        if (link.StartPinID == pinId || link.EndPinID == pinId)
            return &link;
    }

    return nullptr;
}

void App::BuildNode(Node* node)
{
    if (!node)
    {
        return;
    }
    
    for (auto& input : node->Inputs)
    {
        input.Node = node;
        input.Kind = PinKind::Input;
    }

    for (auto& output : node->Outputs)
    {
        output.Node = node;
        output.Kind = PinKind::Output;
    }
}

void App::BuildNodes()
{
    LOG_TRACE("[CHECKPOINT] BuildNodes: Beginning");    
    // Build nodes from active root container
    auto* container = GetActiveRootContainer();
    if (container)
    {
        // Use GetNodes() to resolve IDs to pointers
        auto nodes = container->GetNodes(this);
        
        // Build all container nodes
        int nodeIndex = 0;
        for (auto* node : nodes)
        {
            if (node)
            {
                BuildNode(node);
                nodeIndex++;
            }
        }
    }
    else
    {
        // No container - try to use active root container
        auto* rootContainer = GetActiveRootContainer();
        if (rootContainer)
        {
            auto nodes = rootContainer->GetAllNodes();
            for (auto* node : nodes)
            {
                if (node)
                    BuildNode(node);
            }
        }
    }    
    LOG_TRACE("[CHECKPOINT] BuildNodes: Complete");
}

ImColor App::GetIconColor(PinType type)
{
    switch (type)
    {
        default:
        case PinType::Flow:     return ImColor(255, 255, 255);
        case PinType::Bool:     return ImColor(220,  48,  48);
        case PinType::Int:      return ImColor( 68, 201, 156);
        case PinType::Float:    return ImColor(147, 226,  74);
        case PinType::String:   return ImColor(124,  21, 153);
        case PinType::Object:   return ImColor( 51, 150, 215);
        case PinType::Function: return ImColor(218,   0, 183);
        case PinType::Delegate: return ImColor(255,  48,  48);
    }
}

Node* App::SpawnBlockNode(const char* blockType, int nodeId)
{
    auto* activeRoot = GetActiveRootContainer();
    if (!activeRoot)
    {
        LOG_ERROR("[CREATE] SpawnBlockNode: ERROR - no active root container");
        return nullptr;
    }
        
    // Use container's ID generator to ensure no ID reuse
    if (nodeId < 0)
    {
        nodeId = activeRoot->GetNextId();
    }
    auto block = BlockRegistry::Instance().CreateBlock(blockType, nodeId);
    if (!block)
    {
        return nullptr;
    }
    
    // Create node - will be added to container after construction
    Node newNode(nodeId, block->GetName(), ImColor(128, 195, 248));
    newNode.Type = NodeType::Blueprint;
    newNode.BlockType = blockType;  // Mark as block-based
    newNode.BlockInstance = block;  // Store block instance for rendering
    
    // Generate UUID for node (persistent ID for save/load)
    newNode.UUID = m_UuidIdManager.GenerateUuid();
    
    // Build block structure (adds pins to node)
    block->Build(newNode, this);
    
    // Generate UUIDs for all pins
    for (auto& pin : newNode.Inputs)
    {
        pin.UUID = m_UuidIdManager.GenerateUuid();
    }
    for (auto& pin : newNode.Outputs)
    {
        pin.UUID = m_UuidIdManager.GenerateUuid();
    }
    
    // Add node to active root container FIRST (creates node copy in map)
    Node* addedNode = activeRoot->AddNode(newNode);    
    if (!addedNode)
    {
        return nullptr;
    }
    
    // Register UUID mapping (after node is added to container)
    m_UuidIdManager.RegisterNode(addedNode->UUID, nodeId);
    
    // Register pin UUID mappings
    for (auto& pin : addedNode->Inputs)
    {
        m_UuidIdManager.RegisterPin(pin.UUID, pin.ID.Get());
    }
    for (auto& pin : addedNode->Outputs)
    {
        m_UuidIdManager.RegisterPin(pin.UUID, pin.ID.Get());
    }
    
    // CRITICAL: Build node structure AFTER AddNode
    // This ensures pins' Node pointers point to the actual node in the container, not the temporary copy
    BuildNode(addedNode);
    return addedNode;
}

Node* App::SpawnParameterNode(PinType paramType, int nodeId, ParameterDisplayMode displayMode)
{
    auto* activeRoot = GetActiveRootContainer();
    if (!activeRoot)
    {
        return nullptr;
    }
        
    // Use container's ID generator to ensure no ID reuse
    if (nodeId < 0)
    {
        nodeId = activeRoot->GetNextId();        
    }    
    // Create parameter instance
    auto param = ParameterRegistry::Instance().CreateParameter(paramType, nodeId);
    if (!param)
    {
        return nullptr;
    }
    
    param->SetDisplayMode(displayMode);
    
    
    // Create node - will be added to container after construction
    Node newNode(nodeId, param->GetName(), GetIconColor(paramType));
    
    
    newNode.Type = NodeType::Parameter;
    newNode.ParameterType = paramType;
    newNode.ParameterInstance = param;
    // CRITICAL: Parameter nodes should NEVER have BlockInstance set
    newNode.BlockInstance = nullptr;  // Explicitly ensure it's null
    newNode.BlockType = "";  // Ensure BlockType is empty so IsBlockBased() returns false
    
    
    // Generate UUID for node (persistent ID for save/load)
    newNode.UUID = m_UuidIdManager.GenerateUuid();
    
    // Build node structure (adds output pin)
    param->Build(newNode, this);
    
    // Generate UUIDs for all pins
    for (auto& pin : newNode.Inputs)
    {
        pin.UUID = m_UuidIdManager.GenerateUuid();
    }
    for (auto& pin : newNode.Outputs)
    {
        pin.UUID = m_UuidIdManager.GenerateUuid();
    }
    
    Node* addedNode = activeRoot->AddNode(newNode);
    
    if (!addedNode)
    {
        LOG_ERROR("[CREATE] SpawnParameterNode: ERROR - AddNode returned nullptr");
        return nullptr;
    }
    
    // Register UUID mapping (after node is added to container)
    m_UuidIdManager.RegisterNode(addedNode->UUID, nodeId);
    
    // Register pin UUID mappings
    for (auto& pin : addedNode->Inputs)
    {
        m_UuidIdManager.RegisterPin(pin.UUID, pin.ID.Get());
    }
    for (auto& pin : addedNode->Outputs)
    {
        m_UuidIdManager.RegisterPin(pin.UUID, pin.ID.Get());
    }
    
    BuildNode(addedNode);  
    
    return addedNode;
}

void App::SaveGraph(const std::string& filename, RootContainer* container)
{
    if (!container)
    {
        LOG_WARN("[SAVE] SaveGraph: No container provided, skipping save");
        return;
    }
    
    crude_json::value root;
    
    // Save all nodes with their types AND positions
    auto& nodesArray = root["app_nodes"];
    
    // Get nodes to save from the provided container
    std::vector<const Node*> nodesToSave;
    auto nodes = container->GetNodes(this);
    for (auto* node : nodes)
        if (node) nodesToSave.push_back(node);
    
    // Track orphaned nodes to remove after saving
    std::vector<Node*> orphanedNodes;
    
    for (const auto* nodePtr : nodesToSave)
    {
        if (!nodePtr)
        {
            LOG_WARN("[SAVE] SaveGraph: Skipping null node pointer");
            continue;
        }
        
        // Safe access to node type
        NodeType nodeType;
        try {
            nodeType = nodePtr->Type;
        } catch (...) {
            LOG_WARN("[SAVE] SaveGraph: Cannot access node Type field - skipping corrupted node (ptr={:p})",
                     static_cast<const void*>(nodePtr));
            orphanedNodes.push_back(const_cast<Node*>(nodePtr));
            continue;
        }
        
        // Skip parameter nodes with validation
        if (nodeType == NodeType::Parameter)
        {
            // Validate parameter node has ParameterInstance using safe getter
            if (!nodePtr->GetParameterInstance())
            {
                LOG_WARN("[SAVE] SaveGraph: Parameter node {} has null ParameterInstance - marking for removal",
                         nodePtr->ID.Get());
                orphanedNodes.push_back(const_cast<Node*>(nodePtr));
                continue;
            }
        }
        else
        {
            // Block-based node: validate BlockInstance
            if (nodePtr->IsBlockBased() && !nodePtr->BlockInstance)
            {
                int nodeId = -1;
                try {
                    nodeId = nodePtr->ID.Get();
                } catch (...) {
                    nodeId = -1;
                }
                LOG_WARN("[SAVE] SaveGraph: Block node {} has null BlockInstance - marking for removal",
                         nodeId);
                orphanedNodes.push_back(const_cast<Node*>(nodePtr));
                continue;
            }
            
            // Validate BlockInstance pointer is not corrupted
            if (nodePtr->BlockInstance)
            {
                uintptr_t blockPtrValue = reinterpret_cast<uintptr_t>(nodePtr->BlockInstance);
                if (blockPtrValue < 0x1000 || blockPtrValue > 0x7FFFFFFFFFFFFFFFULL)
                {
                    int nodeId = -1;
                    try {
                        nodeId = nodePtr->ID.Get();
                    } catch (...) {
                        nodeId = -1;
                    }
                    LOG_WARN("[SAVE] SaveGraph: Block node {} has corrupted BlockInstance pointer: 0x{:016X} - marking for removal",
                             nodeId, static_cast<unsigned long long>(blockPtrValue));
                    orphanedNodes.push_back(const_cast<Node*>(nodePtr));
                    continue;
                }
                
                // Validate BlockInstance ID matches node ID
                try {
                    int blockId = nodePtr->BlockInstance->GetID();
                    int nodeId = nodePtr->ID.Get();
                    if (blockId != nodeId)
                    {
                        LOG_WARN("[SAVE] SaveGraph: Block node {} has mismatched BlockInstance ID (block={}, node={}) - marking for removal",
                                 nodeId, blockId, nodeId);
                        orphanedNodes.push_back(const_cast<Node*>(nodePtr));
                        continue;
                    }
                } catch (...) {
                    int nodeId = -1;
                    try {
                        nodeId = nodePtr->ID.Get();
                    } catch (...) {
                        nodeId = -1;
                    }
                    LOG_WARN("[SAVE] SaveGraph: Cannot access BlockInstance ID for node {} - marking for removal",
                             nodeId);
                    orphanedNodes.push_back(const_cast<Node*>(nodePtr));
                    continue;
                }
            }
        }
        
        const auto& node = *nodePtr;
        
        // Safe access to node ID
        int nodeId = -1;
        try {
            nodeId = node.ID.Get();
        } catch (...) {
            LOG_WARN("[SAVE] SaveGraph: Cannot access node ID - skipping");
            orphanedNodes.push_back(const_cast<Node*>(nodePtr));
            continue;
        }
        
        crude_json::value nodeData;
        
        // Save UUID (persistent) - runtime IDs are never saved
        nodeData["uuid_high"] = (double)node.UUID.high;
        nodeData["uuid_low"] = (double)node.UUID.low;
        nodeData["name"] = node.Name;
        
        // Save node position (may fail for corrupted nodes)
        try {
            auto pos = ed::GetNodePosition(node.ID);
            nodeData["position"]["x"] = pos.x;
            nodeData["position"]["y"] = pos.y;
            
            // Save group size if it's a group node
            auto size = ed::GetNodeSize(node.ID);
            if (size.x > 0 || size.y > 0)
            {
                nodeData["size"]["x"] = size.x;
                nodeData["size"]["y"] = size.y;
            }
        } catch (...) {
            LOG_WARN("[SAVE] SaveGraph: Cannot access node position for node {} - using defaults", nodeId);
            nodeData["position"]["x"] = 0.0;
            nodeData["position"]["y"] = 0.0;
        }
        
        // Save based on node type
        if (nodeType == NodeType::Parameter)
        {
            // Parameter node - let it save its own state
            if (node.ParameterInstance)
            {
                node.ParameterInstance->SaveState(const_cast<Node&>(node), nodeData, container, this);
            }
        }
        else if (!node.BlockType.empty())
        {
            // Block-based node
            nodeData["node_type"] = "block";
            nodeData["block_type"] = node.BlockType;
            
            // Save block display mode
            nodeData["block_display_mode"] = (double)static_cast<int>(node.BlockDisplay);
            
            // Call block's SaveState callback (passes container info)
            if (node.BlockInstance)
            {
                node.BlockInstance->SaveState(const_cast<Node&>(node), nodeData, container, this);
            }
        }
        else
        {
            // Hardcoded blueprint node
            nodeData["node_type"] = "hardcoded";
        }
        
        nodesArray.push_back(nodeData);
    }
    
    // Remove orphaned nodes from container after saving
    if (!orphanedNodes.empty())
    {
        LOG_INFO("[SAVE] SaveGraph: Removing {} orphaned node(s) from container", orphanedNodes.size());
        
        for (auto* orphanedNode : orphanedNodes)
        {
            LOG_DEBUG("[DELETE] SaveGraph: Removing orphaned node from container");
            // RemoveNode uses ID-based lookup, safe even if pointer is invalidated
            if (orphanedNode)
                container->RemoveNode(orphanedNode->ID);
        }
    }
    
    // Save all links with control points and pin type info
    auto& linksArray = root["app_links"];
    
    // Get links to save from the provided container
    std::vector<const Link*> linksToSave;
    auto links = container->GetLinks(this);
    for (auto* link : links)
        if (link) linksToSave.push_back(link);
    
    for (const auto* linkPtr : linksToSave)
    {
        if (!linkPtr) continue;
        const auto& link = *linkPtr;
        
        crude_json::value linkData;

        // Save link UUID (persistent) - runtime IDs are never saved
        auto& linkUuid = linkData["uuid"];
        linkUuid["high"] = (double)link.UUID.high;
        linkUuid["low"]  = (double)link.UUID.low;

        // Find pins and their nodes
        auto startPin = FindPin(link.StartPinID);
        auto endPin = FindPin(link.EndPinID);

        if (!startPin || !endPin || !startPin->Node || !endPin->Node)
        {
            LOG_WARN("[SAVE] SaveGraph: Skipping link {} - missing pin or node (startPin={:p}, endPin={:p})",
                     link.ID.Get(), static_cast<const void*>(startPin), static_cast<const void*>(endPin));
            continue;  // Skip invalid links
        }

        // Helper lambda to find relative pin index within its category
        auto getPinRelativeIndex = [](const Pin* pin, Node* node) -> int {
            if (!pin || !node) return -1;

            bool isFlow = (pin->Type == PinType::Flow);
            bool isInput = (pin->Kind == PinKind::Input);

            // Get the appropriate pin list
            const auto& pinList = isInput ? node->Inputs : node->Outputs;

            // Count pins of the same category
            int index = 0;
            for (const auto& p : pinList)
            {
                bool pIsFlow = (p.Type == PinType::Flow);

                // Same category?
                if (pIsFlow == isFlow)
                {
                    if (p.ID == pin->ID)
                        return index;
                    index++;
                }
            }

            return -1;  // Not found
        };

        auto fillEndpoint = [&](const char* key, Pin* pin, int relativeIndex)
        {
            auto& endpoint = linkData[key];

            endpoint["node_id_h"] = (double)pin->Node->UUID.high;
            endpoint["node_id_l"] = (double)pin->Node->UUID.low;
            endpoint["pin_index"] = (double)relativeIndex;
            endpoint["pin_type"] = SerializedPinTypeToString(DetermineSerializedPinType(*pin));
        };

        // Calculate relative indices
        int startPinIndex = getPinRelativeIndex(startPin, startPin->Node);
        int endPinIndex = getPinRelativeIndex(endPin, endPin->Node);

        if (startPinIndex < 0 || endPinIndex < 0)
        {
            LOG_WARN("[SAVE] SaveGraph: Skipping link {} - couldn't calculate relative pin index (start={}, end={})",
                     link.ID.Get(), startPinIndex, endPinIndex);
            continue;
        }

        fillEndpoint("start", startPin, startPinIndex);
        fillEndpoint("end", endPin, endPinIndex);

        // Save if this is a parameter connection for styling
        bool isParameterLink = (startPin->Node && startPin->Node->Type == NodeType::Parameter &&
                               endPin->Node && endPin->Node->IsBlockBased());
        if (isParameterLink)
            linkData["is_parameter_link"] = true;

        // Save user-manipulated flag (preserve user's link path)
        if (link.UserManipulatedWaypoints)
            linkData["user_manipulated"] = true;

        // Save delay setting
        if (link.Delay != 0.0f)
            linkData["delay"] = link.Delay;

        // Save link mode (Auto/Bezier, Straight, or Guided)
        auto linkMode = ed::GetLinkMode(link.ID);
        if (linkMode == ed::LinkMode::Auto)
        {
            linkData["link_mode"] = "bezier";
        }
        else if (linkMode == ed::LinkMode::Straight)
        {
            linkData["link_mode"] = "straight";
        }
        else if (linkMode == ed::LinkMode::Guided)
        {
            linkData["link_mode"] = "guided";

            // Save guided link control points if any
            int cpCount = ed::GetLinkControlPointCount(link.ID);
            if (cpCount > 0)
            {
                auto& controlPoints = linkData["control_points"];

                std::vector<ImVec2> points(cpCount);
                ed::GetLinkControlPoints(link.ID, points.data(), cpCount);

                for (const auto& pos : points)
                {
                    crude_json::value pt;
                    pt["x"] = pos.x;
                    pt["y"] = pos.y;
                    controlPoints.push_back(pt);
                }
            }
        }

        linksArray.push_back(linkData);
    }
    
    // Save to file with pretty formatting (4 space indent)
    // Supports both relative and absolute paths
    std::ofstream file(filename);
    if (file)
    {
        file << root.dump(4);
        LOG_INFO("Graph saved to: {}", filename);
    }
    else
    {
        LOG_ERROR("Error: Failed to save graph to: {}", filename);
    }
}

size_t App::LoadViewSettings(char* data)
{
    // First call: query size
    if (!data)
    {
        std::ifstream file("Blueprints.json");
        if (!file)
            return 0;  // No view settings saved
        
        file.seekg(0, std::ios::end);
        size_t size = static_cast<size_t>(file.tellg());
        
        // If we have saved view settings, skip initial zoom to content
        if (size > 0)
        {
            m_NeedsInitialZoom = false;
            LOG_INFO("[LoadViewSettings] Found saved view state, skipping initial zoom to content");
        }
        
        return size;
    }
    
    // Second call: read data
    std::ifstream file("Blueprints.json");
    if (!file)
        return 0;
    
    std::string jsonData((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    memcpy(data, jsonData.c_str(), jsonData.size());
    
    return jsonData.size();
}

bool App::SaveViewSettings(const char* data, size_t size)
{
    // Node editor gives us full settings JSON with nodes/links we don't want
    // We only want to save view state (scroll, zoom, visible_rect, selection)
    
    std::string jsonStr(data, size);
    auto settings = crude_json::value::parse(jsonStr);
    
    if (settings.is_discarded() || !settings.is_object())
        return false;
    
    // Create new JSON with only view state
    crude_json::value viewOnly;
    
    if (settings.contains("view"))
        viewOnly["view"] = settings["view"];
    
    if (settings.contains("selection"))
        viewOnly["selection"] = settings["selection"];
    
    // Save only view state to Blueprints.json
    std::ofstream file("Blueprints.json");
    if (!file)
        return false;
    
    file << viewOnly.dump(4);
    return true;
}

void App::LoadGraph(const std::string& filename, RootContainer* container)
{
    if (!container)
    {
        LOG_TRACE("[CHECKPOINT] LoadGraph: No container provided, skipping load");
        return;
    }
    
    LOG_TRACE("[CHECKPOINT] LoadGraph: Beginning, filename={}", filename);
    
    // Load app-level graph data
    // Supports both relative and absolute paths
    std::ifstream file(filename);
    if (!file)
    {
        LOG_WARN("No existing graph file found at: {} (starting with empty graph)", filename);
        LOG_TRACE("[CHECKPOINT] LoadGraph: File not found, returning");
        return;
    }
    LOG_INFO("Loading graph from: {}", filename);
    
    LOG_TRACE("[CHECKPOINT] LoadGraph: Reading file data");
    
    std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    
    LOG_TRACE("[CHECKPOINT] LoadGraph: Parsing JSON, size={}", data.size());
    
    auto root = crude_json::value::parse(data);
    
    if (root.is_discarded() || !root.is_object())
    {
        LOG_TRACE("[CHECKPOINT] LoadGraph: JSON parse failed or not object, returning");
        return;
    }
    
    LOG_TRACE("[CHECKPOINT] LoadGraph: JSON parsed successfully");
    
    // Clear UUID mappings before loading new graph
    m_UuidIdManager.Clear();
    
    // Load nodes with positions
    if (root.contains("app_nodes") && root["app_nodes"].is_array())
    {
        const auto& nodesArray = root["app_nodes"].get<crude_json::array>();
        
        LOG_TRACE("[CHECKPOINT] LoadGraph: Found {} nodes to load", nodesArray.size());
        
        // CRITICAL: Track highest pin ID from saved data to avoid ID reuse
        // Old format saves pin_id values in inputs/outputs arrays
        // We must skip past these IDs when generating new ones
        int highestPinId = container->GetCurrentId();
        
        for (const auto& nodeData : nodesArray)
        {
            if (!nodeData.is_object())
                continue;
            
            // Scan input pin IDs
            if (nodeData.contains("inputs") && nodeData["inputs"].is_array())
            {
                for (const auto& pinData : nodeData["inputs"].get<crude_json::array>())
                {
                    if (pinData.is_object() && pinData.contains("pin_id"))
                    {
                        int pinId = (int)pinData["pin_id"].get<double>();
                        if (pinId >= highestPinId)
                            highestPinId = pinId + 1;
                    }
                }
            }
            
            // Scan output pin IDs
            if (nodeData.contains("outputs") && nodeData["outputs"].is_array())
            {
                for (const auto& pinData : nodeData["outputs"].get<crude_json::array>())
                {
                    if (pinData.is_object() && pinData.contains("pin_id"))
                    {
                        int pinId = (int)pinData["pin_id"].get<double>();
                        if (pinId >= highestPinId)
                            highestPinId = pinId + 1;
                    }
                }
            }
            
            // Scan group pin definitions (groups save pin_id separately)
            if (nodeData.contains("group_pin_definitions") && nodeData["group_pin_definitions"].is_array())
            {
                for (const auto& pinDef : nodeData["group_pin_definitions"].get<crude_json::array>())
                {
                    if (pinDef.is_object() && pinDef.contains("pin_id"))
                    {
                        int pinId = (int)pinDef["pin_id"].get<double>();
                        if (pinId >= highestPinId)
                            highestPinId = pinId + 1;
                    }
                }
            }
        }
        
        // Update container's ID counter to avoid reusing pin IDs
        while (container->GetCurrentId() < highestPinId)
        {
            container->GetNextId();
        }
        
        LOG_TRACE("[CHECKPOINT] LoadGraph: Container ID counter updated to {} (highest pin ID was {})",
                  container->GetCurrentId(), highestPinId - 1);
        
        // Load and spawn nodes
        for (const auto& nodeData : nodesArray)
        {
            if (!nodeData.is_object())
                continue;
            
            // Load UUID from file (persistent identifier)
            Uuid64 loadedUuid(0, 0);
            if (nodeData.contains("uuid_high") && nodeData.contains("uuid_low"))
            {
                loadedUuid.high = (uint32_t)nodeData["uuid_high"].get<double>();
                loadedUuid.low = (uint32_t)nodeData["uuid_low"].get<double>();
                LOG_TRACE("[CHECKPOINT] LoadGraph: Loading node with UUID: 0x{:08X}{:08X}", loadedUuid.high, loadedUuid.low);
            }
            else if (nodeData.contains("id"))
            {
                // Old format - generate UUID from runtime ID for migration
                int oldId = (int)nodeData["id"].get<double>();
                loadedUuid = Uuid64(0, (uint32_t)oldId);
                LOG_TRACE("[CHECKPOINT] LoadGraph: Old format node ID {} - generating UUID: 0x{:08X}{:08X}",
                          oldId, loadedUuid.high, loadedUuid.low);
            }
            
            std::string nodeType = nodeData.contains("node_type") ? nodeData["node_type"].get<crude_json::string>() : "hardcoded";
            
            Node* spawnedNode = nullptr;
            
            // Spawn based on node type
            if (nodeType == "parameter")
            {
                // Parameter node
                LOG_TRACE("[CHECKPOINT] LoadGraph: Processing parameter node with UUID: 0x{:08X}{:08X}",
                          loadedUuid.high, loadedUuid.low);
                
                if (!nodeData.contains("param_type") && !nodeData.contains("parameter_type"))
                {
                    LOG_TRACE("[CHECKPOINT] LoadGraph: ERROR - parameter node missing type field!");
                    continue;
                }
                
                PinType paramType;
                if (nodeData.contains("param_type"))
                {
                    paramType = static_cast<PinType>((int)nodeData["param_type"].get<double>());
                }
                else if (nodeData.contains("parameter_type"))
                {
                    paramType = static_cast<PinType>((int)nodeData["parameter_type"].get<double>());
                }
                
                LOG_TRACE("[CHECKPOINT] LoadGraph: Spawning parameter node, type={}", static_cast<int>(paramType));
                
                // Load display mode
                ParameterDisplayMode displayMode = ParameterDisplayMode::NameAndValue;
                if (nodeData.contains("display_mode"))
                    displayMode = static_cast<ParameterDisplayMode>((int)nodeData["display_mode"].get<double>());
                
                // Spawn with auto-generated runtime ID (pass -1)
                spawnedNode = SpawnParameterNode(paramType, -1, displayMode);
                
                // Replace auto-generated UUID with loaded UUID
                if (spawnedNode && loadedUuid.IsValid())
                {
                    // Unregister the auto-generated UUID
                    m_UuidIdManager.UnregisterNode(spawnedNode->UUID);
                    for (auto& pin : spawnedNode->Inputs)
                        m_UuidIdManager.UnregisterPin(pin.UUID);
                    for (auto& pin : spawnedNode->Outputs)
                        m_UuidIdManager.UnregisterPin(pin.UUID);
                    
                    // Assign loaded UUID
                    spawnedNode->UUID = loadedUuid;
                    
                    // Re-register with loaded UUID
                    m_UuidIdManager.RegisterNode(loadedUuid, spawnedNode->ID.Get());
                    LOG_TRACE("[CHECKPOINT] LoadGraph: Assigned loaded UUID to node (runtime ID={})",
                              spawnedNode->ID.Get());
                }
                
                if (spawnedNode && nodeData.contains("value"))
                {
                    switch (paramType)
                    {
                        case PinType::Bool:   spawnedNode->BoolValue = nodeData["value"].get<bool>(); break;
                        case PinType::Int:    spawnedNode->IntValue = (int)nodeData["value"].get<double>(); break;
                        case PinType::Float:  spawnedNode->FloatValue = (float)nodeData["value"].get<double>(); break;
                        case PinType::String: spawnedNode->StringValue = nodeData["value"].get<crude_json::string>(); break;
                        default: break;
                    }
                    
                    // Sync to parameter instance
                    if (spawnedNode->ParameterInstance)
                    {
                        switch (paramType)
                        {
                            case PinType::Bool:   spawnedNode->ParameterInstance->SetBool(spawnedNode->BoolValue); break;
                            case PinType::Int:    spawnedNode->ParameterInstance->SetInt(spawnedNode->IntValue); break;
                            case PinType::Float:  spawnedNode->ParameterInstance->SetFloat(spawnedNode->FloatValue); break;
                            case PinType::String: spawnedNode->ParameterInstance->SetString(spawnedNode->StringValue); break;
                            default: break;
                        }
                    }
                }
                
                // Restore custom name if set
                if (spawnedNode && nodeData.contains("name"))
                {
                    spawnedNode->Name = nodeData["name"].get<crude_json::string>();
                    if (spawnedNode->ParameterInstance)
                        spawnedNode->ParameterInstance->SetName(nodeData["name"].get<crude_json::string>().c_str());
                }
                
                // Call parameter node's LoadState callback (passes container info)
                if (spawnedNode && spawnedNode->ParameterInstance)
                {
                    spawnedNode->ParameterInstance->LoadState(*spawnedNode, nodeData, container, this);
                }
            }
            else if (nodeType == "block" && nodeData.contains("block_type"))
            {
                // Block-based node
                std::string blockType = nodeData["block_type"].get<crude_json::string>();
                LOG_TRACE("[CHECKPOINT] LoadGraph: Spawning block node, type={}, UUID=0x{:08X}{:08X}",
                          blockType, loadedUuid.high, loadedUuid.low);
                
                // Spawn with auto-generated runtime ID (pass -1)
                spawnedNode = SpawnBlockNode(blockType.c_str(), -1);
                
                LOG_TRACE("[CHECKPOINT] LoadGraph: Block node spawned at {:p}", static_cast<const void*>(spawnedNode));
                
                // Replace auto-generated UUID with loaded UUID
                if (spawnedNode && loadedUuid.IsValid())
                {
                    // Unregister the auto-generated UUID
                    m_UuidIdManager.UnregisterNode(spawnedNode->UUID);
                    for (auto& pin : spawnedNode->Inputs)
                        m_UuidIdManager.UnregisterPin(pin.UUID);
                    for (auto& pin : spawnedNode->Outputs)
                        m_UuidIdManager.UnregisterPin(pin.UUID);
                    
                    // Assign loaded UUID
                    spawnedNode->UUID = loadedUuid;
                    
                    // Re-register with loaded UUID
                    m_UuidIdManager.RegisterNode(loadedUuid, spawnedNode->ID.Get());
                    LOG_TRACE("[CHECKPOINT] LoadGraph: Assigned loaded UUID to node (runtime ID={})",
                              spawnedNode->ID.Get());
                }
                
                // Restore block display mode
                if (spawnedNode && nodeData.contains("block_display_mode"))
                {
                    spawnedNode->BlockDisplay = static_cast<::BlockDisplayMode>((int)nodeData["block_display_mode"].get<double>());
                }
                
                // Call block's LoadState callback (passes container info)
                if (spawnedNode && spawnedNode->BlockInstance)
                {
                    spawnedNode->BlockInstance->LoadState(*spawnedNode, nodeData, container, this);
                }
            }
            // Hardcoded nodes would be created here if we had them
            
            // Restore node position (use spawned node's runtime ID, not loaded ID)
            if (spawnedNode && nodeData.contains("position") && nodeData["position"].is_object())
            {
                float x = (float)nodeData["position"]["x"].get<double>();
                float y = (float)nodeData["position"]["y"].get<double>();
                ed::SetNodePosition(spawnedNode->ID, ImVec2(x, y));
            }
            
            // Note: Node size is now calculated automatically by the node editor based on content
            // (Groups are rendered like regular blocks, so explicit size setting is not needed)
        }
        
        LOG_TRACE("[CHECKPOINT] LoadGraph: All nodes loaded, container has {} nodes", container->m_Nodes.size());
    }
    
    // Load links with control points - clear links from the container
    // Clear all links from the container
    auto allLinkIds = container->m_LinkIds;  // Copy IDs before clearing
    for (auto linkId : allLinkIds)
    {
        container->RemoveLink(linkId);
    }
    container->m_LinkIds.clear();
    
    LOG_TRACE("[CHECKPOINT] LoadGraph: Clearing container link IDs (had {} links)",
              container->m_LinkIds.size());
    
    if (root.contains("app_links") && root["app_links"].is_array())
    {
        const auto& linksArray = root["app_links"].get<crude_json::array>();
        
        LOG_TRACE("[CHECKPOINT] LoadGraph: Loading {} links", linksArray.size());
        
        for (const auto& linkData : linksArray)
        {
            if (!linkData.is_object())
                continue;
            
            // Load link UUID
            Uuid64 linkUuid(0, 0);
            if (linkData.contains("uuid") && linkData["uuid"].is_object())
            {
                const auto& uuidObj = linkData["uuid"];
                if (uuidObj.contains("high") && uuidObj.contains("low"))
                {
                    linkUuid.high = (uint32_t)uuidObj["high"].get<double>();
                    linkUuid.low = (uint32_t)uuidObj["low"].get<double>();
                    LOG_DEBUG("[LoadGraph] Loading link UUID: 0x{:08X}{:08X}", linkUuid.high, linkUuid.low);
                }
            }
            
            // Generate NEW runtime link ID (don't use ID from file)
            int linkRuntimeId = container->GetNextId();
            
            // Helper lambda to resolve pin by relative index (using node UUID)
            auto resolvePinByRelativeIndex = [this](const Uuid64& nodeUuid, int pinIndex, bool isFlow, bool isInput) -> Pin* {
                // Resolve node UUID to runtime ID
                int nodeRuntimeId = m_UuidIdManager.GetNodeRuntimeId(nodeUuid);
                if (nodeRuntimeId < 0)
                    return nullptr;
                
                Node* node = FindNode(ed::NodeId(nodeRuntimeId));
                if (!node)
                    return nullptr;
                
                // Get the appropriate pin list (non-const access since we return Pin*)
                auto& pinList = isInput ? node->Inputs : node->Outputs;
                
                // Find the Nth pin of the matching category
                int index = 0;
                for (auto& pin : pinList)
                {
                    bool pIsFlow = (pin.Type == PinType::Flow);
                    
                    // Same category?
                    if (pIsFlow == isFlow)
                    {
                        if (index == pinIndex)
                            return &pin;
                        index++;
                    }
                }
                
                return nullptr;  // Index out of range
            };
            
            Pin* startPin = nullptr;
            Pin* endPin = nullptr;
            int startPinId = -1;
            int endPinId = -1;

            auto parseEndpoint = [&](const char* key, Pin*& outPin, int& outPinId) -> bool
            {
                if (!linkData.contains(key) || !linkData[key].is_object())
                    return false;

                const auto& endpoint = linkData[key];
                if (!endpoint.contains("node_id_h") || !endpoint.contains("node_id_l") ||
                    !endpoint.contains("pin_index") || !endpoint.contains("pin_type"))
                    return false;

                Uuid64 nodeUuid(
                    (uint32_t)endpoint["node_id_h"].get<double>(),
                    (uint32_t)endpoint["node_id_l"].get<double>()
                );

                int pinIndex = (int)endpoint["pin_index"].get<double>();
                std::string pinTypeStr = endpoint["pin_type"].get<crude_json::string>();

                SerializedPinType parsedType;
                if (!TryParseSerializedPinType(pinTypeStr, parsedType))
                {
                    LOG_WARN("[LoadGraph] Unknown pin_type '{}' for link endpoint '{}'", pinTypeStr, key);
                    return false;
                }

                bool isFlow = SerializedPinTypeIsFlow(parsedType);
                bool isInput = SerializedPinTypeIsInput(parsedType);

                outPin = resolvePinByRelativeIndex(nodeUuid, pinIndex, isFlow, isInput);
                if (!outPin)
                    return false;

                outPinId = outPin->ID.Get();
                return true;
            };

            bool startParsed = parseEndpoint("start", startPin, startPinId);
            bool endParsed = parseEndpoint("end", endPin, endPinId);
            
            // Orphan checks - validate pins and nodes exist
            bool linkRemoved = false;
            
            if (!startParsed)
            {
                LOG_WARN("[LoadGraph] REMOVING link (runtime {}) - start endpoint could not be resolved",
                         linkRuntimeId);
                linkRemoved = true;
            }
            else if (!endParsed)
            {
                LOG_WARN("[LoadGraph] REMOVING link (runtime {}) - end endpoint could not be resolved",
                         linkRuntimeId);
                linkRemoved = true;
            }
            else if (!startPin->Node)
            {
                LOG_WARN("[LoadGraph] REMOVING link (runtime {}) - start pin {} has null node", linkRuntimeId, startPinId);
                linkRemoved = true;
            }
            else if (!endPin->Node)
            {
                LOG_WARN("[LoadGraph] REMOVING link (runtime {}) - end pin {} has null node", linkRuntimeId, endPinId);
                linkRemoved = true;
            }
            else
            {
                // Validate: Check that nodes are registered with editor (have valid positions)
                const float FLT_MAX_THRESHOLD = 3.40282e+38f;
                ImVec2 startNodePos = ed::GetNodePosition(startPin->Node->ID);
                ImVec2 endNodePos = ed::GetNodePosition(endPin->Node->ID);
                
                if (startNodePos.x >= FLT_MAX_THRESHOLD)
                {
                    LOG_WARN("[LoadGraph] REMOVING link (runtime {}) - start node {} not registered with editor",
                             linkRuntimeId, startPin->Node->ID.Get());
                    linkRemoved = true;
                }
                else if (endNodePos.x >= FLT_MAX_THRESHOLD)
                {
                    LOG_WARN("[LoadGraph] REMOVING link (runtime {}) - end node {} not registered with editor",
                             linkRuntimeId, endPin->Node->ID.Get());
                    linkRemoved = true;
                }
            }
            
            // Remove link entirely - don't add it to container if validation failed
            if (linkRemoved)
            {
                continue;  // Don't create link
            }
            
            // Create link with NEW runtime ID and resolved pin runtime IDs
            auto linkToAdd = ::Link(ed::LinkId(linkRuntimeId), ed::PinId(startPinId), ed::PinId(endPinId));
            linkToAdd.UUID = linkUuid;  // Assign loaded UUID
            Link* linkPtr = container->AddLink(linkToAdd);
            
            if (!linkPtr)
            {
                LOG_TRACE("[CHECKPOINT] LoadGraph: WARNING - link with runtime ID {} already exists, skipping",
                          linkRuntimeId);
                continue;
            }
            
            // Register link UUID mapping
            if (linkUuid.IsValid())
            {
                m_UuidIdManager.RegisterLink(linkUuid, linkRuntimeId);
                LOG_DEBUG("[LoadGraph] Registered link UUID 0x{:08X}{:08X} -> runtime ID {}",
                          linkUuid.high, linkUuid.low, linkRuntimeId);
            }
            
            // Restore parameter link flag
            if (linkData.contains("is_parameter_link") && linkData["is_parameter_link"].get<bool>())
            {
                linkPtr->IsParameterLink = true;
            }
            
            // Restore user-manipulated flag (preserve user's link path)
            if (linkData.contains("user_manipulated") && linkData["user_manipulated"].get<bool>())
            {
                linkPtr->UserManipulatedWaypoints = true;
            }
            
            // Restore delay setting
            if (linkData.contains("delay") && linkData["delay"].is_number())
            {
                linkPtr->Delay = static_cast<float>(linkData["delay"].get<double>());
            }
            
            // Note: auto_adjust field is ignored (redundant with link_mode=guided)
            
            // Restore link mode (Auto/Bezier, Straight, or Guided)
            // Store in pending map to apply after link is registered with editor (use NEW runtime ID)
            if (linkData.contains("link_mode") && linkData["link_mode"].is_string())
            {
                std::string modeStr = linkData["link_mode"].get<crude_json::string>();
                if (modeStr == "bezier")
                {
                    m_PendingLinkModes[ed::LinkId(linkRuntimeId)] = ax::NodeEditor::LinkMode::Auto;
                }
                else if (modeStr == "straight")
                {
                    m_PendingLinkModes[ed::LinkId(linkRuntimeId)] = ax::NodeEditor::LinkMode::Straight;
                }
                else if (modeStr == "guided")
                {
                    m_PendingLinkModes[ed::LinkId(linkRuntimeId)] = ax::NodeEditor::LinkMode::Guided;
                    
                    // Store control points for guided links
                    if (linkData.contains("control_points") && linkData["control_points"].is_array())
                    {
                        const auto& controlPoints = linkData["control_points"].get<crude_json::array>();
                        for (const auto& pt : controlPoints)
                        {
                            if (pt.is_object() && pt.contains("x") && pt.contains("y"))
                            {
                                float x = (float)pt["x"].get<double>();
                                float y = (float)pt["y"].get<double>();
                                // Store in a pending list to apply after links are drawn (use NEW runtime ID)
                                m_PendingGuidedLinks[ed::LinkId(linkRuntimeId)].push_back(ImVec2(x, y));
                            }
                        }
                    }
                }
                // If link_mode is missing, default to Auto (bezier) mode
            }
        }
        
        LOG_TRACE("[CHECKPOINT] LoadGraph: All links loaded");
    }
}

// Container management (delegates to GraphState)
RootContainer* App::GetActiveRootContainer()
{
    return m_GraphState.GetActiveRootContainer();
}

RootContainer* App::AddRootContainer(const std::string& filename)
{
    auto* container = m_GraphState.AddRootContainer(filename);
    return container;
}

void App::RemoveRootContainer(RootContainer* container)
{
    m_GraphState.RemoveRootContainer(container);
}

void App::SetActiveRootContainer(RootContainer* container)
{
    m_GraphState.SetActiveRootContainer(container);
}

Container* App::FindContainerForNode(ed::NodeId nodeId)
{
    return m_GraphState.FindContainerForNode(nodeId);
}

void App::MarkLinkUserManipulated(ed::LinkId linkId)
{
    // Mark link as user-manipulated to preserve waypoints and disable auto-adjustment
    auto* container = GetActiveRootContainer();
    if (!container)
        return;
    
    auto* link = container->FindLink(linkId);
    if (link)
    {
        link->UserManipulatedWaypoints = true;
    }
}
