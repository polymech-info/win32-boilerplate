//------------------------------------------------------------------------------
// Runtime execution system for blocks
// Processes activated blocks and propagates execution through links
// NOTE: Pure execution logic - no rendering/visualization dependencies
//------------------------------------------------------------------------------
#include "app.h"
#include "types.h"
#include "blocks/block.h"
#include "Logging.h"
#include <vector>
#include <set>

bool App::ExecuteRuntimeStep()
{
    static bool firstCall = true;
    static int totalCalls = 0;
    static int framesWithWork = 0;
    totalCalls++;

    if (firstCall)
    {
        LOG_TRACE("[CHECKPOINT] ExecuteRuntimeStep: Called (from ed::End)");
        firstCall = false;
    }

    // Debug: Log every 60 frames to show we're being called
    if (totalCalls % 60 == 1) // Log on frame 1, 61, 121, etc.
    {
        LOG_TRACE("[Runtime] ExecuteRuntimeStep called {} times total, {} frames had work", totalCalls, framesWithWork);
    }

    // Execute in a loop until no more blocks need to run
    // This propagates execution through the entire chain in one frame
    const int maxIterations = 50; // Safety limit to prevent infinite loops
    int iteration = 0;

    bool didAnyWork = false; // Track if we found any work to do overall

    while (iteration < maxIterations)
    {
        iteration++;

        // Step 0: First, propagate any activated outputs to connected inputs
        // This handles the case where outputs were activated manually (e.g., via 'R' key)
        // Track which outputs we propagate so we can deactivate them after
        std::vector<std::pair<Node *, int>> outputsToDeactivate; // (node, outputIndex)

        LOG_DEBUG("[Runtime DEBUG] Iteration {}: Starting Step 0 (check for activated outputs)", iteration);

        // Get nodes from active root container if available, otherwise use m_Nodes
        auto *container = GetActiveRootContainer();
        std::vector<Node *> nodesToProcess;

        if (container)
        {
            // Use GetNodes() to resolve IDs to pointers (safe from reallocation)
            nodesToProcess = container->GetNodes(this);
        }
        else
        {
            // No container - get nodes from active root container
            if (GetActiveRootContainer())
            {
                nodesToProcess = GetActiveRootContainer()->GetAllNodes();
            }
        }

        for (auto *nodePtr : nodesToProcess)
        {
            if (!nodePtr)
                continue;

            // CRITICAL: Skip parameter nodes - they don't have BlockInstance
            // Check node type FIRST before any BlockInstance access
            // Use try-catch to safely access Type field on potentially freed memory
            NodeType nodeType;
            try
            {
                nodeType = nodePtr->Type;
            }
            catch (...)
            {
                LOG_ERROR("[ERROR] ExecuteRuntimeStep: Failed to access node Type field");
                continue;
            }

            if (nodeType == NodeType::Parameter || !nodePtr->IsBlockBased())
                continue;

            // Use safe getter to validate BlockInstance
            Block *blockInstance = nodePtr->GetBlockInstance();
            if (!blockInstance)
                continue;

            // Validate BlockInstance ID matches node ID (prevents dangling pointer from ID reuse)
            // Only call GetID() if pointer is valid (not corrupted)
            int blockId = -1;
            try
            {
                blockId = blockInstance->GetID();
            }
            catch (...)
            {
                LOG_ERROR("[ERROR] ExecuteRuntimeStep: Failed to call GetID() on BlockInstance for node {}",
                          nodePtr->ID.Get());
                nodePtr->BlockInstance = nullptr;
                continue;
            }

            if (blockId != nodePtr->ID.Get())
            {
                // ID mismatch = dangling pointer, clear it
                LOG_ERROR("[ERROR] ExecuteRuntimeStep: Node {} has BlockInstance with mismatched ID! Node ID={}, Block ID={}",
                          nodePtr->ID.Get(), nodePtr->ID.Get(), blockId);
                nodePtr->BlockInstance = nullptr;
                continue;
            }

            auto &node = *nodePtr;

            // Check all flow outputs for activation
            int outputIndex = 0;
            for (const auto &pin : node.Outputs)
            {
                if (pin.Type == PinType::Flow)
                {
                    if (blockInstance->IsOutputActive(outputIndex))
                    {
                        didAnyWork = true;
                        framesWithWork++;

                        // Track this output for deactivation
                        outputsToDeactivate.push_back({&node, outputIndex});

                        // Find all links connected to this activated output pin
                        // Get links from active root container if available, otherwise use m_Links
                        std::vector<Link *> fallbackLinks;
                        std::vector<Link *> linksToProcess;

                        if (container)
                        {
                            // Get links from container (uses GetLinks which resolves IDs to pointers)
                            linksToProcess = container->GetLinks(this);
                        }
                        else
                        {
                            // No container - get all links from active root container
                            if (GetActiveRootContainer())
                            {
                                linksToProcess = GetActiveRootContainer()->GetAllLinks();
                            }
                        }

                        for (auto *linkPtr : linksToProcess)
                        {
                            if (!linkPtr)
                                continue;
                            auto &link = *linkPtr;

                            if (link.StartPinID == pin.ID)
                            {
                                // Find target node and determine input index
                                auto *targetPin = FindPin(link.EndPinID);
                                if (targetPin && targetPin->Node &&
                                    targetPin->Node->Type != NodeType::Parameter &&
                                    targetPin->Node->IsBlockBased() && targetPin->Node->BlockInstance)
                                {
                                    // Find which input index this pin corresponds to
                                    int targetInputIndex = 0;
                                    for (const auto &targetInputPin : targetPin->Node->Inputs)
                                    {
                                        if (targetInputPin.Type == PinType::Flow)
                                        {
                                            if (targetInputPin.ID == link.EndPinID)
                                            {
                                                targetPin->Node->BlockInstance->ActivateInput(targetInputIndex, true);
                                                break;
                                            }
                                            targetInputIndex++;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    outputIndex++;
                }
            }
        }

        // Step 1: Find all blocks with activated inputs
        std::vector<Node *> blocksToRun;

        for (auto *nodePtr : nodesToProcess)
        {
            // CRITICAL: Validate node pointer is not corrupted/freed before accessing ANY members
            if (!nodePtr)
                continue;

            // CRITICAL: Skip parameter nodes - they don't have BlockInstance
            // Check node type FIRST before any BlockInstance access
            // Use try-catch to safely access Type field on potentially freed memory
            NodeType nodeType;
            try
            {
                nodeType = nodePtr->Type;
            }
            catch (...)
            {
                LOG_ERROR("[ERROR] ExecuteRuntimeStep: Failed to access node Type field");
                continue;
            }

            if (nodeType == NodeType::Parameter || !nodePtr->IsBlockBased())
                continue;

            // Use safe getter to validate BlockInstance
            Block *blockInstance = nodePtr->GetBlockInstance();
            if (!blockInstance)
                continue;

            // Validate BlockInstance ID matches node ID (prevents dangling pointer from ID reuse)
            // Only call GetID() if pointer is valid (not corrupted)
            int blockId = -1;
            try
            {
                blockId = blockInstance->GetID();
            }
            catch (...)
            {
                LOG_ERROR("[ERROR] ExecuteRuntimeStep: Failed to call GetID() on BlockInstance for node {}",
                          nodePtr->ID.Get());
                nodePtr->BlockInstance = nullptr;
                continue;
            }

            if (blockId != nodePtr->ID.Get())
            {
                // ID mismatch = dangling pointer, clear it
                LOG_ERROR("[ERROR] ExecuteRuntimeStep: Node {} has BlockInstance with mismatched ID! Node ID={}, Block ID={}",
                          nodePtr->ID.Get(), nodePtr->ID.Get(), blockId);
                nodePtr->BlockInstance = nullptr;
                continue;
            }

            auto &node = *nodePtr;

            // Check if any flow input is activated
            bool hasActivatedInput = false;
            int inputIndex = 0;
            for (const auto &pin : node.Inputs)
            {
                if (pin.Type == PinType::Flow)
                {
                    if (blockInstance->IsInputActive(inputIndex))
                    {
                        hasActivatedInput = true;
                        break;
                    }
                    inputIndex++;
                }
            }

            if (hasActivatedInput)
            {
                LOG_TRACE("[Runtime] Step 1: Adding node {} to execution queue", node.ID.Get());
                blocksToRun.push_back(&node);
            }
        }

        // If no blocks to run, we're done
        if (blocksToRun.empty())
        {
            if (didAnyWork)
            {
                LOG_TRACE("[Runtime] Iteration {}: No blocks to run (but had work in Step 0)", iteration);
            }
            break;
        }

        didAnyWork = true;
        LOG_TRACE("[Runtime] Step 2: Executing {} block(s)", blocksToRun.size());

        // Step 2: Execute blocks and collect activations to propagate
        std::vector<std::pair<Node *, int>> activationsToPropagate; // (target node, input index)

        for (Node *node : blocksToRun)
        {
            // CRITICAL: Skip parameter nodes
            if (!node || node->Type == NodeType::Parameter || !node->IsBlockBased() || !node->BlockInstance)
                continue;

            // Run the block (pure execution - no logging/visualization here)
            node->BlockInstance->Run(*node, this);

            // Activate first flow output (index 0 = "Done") when block opts-in
            // Blocks can override ShouldAutoActivateDefaultOutput() to handle flow routing manually
            if (node->BlockInstance->ShouldAutoActivateDefaultOutput())
            {
                node->BlockInstance->ActivateOutput(0, true);
            }

            // Step 3: Find links from activated outputs and prepare to activate targets
            int outputIndex = 0;
            for (const auto &pin : node->Outputs)
            {
                if (pin.Type == PinType::Flow)
                {
                    if (node->BlockInstance->IsOutputActive(outputIndex))
                    {
                        // Find all links connected to this output pin
                        // Get links from active root container if available
                        std::vector<Link *> linksToProcess2;

                        if (container)
                        {
                            linksToProcess2 = container->GetLinks(this);
                        }
                        else if (GetActiveRootContainer())
                        {
                            linksToProcess2 = GetActiveRootContainer()->GetAllLinks();
                        }

                        for (auto *linkPtr : linksToProcess2)
                        {
                            if (!linkPtr)
                                continue;
                            auto &link = *linkPtr;

                            if (link.StartPinID == pin.ID)
                            {
                                // Find target node and determine input index
                                auto *targetPin = FindPin(link.EndPinID);
                                if (targetPin && targetPin->Node && targetPin->Node->IsBlockBased())
                                {
                                    // Find which input index this pin corresponds to
                                    int targetInputIndex = 0;
                                    for (const auto &targetInputPin : targetPin->Node->Inputs)
                                    {
                                        if (targetInputPin.Type == PinType::Flow)
                                        {
                                            if (targetInputPin.ID == link.EndPinID)
                                            {
                                                // Queue activation for next iteration
                                                activationsToPropagate.push_back({targetPin->Node, targetInputIndex});
                                                break;
                                            }
                                            targetInputIndex++;
                                        }
                                    }
                                }
                            }
                        }
                        outputIndex++;
                    }
                }
            }

            // Step 4: Visualize execution (only if rendering - headless mode skips this)
            // Collect affected links from executed blocks for visualization
            std::vector<ed::LinkId> affectedLinks;
            for (Node *node : blocksToRun)
            {
                // CRITICAL: Skip parameter nodes
                if (!node || node->Type == NodeType::Parameter || !node->IsBlockBased())
                    continue;

                // Collect all output links (flow + parameter)
                // Get links from active root container if available
                std::vector<Link *> linksToProcess3;

                if (container)
                {
                    linksToProcess3 = container->GetLinks(this);
                }
                else if (GetActiveRootContainer())
                {
                    linksToProcess3 = GetActiveRootContainer()->GetAllLinks();
                }

                for (const auto &pin : node->Outputs)
                {
                    for (auto *linkPtr : linksToProcess3)
                    {
                        if (!linkPtr)
                            continue;
                        auto &link = *linkPtr;

                        if (link.StartPinID == pin.ID)
                        {
                            affectedLinks.push_back(link.ID);
                        }
                    }
                }
            }

            // Visualize (only in rendering mode - will be no-op in headless)
            VisualizeRuntimeExecution(blocksToRun, affectedLinks);

            // Step 5: Deactivate all outputs and inputs after execution
            // First, deactivate outputs that were propagated in Step 0 (to prevent re-propagation)
            for (const auto &outputInfo : outputsToDeactivate)
            {
                Node *node = outputInfo.first;
                int outputIndex = outputInfo.second;
                if (node && node->IsBlockBased() && node->BlockInstance)
                {
                    node->BlockInstance->ActivateOutput(outputIndex, false);
                }
            }

            // Then deactivate all outputs and inputs of executed blocks
            for (Node *node : blocksToRun)
            {
                // CRITICAL: Skip parameter nodes
                if (!node || node->Type == NodeType::Parameter || !node->IsBlockBased() || !node->BlockInstance)
                    continue;

                // Deactivate all outputs
                int outputCount = node->BlockInstance->GetOutputCount();
                for (int i = 0; i < outputCount; ++i)
                {
                    node->BlockInstance->ActivateOutput(i, false);
                }

                // Also deactivate inputs (blocks should deactivate their inputs after running)
                int inputCount = node->BlockInstance->GetInputCount();
                for (int i = 0; i < inputCount; ++i)
                {
                    node->BlockInstance->ActivateInput(i, false);
                }
            }

            // Step 6: Activate inputs for next iteration (same frame)
            for (const auto &activation : activationsToPropagate)
            {
                Node *targetNode = activation.first;
                int inputIndex = activation.second;
                if (targetNode && targetNode->IsBlockBased() && targetNode->BlockInstance)
                {
                    targetNode->BlockInstance->ActivateInput(inputIndex, true);
                }
            }

            // Continue loop to execute newly activated blocks
        }

        return didAnyWork;
    }
    return false;
}
