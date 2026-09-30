#pragma once
#include "block.h"
//#include "parameter.h"

// Example blocks - Math operations

class AddBlock : public ParameterizedBlock
{
public:
    AddBlock(int id) : ParameterizedBlock(id, "Add") 
    {
        SetTypeName("Math.Add");
        m_Type = NodeType::Blueprint;
        m_Color = ImColor(128, 195, 248);
    }
    
    void Build(Node& node, App* app) override;
    int Run(Node& node, App* app) override;
    NH_CSTRING GetBlockType() const override { return "Math.Add"; }
};

class MultiplyBlock : public ParameterizedBlock
{
public:
    MultiplyBlock(int id) : ParameterizedBlock(id, "Multiply")
    {
        SetTypeName("Math.Multiply");
        m_Type = NodeType::Blueprint;
        m_Color = ImColor(128, 195, 248);
    }
    
    void Build(Node& node, App* app) override;
    int Run(Node& node, App* app) override;
    NH_CSTRING GetBlockType() const override { return "Math.Multiply"; }
};

class CompareBlock : public ParameterizedBlock
{
public:
    CompareBlock(int id) : ParameterizedBlock(id, "Compare")
    {
        SetTypeName("Math.Compare");
        m_Type = NodeType::Blueprint;
        m_Color = ImColor(128, 195, 248);
    }
    
    void Build(Node& node, App* app) override;
    int Run(Node& node, App* app) override;
    NH_CSTRING GetBlockType() const override { return "Math.Compare"; }
};

