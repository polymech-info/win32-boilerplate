#define IMGUI_DEFINE_MATH_OPERATORS
#include "parameter_operation.h"
#include "../app.h"
#include "../utilities/node_renderer_base.h"
#include "NodeEx.h"
#include <crude_json.h>
#include <imgui.h>
#include <imgui_node_editor.h>

namespace ed = ax::NodeEditor;
using namespace ax::NodeRendering;

//------------------------------------------------------------------------------
// Parameter Operation Registry
//------------------------------------------------------------------------------

ParameterOperationRegistry::ParameterOperationRegistry()
{
    // Register some default operations (stubbed for now)
    // Int operations
    RegisterOperation(ParameterOperationDef("int_add", "Add", PinType::Int, PinType::Int, PinType::Int));
    RegisterOperation(ParameterOperationDef("int_sub", "Subtract", PinType::Int, PinType::Int, PinType::Int));
    RegisterOperation(ParameterOperationDef("int_mul", "Multiply", PinType::Int, PinType::Int, PinType::Int));
    RegisterOperation(ParameterOperationDef("int_div", "Divide", PinType::Int, PinType::Int, PinType::Int));
    
    // Float operations
    RegisterOperation(ParameterOperationDef("float_add", "Add", PinType::Float, PinType::Float, PinType::Float));
    RegisterOperation(ParameterOperationDef("float_sub", "Subtract", PinType::Float, PinType::Float, PinType::Float));
    RegisterOperation(ParameterOperationDef("float_mul", "Multiply", PinType::Float, PinType::Float, PinType::Float));
    RegisterOperation(ParameterOperationDef("float_div", "Divide", PinType::Float, PinType::Float, PinType::Float));
    
    // String operations
    RegisterOperation(ParameterOperationDef("string_concat", "Concatenate", PinType::String, PinType::String, PinType::String));
    
    // Bool operations
    RegisterOperation(ParameterOperationDef("bool_and", "AND", PinType::Bool, PinType::Bool, PinType::Bool));
    RegisterOperation(ParameterOperationDef("bool_or", "OR", PinType::Bool, PinType::Bool, PinType::Bool));
    
    // Comparison operations (Int -> Bool)
    RegisterOperation(ParameterOperationDef("int_eq", "Equal", PinType::Int, PinType::Int, PinType::Bool));
    RegisterOperation(ParameterOperationDef("int_lt", "Less Than", PinType::Int, PinType::Int, PinType::Bool));
    RegisterOperation(ParameterOperationDef("int_gt", "Greater Than", PinType::Int, PinType::Int, PinType::Bool));
    
    // Float comparison
    RegisterOperation(ParameterOperationDef("float_eq", "Equal", PinType::Float, PinType::Float, PinType::Bool));
    RegisterOperation(ParameterOperationDef("float_lt", "Less Than", PinType::Float, PinType::Float, PinType::Bool));
    RegisterOperation(ParameterOperationDef("float_gt", "Greater Than", PinType::Float, PinType::Float, PinType::Bool));
}

void ParameterOperationRegistry::RegisterOperation(const ParameterOperationDef& opDef)
{
    m_Operations.push_back(opDef);
}

std::vector<ParameterOperationDef> ParameterOperationRegistry::GetMatchingOperations(
    PinType inputA, PinType inputB, PinType output)
{
    std::vector<ParameterOperationDef> matching;
    for (const auto& op : m_Operations)
    {
        if (op.InputAType == inputA && op.InputBType == inputB && op.OutputType == output)
        {
            matching.push_back(op);
        }
    }
    return matching;
}

const ParameterOperationDef* ParameterOperationRegistry::GetOperation(const std::string& uuid)
{
    for (const auto& op : m_Operations)
    {
        if (op.UUID == uuid)
            return &op;
    }
    return nullptr;
}

//------------------------------------------------------------------------------
// Parameter Operation Block
//------------------------------------------------------------------------------

void ParameterOperationBlock::Build(Node& node, App* app)
{
    node.Type = m_Type;
    node.Color = m_Color;
    
    // Clear existing pins
    m_InputParams.clear();
    m_OutputParams.clear();
    
    // Build 2 input parameters at top
    AddInputParameter(app, node, "A", m_InputAType);
    AddInputParameter(app, node, "B", m_InputBType);
    
    // Build 1 output parameter at bottom
    AddOutputParameter(app, node, "Result", m_OutputType);
}

