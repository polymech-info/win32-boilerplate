#include "math_blocks.h"
#include "../app.h"
#include "block.h"
#include "../Logging.h"

void AddBlock::Build(Node& node, App* app)
{
    node.Type = m_Type;
    node.Color = m_Color;
    
    // Flow control (left/right) - add FIRST so they're at indices 0
    AddInput(app, node, "Execute");
    
    // Input parameters (connections or values)
    AddInputParameter(app, node, "A", PinType::Int);
    AddInputParameter(app, node, "B", PinType::Int);
    
    // Flow outputs
    AddOutput(app, node, "Done");

    // Output parameters
    AddOutputParameter(app, node, "Result", PinType::Int);

}

void MultiplyBlock::Build(Node& node, App* app)
{
    node.Type = m_Type;
    node.Color = m_Color;
    
    // Flow control
    AddInput(app, node, "Execute");
    
    // Input parameters
    AddInputParameter(app, node, "A", PinType::Int);
    AddInputParameter(app, node, "B", PinType::Int);
    
    // Flow outputs
    AddOutput(app, node, "Done");
    
    // Output parameters
    AddOutputParameter(app, node, "Result", PinType::Int);
}

void CompareBlock::Build(Node& node, App* app)
{
    node.Type = m_Type;
    node.Color = m_Color;
    
    // Flow control
    AddInput(app, node, "Execute");
    
    // Input parameters
    AddInputParameter(app, node, "A", PinType::Int);
    AddInputParameter(app, node, "B", PinType::Int);
    
    // Flow outputs
    AddOutput(app, node, "Done");
    
    // Output parameters (multiple outputs for comparison results)
    AddOutputParameter(app, node, "==", PinType::Bool);
    AddOutputParameter(app, node, "<", PinType::Bool);
    AddOutputParameter(app, node, ">", PinType::Bool);
}

// Helper functions moved to ParameterizedBlock base class
// Use ParameterizedBlock::GetInputParamValueInt/SetOutputParamValueInt/etc.

int AddBlock::Run(Node& node, App* app)
{
    // Get input parameters (skip flow pins)
    int inputA = 0, inputB = 0;
    int paramIndex = 0;    
    for (const auto& pin : node.Inputs)
    {
        if (pin.Type == PinType::Flow)
            continue;
        
        if (paramIndex == 0)
            inputA = ParameterizedBlock::GetInputParamValueInt(pin, node, app, 0);
        else if (paramIndex == 1)
            inputB = ParameterizedBlock::GetInputParamValueInt(pin, node, app, 0);
        
        paramIndex++;
    }
    
    // Perform addition
    int result = inputA + inputB;
    
    
    // Set output parameter value
    paramIndex = 0;
    for (const auto& pin : node.Outputs)
    {
        if (pin.Type == PinType::Flow)
        continue;
    
    if (paramIndex == 0)
    {
        ParameterizedBlock::SetOutputParamValueInt(pin, node, app, result);
        break;
    }
    paramIndex++;
    }
    
    LOG_TRACE("[Math.Add] Computed: {} + {} = {}", inputA, inputB, result);
    
    return E_OK;
}

int MultiplyBlock::Run(Node& node, App* app)
{
    LOG_INFO("[Math.Multiply] Block executing (ID: {})", GetID());

    // Get input parameters (skip flow pins)
    int inputA = 0, inputB = 0;
    int paramIndex = 0;
    
    for (const auto& pin : node.Inputs)
    {
        if (pin.Type == PinType::Flow)
            continue;
        
        if (paramIndex == 0)
            inputA = ParameterizedBlock::GetInputParamValueInt(pin, node, app, 0);
        else if (paramIndex == 1)
            inputB = ParameterizedBlock::GetInputParamValueInt(pin, node, app, 0);
        
        paramIndex++;
    }
    
    // Perform multiplication
    int result = inputA * inputB;

    LOG_INFO("[Math.Multiply] Computed: {} * {} = {}", inputA, inputB, result);
    
    // Set output parameter value
    paramIndex = 0;
    for (const auto& pin : node.Outputs)
    {
        if (pin.Type == PinType::Flow)
            continue;
        
        if (paramIndex == 0)
        {
            ParameterizedBlock::SetOutputParamValueInt(pin, node, app, result);
            break;
        }
        paramIndex++;
    }
    
    return E_OK;
}

int CompareBlock::Run(Node& node, App* app)
{
    LOG_INFO("[Math.Compare] Block executing (ID: {})", GetID());

    // Get input parameters (skip flow pins)
    int inputA = 0, inputB = 0;
    int paramIndex = 0;
    
    for (const auto& pin : node.Inputs)
    {
        if (pin.Type == PinType::Flow)
            continue;
        
        if (paramIndex == 0)
            inputA = ParameterizedBlock::GetInputParamValueInt(pin, node, app, 0);
        else if (paramIndex == 1)
            inputB = ParameterizedBlock::GetInputParamValueInt(pin, node, app, 0);
        
        paramIndex++;
    }
    
    // Perform comparisons and set output parameter values
    paramIndex = 0;
    for (const auto& pin : node.Outputs)
    {
        if (pin.Type == PinType::Flow)
            continue;
        
        bool result = false;
        if (paramIndex == 0) // ==
            result = (inputA == inputB);
        else if (paramIndex == 1) // <
            result = (inputA < inputB);
        else if (paramIndex == 2) // >
            result = (inputA > inputB);
        LOG_DEBUG("[Math.Compare] Output {} = {}", paramIndex == 0 ? "==" : paramIndex == 1 ? "<" : ">", result);
        
        // Set output parameter value (propagates to connected nodes)
        ParameterizedBlock::SetOutputParamValueBool(pin, node, app, result);
        
        paramIndex++;
    }
    
    return E_OK;
}

// Register blocks
REGISTER_BLOCK(AddBlock, "Math.Add");
REGISTER_BLOCK(MultiplyBlock, "Math.Multiply");
REGISTER_BLOCK(CompareBlock, "Math.Compare");

