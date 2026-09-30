#include "logic_blocks.h"

#include "../app.h"
#include "../Logging.h"
#include <crude_json.h>
#include <imgui.h>
#include <map>
#include <string>

namespace
{
    const char* ToString(LogicTestOperator op)
    {
        switch (op)
        {
            case LogicTestOperator::Equal:        return "Equal";
            case LogicTestOperator::NotEqual:     return "Not Equal";
            case LogicTestOperator::Less:         return "Less";
            case LogicTestOperator::LessEqual:    return "Less or Equal";
            case LogicTestOperator::Greater:      return "Greater";
            case LogicTestOperator::GreaterEqual: return "Greater or Equal";
            default:                              return "Unknown";
        }
    }
}

LogicTestBlock::LogicTestBlock(int id)
    : ParameterizedBlock(id, "Test")
    , m_ParamType(PinType::Int)
    , m_Operator(LogicTestOperator::Equal)
    , m_FlowInputId(-1)
    , m_FlowOutputIds({-1, -1})
    , m_ValueParamIds({-1, -1})
{
    SetTypeName("Logic.Test");
    m_Type = NodeType::Blueprint;
    m_Color = ImColor(230, 180, 95);
}

void LogicTestBlock::Build(Node& node, App* app)
{
    node.Type = m_Type;
    node.Color = m_Color;

    m_InputParams.clear();
    m_OutputParams.clear();
    m_Inputs.clear();
    m_Outputs.clear();

    // Flow input (Execute)
    if (m_FlowInputId < 0)
    {
        m_FlowInputId = app->GetNextId();
    }
    m_Inputs.push_back(m_FlowInputId);
    node.Inputs.emplace_back(m_FlowInputId, "", PinType::Flow);

    // Parameter inputs (A, B)
    for (int i = 0; i < 2; ++i)
    {
        if (m_ValueParamIds[i] < 0)
        {
            m_ValueParamIds[i] = app->GetNextId();
        }

        m_InputParams.push_back(m_ValueParamIds[i]);
        const char* label = (i == 0) ? "A" : "B";
        node.Inputs.emplace_back(m_ValueParamIds[i], label, m_ParamType);
    }

    // Flow outputs (True, False)
    static const char* kFlowNames[2] = { "True", "False" };
    for (int i = 0; i < 2; ++i)
    {
        if (m_FlowOutputIds[i] < 0)
        {
            m_FlowOutputIds[i] = app->GetNextId();
        }

        m_Outputs.push_back(m_FlowOutputIds[i]);
        node.Outputs.emplace_back(m_FlowOutputIds[i], kFlowNames[i], PinType::Flow);
    }

    EnsureDefaultParamValues(node);
}

template <typename T>
bool LogicTestBlock::EvaluateOrdered(const T& a, const T& b) const
{
    switch (m_Operator)
    {
        case LogicTestOperator::Equal:        return a == b;
        case LogicTestOperator::NotEqual:     return a != b;
        case LogicTestOperator::Less:         return a < b;
        case LogicTestOperator::LessEqual:    return a <= b;
        case LogicTestOperator::Greater:      return a > b;
        case LogicTestOperator::GreaterEqual: return a >= b;
    }
    return false;
}

bool LogicTestBlock::EvaluateStrings(const std::string& a, const std::string& b) const
{
    switch (m_Operator)
    {
        case LogicTestOperator::Equal:        return a == b;
        case LogicTestOperator::NotEqual:     return a != b;
        case LogicTestOperator::Less:         return a < b;
        case LogicTestOperator::LessEqual:    return a <= b;
        case LogicTestOperator::Greater:      return a > b;
        case LogicTestOperator::GreaterEqual: return a >= b;
    }
    return false;
}