int ParameterOperationBlock::Run(Node& node, App* app)
{
    // Stub for now - will execute the selected operation
    if (m_OperationUUID.empty())
    {
        printf("[ParamOp] No operation selected\n");
        return E_OK;
    }
    
    auto* opDef = ParameterOperationRegistry::Instance().GetOperation(m_OperationUUID);
    if (!opDef)
    {
        printf("[ParamOp] Operation '%s' not found\n", m_OperationUUID.c_str());
        return E_OK;
    }
    
    printf("[ParamOp] Executing operation: %s (%s)\n", opDef->Label.c_str(), opDef->UUID.c_str());
    
    // Get input values (stub - actual implementation would read from connected parameters)
    // For now, just get default values
    int paramIndex = 0;
    int inputAInt = 0, inputBInt = 0;
    float inputAFloat = 0.0f, inputBFloat = 0.0f;
    bool inputABool = false, inputBBool = false;
    std::string inputAString = "", inputBString = "";
    
    // Read input values based on type
    for (const auto& pin : node.Inputs)
    {
        if (pin.Type == PinType::Flow)
            continue;
        
        if (paramIndex == 0)
        {
            // Input A
            switch (m_InputAType)
            {
                case PinType::Int: inputAInt = GetInputParamValueInt(pin, node, app, 0); break;
                case PinType::Float: inputAFloat = GetInputParamValueFloat(pin, node, app, 0.0f); break;
                case PinType::Bool: inputABool = GetInputParamValueBool(pin, node, app, false); break;
                case PinType::String: inputAString = GetInputParamValueString(pin, node, app, ""); break;
                default: break;
            }
        }
        else if (paramIndex == 1)
        {
            // Input B
            switch (m_InputBType)
            {
                case PinType::Int: inputBInt = GetInputParamValueInt(pin, node, app, 0); break;
                case PinType::Float: inputBFloat = GetInputParamValueFloat(pin, node, app, 0.0f); break;
                case PinType::Bool: inputBBool = GetInputParamValueBool(pin, node, app, false); break;
                case PinType::String: inputBString = GetInputParamValueString(pin, node, app, ""); break;
                default: break;
            }
        }
        
        paramIndex++;
    }
    
    // Execute operation (stub implementations)
    bool resultBool = false;
    int resultInt = 0;
    float resultFloat = 0.0f;
    std::string resultString = "";
    
    if (opDef->UUID == "int_add")
        resultInt = inputAInt + inputBInt;
    else if (opDef->UUID == "int_sub")
        resultInt = inputAInt - inputBInt;
    else if (opDef->UUID == "int_mul")
        resultInt = inputAInt * inputBInt;
    else if (opDef->UUID == "int_div")
        resultInt = inputBInt != 0 ? inputAInt / inputBInt : 0;
    else if (opDef->UUID == "float_add")
        resultFloat = inputAFloat + inputBFloat;
    else if (opDef->UUID == "float_sub")
        resultFloat = inputAFloat - inputBFloat;
    else if (opDef->UUID == "float_mul")
        resultFloat = inputAFloat * inputBFloat;
    else if (opDef->UUID == "float_div")
        resultFloat = inputBFloat != 0.0f ? inputAFloat / inputBFloat : 0.0f;
    else if (opDef->UUID == "string_concat")
        resultString = inputAString + inputBString;
    else if (opDef->UUID == "bool_and")
        resultBool = inputABool && inputBBool;
    else if (opDef->UUID == "bool_or")
        resultBool = inputABool || inputBBool;
    else if (opDef->UUID == "int_eq")
        resultBool = (inputAInt == inputBInt);
    else if (opDef->UUID == "int_lt")
        resultBool = (inputAInt < inputBInt);
    else if (opDef->UUID == "int_gt")
        resultBool = (inputAInt > inputBInt);
    else if (opDef->UUID == "float_eq")
        resultBool = (inputAFloat == inputBFloat);
    else if (opDef->UUID == "float_lt")
        resultBool = (inputAFloat < inputBFloat);
    else if (opDef->UUID == "float_gt")
        resultBool = (inputAFloat > inputBFloat);
    
    // Set output value
    paramIndex = 0;
    for (const auto& pin : node.Outputs)
    {
        if (pin.Type == PinType::Flow)
            continue;
        
        if (paramIndex == 0)
        {
            // Output
            switch (m_OutputType)
            {
                case PinType::Int: SetOutputParamValueInt(pin, node, app, resultInt); break;
                case PinType::Float: SetOutputParamValueFloat(pin, node, app, resultFloat); break;
                case PinType::Bool: SetOutputParamValueBool(pin, node, app, resultBool); break;
                case PinType::String: SetOutputParamValueString(pin, node, app, resultString); break;
                default: break;
            }
            break;
        }
        paramIndex++;
    }
    
    return E_OK;
}

