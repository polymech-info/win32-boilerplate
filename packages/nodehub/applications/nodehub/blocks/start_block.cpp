#include "start_block.h"
#include "../app.h"

void StartBlock::Build(Node& node, App* app)
{
    node.Type = m_Type;
    node.Color = m_Color;
    
    // No flow input - this is an entry point block
    // Only has flow output "Start"
    AddOutput(app, node, "Start");
}

int StartBlock::Run(Node& node, App* app)
{
    // Entry point block - just activate the output to trigger connected blocks
    // In GUI mode: Activate this block by selecting it and pressing 'R'
    // In headless mode: Would need to find all Start blocks and activate them automatically
    
    // The output will be activated by the runtime system (ExecuteRuntimeStep)
    // after Run() returns, so we just need to return success
    return E_OK;
}

// Register the Start block
REGISTER_BLOCK(StartBlock, "Start");