int LogicTestBlock::Run(Node& node, App* app)
{
    // Gather parameter pins (skip flow pins)
    const Pin* valuePins[2] = { nullptr, nullptr };
    int valueIndex = 0;
    for (const auto& pin : node.Inputs)
    {
        if (pin.Type == PinType::Flow)
            continue;
        if (valueIndex < 2)
        {
            valuePins[valueIndex++] = &pin;
        }
    }

    if (!valuePins[0] || !valuePins[1])
    {
        LOG_WARN("[Logic.Test] Missing parameter inputs on node {}", ToRuntimeId(node.ID));
        ActivateOutput(0, false);
        ActivateOutput(1, false);
        return E_OK;
    }

    bool comparisonResult = false;

    switch (m_ParamType)
    {
        case PinType::Bool:
        {
            bool a = GetInputParamValueBool(*valuePins[0], node, app, false);
            bool b = GetInputParamValueBool(*valuePins[1], node, app, false);
            // Treat booleans as integers for ordered comparisons
            comparisonResult = EvaluateOrdered<int>(a ? 1 : 0, b ? 1 : 0);
            break;
        }
        case PinType::Int:
        {
            int a = GetInputParamValueInt(*valuePins[0], node, app, 0);
            int b = GetInputParamValueInt(*valuePins[1], node, app, 0);
            comparisonResult = EvaluateOrdered<int>(a, b);
            break;
        }
        case PinType::Float:
        {
            float a = GetInputParamValueFloat(*valuePins[0], node, app, 0.0f);
            float b = GetInputParamValueFloat(*valuePins[1], node, app, 0.0f);
            comparisonResult = EvaluateOrdered<float>(a, b);
            break;
        }
        case PinType::String:
        {
            std::string a = GetInputParamValueString(*valuePins[0], node, app, "");
            std::string b = GetInputParamValueString(*valuePins[1], node, app, "");
            comparisonResult = EvaluateStrings(a, b);
            break;
        }
        default:
        {
            LOG_WARN("[Logic.Test] Unsupported parameter type on node {}", ToRuntimeId(node.ID));
            comparisonResult = false;
            break;
        }
    }

    ActivateOutput(0, comparisonResult);
    ActivateOutput(1, !comparisonResult);

    LOG_DEBUG("[Logic.Test] Node {} result={} (operator={})", ToRuntimeId(node.ID), comparisonResult ? "true" : "false", ToString(m_Operator));

    return E_OK;
}

void LogicTestBlock::EnsureDefaultParamValues(Node& node)
{
    for (int i = 0; i < 2; ++i)
    {
        int pinId = m_ValueParamIds[i];
        if (pinId < 0)
            continue;

        std::string& value = node.UnconnectedParamValues[pinId];

        switch (m_ParamType)
        {
            case PinType::Bool:
            {
                if (!(value == "true" || value == "false" || value == "1" || value == "0"))
                    value = "false";
                break;
            }
            case PinType::Int:
            {
                if (value.empty())
                    value = "0";
                break;
            }
            case PinType::Float:
            {
                if (value.empty())
                    value = "0.0";
                break;
            }
            case PinType::String:
            default:
            {
                // Nothing to do; entry already exists
                break;
            }
        }
    }
}

void LogicTestBlock::RebuildPins(Node& node, App* app)
{
    std::map<int, Uuid64> oldPinUuids;
    for (const auto& input : node.Inputs)
    {
        const int pinId = ToRuntimeId(input.ID);
        if (input.UUID.IsValid())
        {
            oldPinUuids[pinId] = input.UUID;
        }
    }
    for (const auto& output : node.Outputs)
    {
        if (output.UUID.IsValid())
        {
            oldPinUuids[ToRuntimeId(output.ID)] = output.UUID;
        }
    }

    node.Inputs.clear();
    node.Outputs.clear();
    m_InputParams.clear();
    m_OutputParams.clear();
    m_Inputs.clear();
    m_Outputs.clear();

    Build(node, app);

    for (auto& input : node.Inputs)
    {
        const int pinId = ToRuntimeId(input.ID);
        auto it = oldPinUuids.find(pinId);
        if (it != oldPinUuids.end())
        {
            input.UUID = it->second;
            app->m_UuidIdManager.RegisterPin(input.UUID, pinId);
        }
        else
        {
            input.UUID = app->m_UuidIdManager.GenerateUuid();
            app->m_UuidIdManager.RegisterPin(input.UUID, pinId);
        }
        input.Node = &node;
        input.Kind = PinKind::Input;
    }

    for (auto& output : node.Outputs)
    {
        const int pinId = ToRuntimeId(output.ID);
        auto it = oldPinUuids.find(pinId);
        if (it != oldPinUuids.end())
        {
            output.UUID = it->second;
            app->m_UuidIdManager.RegisterPin(output.UUID, pinId);
        }
        else
        {
            output.UUID = app->m_UuidIdManager.GenerateUuid();
            app->m_UuidIdManager.RegisterPin(output.UUID, pinId);
        }
        output.Node = &node;
        output.Kind = PinKind::Output;
    }
}

void LogicTestBlock::SaveState(Node& node, crude_json::value& nodeData, const Container* container, App* app)
{
    ParameterizedBlock::SaveState(node, nodeData, container, app);

    nodeData["logic_test_param_type"] = (double)static_cast<int>(m_ParamType);
    nodeData["logic_test_operator"] = (double)static_cast<int>(m_Operator);
    nodeData["logic_test_flow_input_id"] = (double)m_FlowInputId;
    nodeData["logic_test_flow_output_true_id"] = (double)m_FlowOutputIds[0];
    nodeData["logic_test_flow_output_false_id"] = (double)m_FlowOutputIds[1];
    nodeData["logic_test_param_a_id"] = (double)m_ValueParamIds[0];
    nodeData["logic_test_param_b_id"] = (double)m_ValueParamIds[1];
}