void ParameterOperationBlock::Render(Node& node, App* app, Pin* newLinkPin)
{
    // Check if node is currently running (for red border visualization)
    double currentTime = (double)ImGui::GetTime();
    bool isRunning = false;
    auto runningIt = app->m_RunningNodes.find(node.ID);
    if (runningIt != app->m_RunningNodes.end())
    {
        isRunning = (currentTime < runningIt->second);
    }
    
    // Get styles from StyleManager - use ParameterStyle for visual distinction
    auto& styleManager = app->GetStyleManager();
    auto& paramStyle = styleManager.ParameterStyle;
    
    // Parameter Operations get a slightly blue border for distinction
    ImColor borderColor = isRunning ? paramStyle.BorderColorRunning : ImColor(180, 200, 255, 255);  // Light blue
    float activeBorderWidth = isRunning ? paramStyle.BorderWidthRunning : paramStyle.BorderWidth;
    
    // Use NodeStyleScope with ParameterStyle for parameter-like appearance
    NodeStyleScope style(
        paramStyle.BgColor,                 // Parameter-style background (grayer)
        borderColor,                        // border (red if running)
        paramStyle.Rounding, activeBorderWidth,  // More rounded than blocks
        paramStyle.Padding,                 // padding
        ImVec2(0.0f, 1.0f),                 // source direction (down)
        ImVec2(0.0f, -1.0f)                 // target direction (up)
    );
    
    ed::BeginNode(node.ID);
    ImGui::PushID(node.ID.AsPointer());
    
    ImGui::BeginVertical("node");
    
    // Center - header (simple centered text)
    ImGui::BeginHorizontal("content");
    ImGui::Spring(1, 0.0f);
    
    ImGui::BeginVertical("center");
    ImGui::Dummy(ImVec2(styleManager.MinNodeWidth, 0));  // Minimum width
    ImGui::Spring(1);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    ImGui::TextUnformatted(node.Name.c_str());
    ImGui::PopStyleColor();
    ImGui::Spring(1);
    ImGui::EndVertical();
    
    ImGui::Spring(1, 0.0f);
    ImGui::EndHorizontal();
    
    ImGui::EndVertical();
    
    // Save cursor and get node bounds
    ImVec2 contentEndPos = ImGui::GetCursorScreenPos();
    ImVec2 nodePos = ed::GetNodePosition(node.ID);
    ImVec2 nodeSize = ed::GetNodeSize(node.ID);
    
    if (nodeSize.x <= 0 || nodeSize.y <= 0)
    {
        ImVec2 contentMin = ImGui::GetItemRectMin();
        ImVec2 contentMax = ImGui::GetItemRectMax();
        nodeSize = contentMax - contentMin;
    }
    
    ImRect nodeRect = ImRect(nodePos, nodePos + nodeSize);
    
    // Collect pins by type
    std::vector<Pin*> inputParams;
    std::vector<Pin*> outputParams;
    
    for (auto& pin : node.Inputs)
    {
        if (pin.Type != PinType::Flow)
            inputParams.push_back(&pin);
    }
    
    for (auto& pin : node.Outputs)
    {
        if (pin.Type != PinType::Flow)
            outputParams.push_back(&pin);
    }
    
    // Render input parameters at top edge using NodeEx
    if (!inputParams.empty())
    {
        float spacing = 1.0f / (inputParams.size() + 1);
        for (size_t i = 0; i < inputParams.size(); ++i)
        {
            Pin* pin = inputParams[i];
            float offset = spacing * (i + 1);
            float alpha = GetPinAlpha(pin, newLinkPin, app);
            
            ed::PinState state = (alpha < 1.0f) ? ed::PinState::Deactivated : ed::PinState::Normal;
            ImRect pinRect = ed::PinEx(pin->ID, ed::PinKind::Input, ed::PinEdge::Top, 
                                       offset, styleManager.ParameterPinEdgeOffset, nodeRect, state);
            
            pin->LastPivotPosition = ImVec2(pinRect.GetCenter().x, pinRect.Min.y);
            pin->LastRenderBounds = pinRect;
            pin->HasPositionData = true;
        }
    }
    
    // Render output parameters at bottom edge using NodeEx
    if (!outputParams.empty())
    {
        float spacing = 1.0f / (outputParams.size() + 1);
        for (size_t i = 0; i < outputParams.size(); ++i)
        {
            Pin* pin = outputParams[i];
            float offset = spacing * (i + 1);
            float alpha = GetPinAlpha(pin, newLinkPin, app);
            
            ed::PinState state = (alpha < 1.0f) ? ed::PinState::Deactivated : ed::PinState::Normal;
            ImRect pinRect = ed::PinEx(pin->ID, ed::PinKind::Output, ed::PinEdge::Bottom, 
                                       offset, styleManager.ParameterPinEdgeOffset, nodeRect, state);
            
            pin->LastPivotPosition = ImVec2(pinRect.GetCenter().x, pinRect.Max.y);
            pin->LastRenderBounds = pinRect;
            pin->HasPositionData = true;
        }
    }
    
    // Restore cursor
    ImGui::SetCursorScreenPos(contentEndPos);
    
    ImGui::PopID();
    ed::EndNode();
}

