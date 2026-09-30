#pragma once
#include "block.h"
#include <string>
#include <vector>

// Operation definition for parameter operations
struct ParameterOperationDef
{
    std::string UUID;        // Unique identifier for the operation
    std::string Label;       // Display label (e.g., "Multiplication", "Addition")
    PinType InputAType;      // Type of first input
    PinType InputBType;      // Type of second input
    PinType OutputType;      // Type of output
    
    ParameterOperationDef(const std::string& uuid, const std::string& label,
                          PinType inputA, PinType inputB, PinType output)
        : UUID(uuid), Label(label), InputAType(inputA), InputBType(inputB), OutputType(output) {}
};

// Registry for parameter operations
class ParameterOperationRegistry
{
public:
    static ParameterOperationRegistry& Instance()
    {
        static ParameterOperationRegistry instance;
        return instance;
    }
    
    // Register a new operation
    void RegisterOperation(const ParameterOperationDef& opDef);
    
    // Get all operations matching input/output types
    std::vector<ParameterOperationDef> GetMatchingOperations(PinType inputA, PinType inputB, PinType output);
    
    // Get operation by UUID
    const ParameterOperationDef* GetOperation(const std::string& uuid);
    
private:
    ParameterOperationRegistry();
    std::vector<ParameterOperationDef> m_Operations;
};

// Parameter Operation Block
class ParameterOperationBlock : public ParameterizedBlock
{
public:
    ParameterOperationBlock(int id) : ParameterizedBlock(id, "ParamOp")
    {
        SetTypeName("ParamOp");
        m_Type = NodeType::Blueprint;
        m_Color = ImColor(180, 200, 180); // Light green
        SetFlags(static_cast<NH_BEHAVIOR_FLAGS>(NHBEHAVIOR_SCRIPT));
        
        // Default types
        m_InputAType = PinType::Int;
        m_InputBType = PinType::Int;
        m_OutputType = PinType::Int;
        m_OperationUUID = ""; // No operation selected by default
    }
    
    void Build(Node& node, App* app) override;
    int Run(Node& node, App* app) override;
    void Render(Node& node, App* app, Pin* newLinkPin) override;
    NH_CSTRING GetBlockType() const override { return "ParamOp"; }
    
    void OnMenu(Node& node, App* app) override;
    
    // Save/load state
    void SaveState(Node& node, crude_json::value& nodeData, const Container* container, App* app) override;
    void LoadState(Node& node, const crude_json::value& nodeData, Container* container, App* app) override;
    
    // Type management
    void SetInputAType(PinType type) { m_InputAType = type; }
    void SetInputBType(PinType type) { m_InputBType = type; }
    void SetOutputType(PinType type) { m_OutputType = type; }
    PinType GetInputAType() const { return m_InputAType; }
    PinType GetInputBType() const { return m_InputBType; }
    PinType GetOutputType() const { return m_OutputType; }
    
    // Operation management
    void SetOperation(const std::string& uuid) { m_OperationUUID = uuid; }
    std::string GetOperationUUID() const { return m_OperationUUID; }
    
private:
    PinType m_InputAType;
    PinType m_InputBType;
    PinType m_OutputType;
    std::string m_OperationUUID;
};