void LogicTestBlock::LoadState(Node& node, const crude_json::value& nodeData, Container* container, App* app)
{
    if (nodeData.contains("logic_test_param_type"))
    {
        m_ParamType = static_cast<PinType>((int)nodeData["logic_test_param_type"].get<double>());
    }

    if (nodeData.contains("logic_test_operator"))
    {
        m_Operator = static_cast<LogicTestOperator>((int)nodeData["logic_test_operator"].get<double>());
    }

    if (nodeData.contains("logic_test_flow_input_id"))
        m_FlowInputId = (int)nodeData["logic_test_flow_input_id"].get<double>();
    if (nodeData.contains("logic_test_flow_output_true_id"))
        m_FlowOutputIds[0] = (int)nodeData["logic_test_flow_output_true_id"].get<double>();
    if (nodeData.contains("logic_test_flow_output_false_id"))
        m_FlowOutputIds[1] = (int)nodeData["logic_test_flow_output_false_id"].get<double>();
    if (nodeData.contains("logic_test_param_a_id"))
        m_ValueParamIds[0] = (int)nodeData["logic_test_param_a_id"].get<double>();
    if (nodeData.contains("logic_test_param_b_id"))
        m_ValueParamIds[1] = (int)nodeData["logic_test_param_b_id"].get<double>();

    // If IDs were not saved (backward compatibility), capture current IDs from node
    if (m_FlowInputId < 0 && !node.Inputs.empty())
        m_FlowInputId = ToRuntimeId(node.Inputs.front().ID);

    int paramCounter = 0;
    for (const auto& pin : node.Inputs)
    {
        if (pin.Type == PinType::Flow)
            continue;

        int valueIndex = paramCounter;
        if (valueIndex < 2 && m_ValueParamIds[valueIndex] < 0)
            m_ValueParamIds[valueIndex] = ToRuntimeId(pin.ID);

        ++paramCounter;
    }

    int flowOutCounter = 0;
    for (const auto& pin : node.Outputs)
    {
        if (pin.Type != PinType::Flow)
            continue;
        if (flowOutCounter < 2 && m_FlowOutputIds[flowOutCounter] < 0)
        {
            m_FlowOutputIds[flowOutCounter] = ToRuntimeId(pin.ID);
        }
        ++flowOutCounter;
    }

    RebuildPins(node, app);
    ParameterizedBlock::LoadState(node, nodeData, container, app);
    EnsureDefaultParamValues(node);
}

void LogicTestBlock::OnMenu(Node& node, App* app)
{
    ParameterizedBlock::OnMenu(node, app);

    ImGui::Separator();
    ImGui::TextUnformatted("Logic.Test");

    if (ImGui::BeginMenu("Parameter Type"))
    {
        const struct { PinType Type; const char* Label; } options[] = {
            { PinType::Bool,   "Bool" },
            { PinType::Int,    "Int" },
            { PinType::Float,  "Float" },
            { PinType::String, "String" }
        };

        for (const auto& option : options)
        {
            bool selected = (m_ParamType == option.Type);
            if (ImGui::MenuItem(option.Label, nullptr, selected))
            {
                m_ParamType = option.Type;
                RebuildPins(node, app);
                EnsureDefaultParamValues(node);
            }
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Operator"))
    {
        const struct { LogicTestOperator Op; const char* Label; } ops[] = {
            { LogicTestOperator::Equal, "Equal (==)" },
            { LogicTestOperator::NotEqual, "Not Equal (!=)" },
            { LogicTestOperator::Less, "Less (<)" },
            { LogicTestOperator::LessEqual, "Less or Equal (<=)" },
            { LogicTestOperator::Greater, "Greater (>)" },
            { LogicTestOperator::GreaterEqual, "Greater or Equal (>=)" }
        };

        for (const auto& entry : ops)
        {
            bool selected = (m_Operator == entry.Op);
            if (ImGui::MenuItem(entry.Label, nullptr, selected))
            {
                m_Operator = entry.Op;
            }
        }

        ImGui::EndMenu();
    }

    ImGui::Separator();
    ImGui::Text("Current Operator: %s", ToString(m_Operator));
}

REGISTER_BLOCK(LogicTestBlock, "Logic.Test");