void ParameterOperationBlock::OnMenu(Node& node, App* app)
{
    // Call base class menu first
    ParameterizedBlock::OnMenu(node, app);
    
    ImGui::Separator();
    ImGui::TextUnformatted("Parameter Operation");
    
    // Show current operation
    if (!m_OperationUUID.empty())
    {
        auto* opDef = ParameterOperationRegistry::Instance().GetOperation(m_OperationUUID);
        if (opDef)
        {
            ImGui::Text("Operation: %s", opDef->Label.c_str());
        }
        else
        {
            ImGui::Text("Operation: (unknown)");
        }
    }
    else
    {
        ImGui::Text("Operation: (none selected)");
    }
    
    ImGui::Separator();
    
    // Type selection submenus
    if (ImGui::BeginMenu("Set Input A Type"))
    {
        if (ImGui::MenuItem("Bool", nullptr, m_InputAType == PinType::Bool))
        {
            m_InputAType = PinType::Bool;
            m_OperationUUID = ""; // Clear operation when type changes
            node.Inputs.clear();
            node.Outputs.clear();
            Build(node, app);
        }
        if (ImGui::MenuItem("Int", nullptr, m_InputAType == PinType::Int))
        {
            m_InputAType = PinType::Int;
            m_OperationUUID = "";
            node.Inputs.clear();
            node.Outputs.clear();
            Build(node, app);
        }
        if (ImGui::MenuItem("Float", nullptr, m_InputAType == PinType::Float))
        {
            m_InputAType = PinType::Float;
            m_OperationUUID = "";
            node.Inputs.clear();
            node.Outputs.clear();
            Build(node, app);
        }
        if (ImGui::MenuItem("String", nullptr, m_InputAType == PinType::String))
        {
            m_InputAType = PinType::String;
            m_OperationUUID = "";
            node.Inputs.clear();
            node.Outputs.clear();
            Build(node, app);
        }
        ImGui::EndMenu();
    }
    
    if (ImGui::BeginMenu("Set Input B Type"))
    {
        if (ImGui::MenuItem("Bool", nullptr, m_InputBType == PinType::Bool))
        {
            m_InputBType = PinType::Bool;
            m_OperationUUID = "";
            node.Inputs.clear();
            node.Outputs.clear();
            Build(node, app);
        }
        if (ImGui::MenuItem("Int", nullptr, m_InputBType == PinType::Int))
        {
            m_InputBType = PinType::Int;
            m_OperationUUID = "";
            node.Inputs.clear();
            node.Outputs.clear();
            Build(node, app);
        }
        if (ImGui::MenuItem("Float", nullptr, m_InputBType == PinType::Float))
        {
            m_InputBType = PinType::Float;
            m_OperationUUID = "";
            node.Inputs.clear();
            node.Outputs.clear();
            Build(node, app);
        }
        if (ImGui::MenuItem("String", nullptr, m_InputBType == PinType::String))
        {
            m_InputBType = PinType::String;
            m_OperationUUID = "";
            node.Inputs.clear();
            node.Outputs.clear();
            Build(node, app);
        }
        ImGui::EndMenu();
    }
    
    if (ImGui::BeginMenu("Set Output Type"))
    {
        if (ImGui::MenuItem("Bool", nullptr, m_OutputType == PinType::Bool))
        {
            m_OutputType = PinType::Bool;
            m_OperationUUID = "";
            node.Inputs.clear();
            node.Outputs.clear();
            Build(node, app);
        }
        if (ImGui::MenuItem("Int", nullptr, m_OutputType == PinType::Int))
        {
            m_OutputType = PinType::Int;
            m_OperationUUID = "";
            node.Inputs.clear();
            node.Outputs.clear();
            Build(node, app);
        }
        if (ImGui::MenuItem("Float", nullptr, m_OutputType == PinType::Float))
        {
            m_OutputType = PinType::Float;
            m_OperationUUID = "";
            node.Inputs.clear();
            node.Outputs.clear();
            Build(node, app);
        }
        if (ImGui::MenuItem("String", nullptr, m_OutputType == PinType::String))
        {
            m_OutputType = PinType::String;
            m_OperationUUID = "";
            node.Inputs.clear();
            node.Outputs.clear();
            Build(node, app);
        }
        ImGui::EndMenu();
    }
    
    ImGui::Separator();
    
    // Operation selection based on current types
    auto matchingOps = ParameterOperationRegistry::Instance().GetMatchingOperations(
        m_InputAType, m_InputBType, m_OutputType);
    
    if (!matchingOps.empty())
    {
        if (ImGui::BeginMenu("Select Operation"))
        {
            for (const auto& op : matchingOps)
            {
                bool isSelected = (m_OperationUUID == op.UUID);
                if (ImGui::MenuItem(op.Label.c_str(), nullptr, isSelected))
                {
                    m_OperationUUID = op.UUID;
                    // Update node name to reflect operation
                    node.Name = op.Label;
                }
            }
            ImGui::EndMenu();
        }
    }
    else
    {
        ImGui::TextDisabled("No operations available for current types");
    }
}

