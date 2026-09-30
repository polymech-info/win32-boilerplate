#pragma once

#include "block.h"
#include <array>

class Container;
namespace crude_json { struct value; }

// Operators supported by Logic.Test block
enum class LogicTestOperator
{
    Equal = 0,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual
};

class LogicTestBlock : public ParameterizedBlock
{
public:
    LogicTestBlock(int id);

    void Build(Node& node, App* app) override;
    int Run(Node& node, App* app) override;
    NH_CSTRING GetBlockType() const override { return "Logic.Test"; }
    void SaveState(Node& node, crude_json::value& nodeData, const Container* container, App* app) override;
    void LoadState(Node& node, const crude_json::value& nodeData, Container* container, App* app) override;
    void OnMenu(Node& node, App* app) override;
    bool ShouldAutoActivateDefaultOutput() const override { return false; }

private:
    void RebuildPins(Node& node, App* app);
    void EnsureDefaultParamValues(Node& node);

    template <typename T>
    bool EvaluateOrdered(const T& a, const T& b) const;
    bool EvaluateStrings(const std::string& a, const std::string& b) const;

    PinType m_ParamType;
    LogicTestOperator m_Operator;

    int m_FlowInputId;
    std::array<int, 2> m_FlowOutputIds; // 0 = True, 1 = False
    std::array<int, 2> m_ValueParamIds; // 0 = A, 1 = B
};


