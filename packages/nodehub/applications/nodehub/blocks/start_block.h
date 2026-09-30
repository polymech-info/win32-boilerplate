#pragma once
#include "block.h"

// Start block - entry point for graph execution
// Has no flow input, only a flow output
// Anything connected to its output will execute when the graph runs
class StartBlock : public ParameterizedBlock
{
public:
    StartBlock(int id) : ParameterizedBlock(id, "Start") 
    {
        SetTypeName("Start");
        m_Type = NodeType::Blueprint;
        m_Color = ImColor(100, 255, 100);
    }
    
    void Build(Node& node, App* app) override;
    int Run(Node& node, App* app) override;
    NH_CSTRING GetBlockType() const override { return "Start"; }
};