void ParameterOperationBlock::SaveState(Node& node, crude_json::value& nodeData, const Container* container, App* app)
{
    // Call base class
    ParameterizedBlock::SaveState(node, nodeData, container, app);
    
    // Save operation state
    nodeData["op_input_a_type"] = (double)static_cast<int>(m_InputAType);
    nodeData["op_input_b_type"] = (double)static_cast<int>(m_InputBType);
    nodeData["op_output_type"] = (double)static_cast<int>(m_OutputType);
    nodeData["op_uuid"] = m_OperationUUID;
}

void ParameterOperationBlock::LoadState(Node& node, const crude_json::value& nodeData, Container* container, App* app)
{
    // Load operation state first (before base class, so pins are correct)
    if (nodeData.contains("op_input_a_type"))
        m_InputAType = static_cast<PinType>((int)nodeData["op_input_a_type"].get<double>());
    else
        m_InputAType = PinType::Int;
    
    if (nodeData.contains("op_input_b_type"))
        m_InputBType = static_cast<PinType>((int)nodeData["op_input_b_type"].get<double>());
    else
        m_InputBType = PinType::Int;
    
    if (nodeData.contains("op_output_type"))
        m_OutputType = static_cast<PinType>((int)nodeData["op_output_type"].get<double>());
    else
        m_OutputType = PinType::Int;
    
    if (nodeData.contains("op_uuid"))
        m_OperationUUID = nodeData["op_uuid"].get<crude_json::string>();
    else
        m_OperationUUID = "";
    
    // Rebuild pins with loaded types
    node.Inputs.clear();
    node.Outputs.clear();
    m_InputParams.clear();
    m_OutputParams.clear();
    Build(node, app);
    
    // Call base class to load parameter values
    ParameterizedBlock::LoadState(node, nodeData, container, app);
}

// Register block
REGISTER_BLOCK(ParameterOperationBlock, "ParamOp");

