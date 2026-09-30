#define IMGUI_DEFINE_MATH_OPERATORS
#include "block_edit_dialog.h"
#include "../app.h"
#include "../types.h"
#include "../blocks/parameter_node.h"
#include "../blocks/group_block.h"
#include "../blocks/parameter_operation.h"
#include <imgui.h>
#include <imgui_node_editor.h>

namespace ed = ax::NodeEditor;

//------------------------------------------------------------------------------
// Block Edit Dialog
//------------------------------------------------------------------------------
static bool s_BlockEditDialogOpen = false;
static Node* s_EditingNode = nullptr;
static App* s_EditingApp = nullptr;

void OpenBlockEditDialog(Node* node, App* app)
{
    s_EditingNode = node;
    s_EditingApp = app;
    s_BlockEditDialogOpen = true;
    // Note: OpenPopup must be called in the same frame as BeginPopupModal
    // It will be handled in RenderBlockEditDialog when the context menu closes
}

void RenderBlockEditDialog()
{
    if (!s_BlockEditDialogOpen || !s_EditingNode || !s_EditingApp)
        return;

    // Suspend node editor and disable shortcuts to prevent input conflicts
    ed::Suspend();
    ed::EnableShortcuts(false);
    
    // Center modal window
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(400, 0), ImGuiCond_Appearing);

    // Open popup if not already open (first frame after OpenBlockEditDialog)
    if (!ImGui::IsPopupOpen("Edit Block Parameters", ImGuiPopupFlags_AnyPopupId))
        ImGui::OpenPopup("Edit Block Parameters");

    // Use proper modal flags to prevent conflicts
    bool isOpen = s_BlockEditDialogOpen;
    if (!ImGui::BeginPopupModal("Edit Block Parameters", &isOpen, 
                                 ImGuiWindowFlags_AlwaysAutoResize))
    {
        // Popup was closed externally
        if (!isOpen)
            s_BlockEditDialogOpen = false;
        ed::EnableShortcuts(true);
        ed::Resume();
        return;
    }
    
    // Update our state if popup was closed via X button
    if (!isOpen)
    {
        s_BlockEditDialogOpen = false;
        ImGui::EndPopup();
        ed::EnableShortcuts(true);
        ed::Resume();
        return;
    }

    // Handle ESC to close (only if not typing in an input field)
    if (!ImGui::IsAnyItemActive() && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Escape)))
    {
        s_BlockEditDialogOpen = false;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        ed::EnableShortcuts(true);
        ed::Resume();
        return;
    }

    if (!s_EditingNode->IsBlockBased() || !s_EditingNode->BlockInstance)
    {
        ImGui::Text("Error: Not a block-based node");
        if (ImGui::Button("Close"))
        {
            s_BlockEditDialogOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
        ed::EnableShortcuts(true);
        ed::Resume();
        return;
    }

    // Block name (editable if NHBEHAVIOR_SCRIPT flag is set)
    bool canEditName = false;
    if (s_EditingNode->IsBlockBased() && s_EditingNode->BlockInstance)
    {
        const NH_BEHAVIOR_FLAGS flags = s_EditingNode->BlockInstance->GetFlags();
        canEditName = (flags & NHBEHAVIOR_SCRIPT) != 0;
    }
    
    if (canEditName)
    {
        ImGui::Text("Block Name:");
        ImGui::SameLine();
        ImGui::PushItemWidth(200.0f);
        static std::map<int, std::string> nodeNameBuffers; // Per-node name buffers
        int nodeId = ToRuntimeId(s_EditingNode->ID);
        if (nodeNameBuffers.find(nodeId) == nodeNameBuffers.end())
        {
            nodeNameBuffers[nodeId] = s_EditingNode->Name;
        }
        
        char nameBuffer[128];
        strncpy(nameBuffer, nodeNameBuffers[nodeId].c_str(), 127);
        nameBuffer[127] = '\0';
        
        if (ImGui::InputText("##block_name", nameBuffer, 128))
        {
            nodeNameBuffers[nodeId] = nameBuffer;
            s_EditingNode->Name = nameBuffer;
        }
        ImGui::PopItemWidth();
    }
    else
    {
        ImGui::Text("Block: %s", s_EditingNode->Name.c_str());
    }
    
    ImGui::Separator();
    ImGui::Spacing();

    // Flow Inputs section (for Group blocks with variable I/O)
    bool hasFlowInputs = false;
    for (const auto& pin : s_EditingNode->Inputs)
    {
        if (pin.Type == PinType::Flow)
        {
            hasFlowInputs = true;
            break;
        }
    }

    if (hasFlowInputs && s_EditingNode->BlockType == "Group")
    {
        ImGui::Text("Flow Inputs:");
        ImGui::Spacing();

        size_t flowInputIndex = 0;
        bool needsRebuild = false;
        for (auto& pin : s_EditingNode->Inputs)
        {
            if (pin.Type != PinType::Flow)
                continue;

            ImGui::PushID(pin.ID.AsPointer());
            
            // Editable flow input name
            ImGui::PushItemWidth(150.0f);
            static std::map<int, std::string> flowInputNameBuffers;
            const int pinId = ToRuntimeId(pin.ID);
            if (flowInputNameBuffers.find(pinId) == flowInputNameBuffers.end())
            {
                flowInputNameBuffers[pinId] = pin.Name;
            }
            
            char nameBuffer[64];
            strncpy(nameBuffer, flowInputNameBuffers[pinId].c_str(), 63);
            nameBuffer[63] = '\0';
            
            if (ImGui::InputText("##flow_input_name", nameBuffer, 64))
            {
                flowInputNameBuffers[pinId] = nameBuffer;
                pin.Name = nameBuffer;
                
                // Update GroupBlock pin definition
                auto* groupBlock = dynamic_cast<GroupBlock*>(s_EditingNode->BlockInstance);
                if (groupBlock)
                {
                    groupBlock->UpdatePinDefName(pinId, nameBuffer);
                }
            }
            ImGui::PopItemWidth();
            ImGui::SameLine();
            ImGui::TextDisabled("(Flow)");
            ImGui::SameLine();
            
            // Delete button
            if (ImGui::Button("Delete"))
            {
                auto* groupBlock = dynamic_cast<GroupBlock*>(s_EditingNode->BlockInstance);
                if (groupBlock)
                {
                    groupBlock->RemovePinDef(flowInputIndex, PinKind::Input);
                    needsRebuild = true;
                }
            }

            ImGui::PopID();
            ImGui::Spacing();
            flowInputIndex++;
        }
        
        // Rebuild if any pin was deleted
        if (needsRebuild)
        {
            auto* groupBlock = dynamic_cast<GroupBlock*>(s_EditingNode->BlockInstance);
            if (groupBlock)
            {
                s_EditingNode->Inputs.clear();
                s_EditingNode->Outputs.clear();
                groupBlock->Build(*s_EditingNode, s_EditingApp);
            }
        }
        
        ImGui::Separator();
        ImGui::Spacing();
    }

    // Get input parameters (non-flow pins)
    bool hasInputParams = false;
    for (const auto& pin : s_EditingNode->Inputs)
    {
        if (pin.Type != PinType::Flow)
        {
            hasInputParams = true;
            break;
        }
    }

    if (!hasInputParams)
    {
        ImGui::Text("No input parameters");
    }
    else
    {
        ImGui::Text("Input Parameters:");
        ImGui::Spacing();

        size_t paramInputIndex = 0;
        bool needsParamRebuild = false;
        for (auto& pin : s_EditingNode->Inputs)
        {
            if (pin.Type == PinType::Flow)
                continue;

            ImGui::PushID(pin.ID.AsPointer());
            
            // Editable parameter name
            ImGui::PushItemWidth(150.0f);
            static std::map<int, std::string> nameBuffers; // Per-pin name buffers
            const int pinId = ToRuntimeId(pin.ID);
            if (nameBuffers.find(pinId) == nameBuffers.end())
            {
                nameBuffers[pinId] = pin.Name;
            }
            
            char nameBuffer[64];
            strncpy(nameBuffer, nameBuffers[pinId].c_str(), 63);
            nameBuffer[63] = '\0';
            
            if (ImGui::InputText("##param_name", nameBuffer, 64))
            {
                nameBuffers[pinId] = nameBuffer;
                pin.Name = nameBuffer;
                
                // Update GroupBlock pin definition if this is a Group block
                if (s_EditingNode->BlockType == "Group")
                {
                    auto* groupBlock = dynamic_cast<GroupBlock*>(s_EditingNode->BlockInstance);
                    if (groupBlock)
                    {
                        groupBlock->UpdatePinDefName(pinId, nameBuffer);
                    }
                }
            }
            ImGui::PopItemWidth();
            ImGui::SameLine();
            
            // Editable parameter type (combo)
            ImGui::PushItemWidth(100.0f);
            static std::map<int, int> typeIndices; // Per-pin type index buffers
            
            const char* typeItems[] = { "Bool", "Int", "Float", "String", "Object", "Function", "Delegate" };
            PinType typeValues[] = { PinType::Bool, PinType::Int, PinType::Float, PinType::String, PinType::Object, PinType::Function, PinType::Delegate };
            const int typeCount = 7;
            
            // Initialize type index if needed
            if (typeIndices.find(pinId) == typeIndices.end())
            {
                // Find current type in the array
                for (int i = 0; i < typeCount; i++)
                {
                    if (typeValues[i] == pin.Type)
                    {
                        typeIndices[pinId] = i;
                        break;
                    }
                }
            }
            
            int currentTypeIndex = typeIndices[pinId];
            if (ImGui::Combo("##param_type", &currentTypeIndex, typeItems, typeCount))
            {
                typeIndices[pinId] = currentTypeIndex;
                PinType newType = typeValues[currentTypeIndex];
                
                // Update pin type
                pin.Type = newType;
                
                // Update GroupBlock pin definition if this is a Group block
                if (s_EditingNode->BlockType == "Group")
                {
                    auto* groupBlock = dynamic_cast<GroupBlock*>(s_EditingNode->BlockInstance);
                    if (groupBlock)
                    {
                        groupBlock->UpdatePinDefType(pinId, newType);
                        // Clear and rebuild node after type change (this will disconnect incompatible links)
                        s_EditingNode->Inputs.clear();
                        s_EditingNode->Outputs.clear();
                        groupBlock->Build(*s_EditingNode, s_EditingApp);
                    }
                }
                // Update ParamOp input types
                else if (s_EditingNode->BlockType == "ParamOp")
                {
                    auto* paramOpBlock = dynamic_cast<ParameterOperationBlock*>(s_EditingNode->BlockInstance);
                    if (paramOpBlock)
                    {
                        // Determine which input this is (A or B)
                        if (paramInputIndex == 0)
                            paramOpBlock->SetInputAType(newType);
                        else if (paramInputIndex == 1)
                            paramOpBlock->SetInputBType(newType);
                        
                        // Clear operation when types change
                        paramOpBlock->SetOperation("");
                        
                        // Rebuild node
                        s_EditingNode->Inputs.clear();
                        s_EditingNode->Outputs.clear();
                        paramOpBlock->Build(*s_EditingNode, s_EditingApp);
                    }
                }
            }
            ImGui::PopItemWidth();
            
            // Delete button for Group block parameters
            if (s_EditingNode->BlockType == "Group")
            {
                ImGui::SameLine();
                if (ImGui::Button("Delete"))
                {
                    auto* groupBlock = dynamic_cast<GroupBlock*>(s_EditingNode->BlockInstance);
                    if (groupBlock)
                    {
                        groupBlock->RemovePinDef(paramInputIndex, PinKind::Input);
                        needsParamRebuild = true;
                    }
                }
            }
            
            ImGui::SameLine();

            // Check if linked to a parameter node
            auto* link = s_EditingApp->FindLinkConnectedToPin(pin.ID);
            bool isLinked = (link != nullptr && link->EndPinID == pin.ID);
            
            if (isLinked)
            {
                // Get the source pin (where data flows FROM)
                auto* sourcePin = s_EditingApp->FindPin(link->StartPinID);
                if (sourcePin && sourcePin->Node && sourcePin->Node->Type == NodeType::Parameter)
                {
                    // Connected to a parameter node
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 1.0f, 0.5f, 1.0f));
                    ImGui::Text("[External Parameter]");
                    ImGui::PopStyleColor();
                    
                    // Show parameter node name and editable value
                    ImGui::Indent();
                    ImGui::TextDisabled("Source: %s", sourcePin->Node->Name.c_str());
                    ImGui::Spacing();
                    
                    // Editable value field
                    ImGui::Text("Value:");
                    ImGui::SameLine();
                    ImGui::PushItemWidth(150.0f);
                    
                    Node* paramNode = sourcePin->Node;
                    bool valueChanged = false;
                    
                    switch (paramNode->ParameterType)
                    {
                        case PinType::Bool:
                            valueChanged = ImGui::Checkbox("##edit_value", &paramNode->BoolValue);
                            if (valueChanged && paramNode->ParameterInstance)
                                paramNode->ParameterInstance->SetBool(paramNode->BoolValue);
                            break;
                        case PinType::Int:
                            valueChanged = ImGui::InputInt("##edit_value", &paramNode->IntValue);
                            if (valueChanged && paramNode->ParameterInstance)
                                paramNode->ParameterInstance->SetInt(paramNode->IntValue);
                            break;
                        case PinType::Float:
                            valueChanged = ImGui::InputFloat("##edit_value", &paramNode->FloatValue, 0.01f, 1.0f, "%.3f");
                            if (valueChanged && paramNode->ParameterInstance)
                                paramNode->ParameterInstance->SetFloat(paramNode->FloatValue);
                            break;
                        case PinType::String:
                            {
                                static std::map<int, std::string> stringBuffers; // Per-pin buffers
                                const int pinId = ToRuntimeId(pin.ID);
                                if (stringBuffers.find(pinId) == stringBuffers.end())
                                {
                                    stringBuffers[pinId] = paramNode->StringValue;
                                }
                                
                                char strBuffer[256];
                                strncpy(strBuffer, stringBuffers[pinId].c_str(), 255);
                                strBuffer[255] = '\0';
                                
                                if (ImGui::InputText("##edit_value", strBuffer, 256))
                                {
                                    stringBuffers[pinId] = strBuffer;
                                    paramNode->StringValue = strBuffer;
                                    valueChanged = true;
                                    if (paramNode->ParameterInstance)
                                        paramNode->ParameterInstance->SetString(paramNode->StringValue);
                                }
                            }
                            break;
                        default:
                            break;
                    }
                    
                    ImGui::PopItemWidth();
                    ImGui::Unindent();
                }
                else
                {
                    ImGui::TextDisabled("[Linked]");
                }
            }
            else
            {
                ImGui::TextDisabled("[Not connected]");
                ImGui::Spacing();
                
                // Editable value field for unconnected parameters
                ImGui::Indent();
                ImGui::Text("Default Value:");
                ImGui::SameLine();
                ImGui::PushItemWidth(150.0f);
                
                // Get or create default value
                const int pinId = ToRuntimeId(pin.ID);
                auto& paramValues = s_EditingNode->UnconnectedParamValues;
                if (paramValues.find(pinId) == paramValues.end())
                {
                    // Initialize with default based on type
                    switch (pin.Type)
                    {
                        case PinType::Bool:   paramValues[pinId] = "false"; break;
                        case PinType::Int:    paramValues[pinId] = "0"; break;
                        case PinType::Float:  paramValues[pinId] = "0.0"; break;
                        case PinType::String: paramValues[pinId] = ""; break;
                        default: paramValues[pinId] = "0"; break;
                    }
                }
                
                bool valueChanged = false;
                std::string& valueStr = paramValues[pinId];
                
                switch (pin.Type)
                {
                    case PinType::Bool:
                        {
                            bool boolVal = (valueStr == "true" || valueStr == "1");
                            if (ImGui::Checkbox("##default_value", &boolVal))
                            {
                                valueStr = boolVal ? "true" : "false";
                                valueChanged = true;
                            }
                        }
                        break;
                    case PinType::Int:
                        {
                            int intVal = 0;
                            try { intVal = std::stoi(valueStr); } catch (...) {}
                            if (ImGui::InputInt("##default_value", &intVal))
                            {
                                valueStr = std::to_string(intVal);
                                valueChanged = true;
                            }
                        }
                        break;
                    case PinType::Float:
                        {
                            float floatVal = 0.0f;
                            try { floatVal = std::stof(valueStr); } catch (...) {}
                            if (ImGui::InputFloat("##default_value", &floatVal, 0.01f, 1.0f, "%.3f"))
                            {
                                char buf[32];
                                snprintf(buf, sizeof(buf), "%.6g", floatVal);
                                valueStr = buf;
                                valueChanged = true;
                            }
                        }
                        break;
                    case PinType::String:
                        {
                            static std::map<int, std::string> stringBuffers;
                            if (stringBuffers.find(pinId) == stringBuffers.end())
                            {
                                stringBuffers[pinId] = valueStr;
                            }
                            
                            char strBuffer[256];
                            strncpy(strBuffer, stringBuffers[pinId].c_str(), 255);
                            strBuffer[255] = '\0';
                            
                            if (ImGui::InputText("##default_value", strBuffer, 256))
                            {
                                stringBuffers[pinId] = strBuffer;
                                valueStr = strBuffer;
                                valueChanged = true;
                            }
                        }
                        break;
                    default:
                        break;
                }
                
                ImGui::PopItemWidth();
                ImGui::Unindent();
            }

            ImGui::PopID();
            ImGui::Spacing();
            paramInputIndex++;
        }
        
        // Rebuild if any parameter was deleted
        if (needsParamRebuild)
        {
            auto* groupBlock = dynamic_cast<GroupBlock*>(s_EditingNode->BlockInstance);
            if (groupBlock)
            {
                s_EditingNode->Inputs.clear();
                s_EditingNode->Outputs.clear();
                groupBlock->Build(*s_EditingNode, s_EditingApp);
            }
        }
    }

    ImGui::Separator();
    ImGui::Spacing();

    // Flow Outputs section (for Group blocks with variable I/O)
    bool hasFlowOutputs = false;
    for (const auto& pin : s_EditingNode->Outputs)
    {
        if (pin.Type == PinType::Flow)
        {
            hasFlowOutputs = true;
            break;
        }
    }

    if (hasFlowOutputs && s_EditingNode->BlockType == "Group")
    {
        ImGui::Text("Flow Outputs:");
        ImGui::Spacing();

        size_t flowOutputIndex = 0;
        bool needsFlowOutputRebuild = false;
        for (auto& pin : s_EditingNode->Outputs)
        {
            if (pin.Type != PinType::Flow)
                continue;

            ImGui::PushID(pin.ID.AsPointer());
            
            // Editable flow output name
            ImGui::PushItemWidth(150.0f);
            static std::map<int, std::string> flowOutputNameBuffers;
            const int pinId = ToRuntimeId(pin.ID);
            if (flowOutputNameBuffers.find(pinId) == flowOutputNameBuffers.end())
            {
                flowOutputNameBuffers[pinId] = pin.Name;
            }
            
            char nameBuffer[64];
            strncpy(nameBuffer, flowOutputNameBuffers[pinId].c_str(), 63);
            nameBuffer[63] = '\0';
            
            if (ImGui::InputText("##flow_output_name", nameBuffer, 64))
            {
                flowOutputNameBuffers[pinId] = nameBuffer;
                pin.Name = nameBuffer;
                
                // Update GroupBlock pin definition
                auto* groupBlock = dynamic_cast<GroupBlock*>(s_EditingNode->BlockInstance);
                if (groupBlock)
                {
                    groupBlock->UpdatePinDefName(pinId, nameBuffer);
                }
            }
            ImGui::PopItemWidth();
            ImGui::SameLine();
            ImGui::TextDisabled("(Flow)");
            ImGui::SameLine();
            
            // Delete button
            if (ImGui::Button("Delete"))
            {
                auto* groupBlock = dynamic_cast<GroupBlock*>(s_EditingNode->BlockInstance);
                if (groupBlock)
                {
                    groupBlock->RemovePinDef(flowOutputIndex, PinKind::Output);
                    needsFlowOutputRebuild = true;
                }
            }

            ImGui::PopID();
            ImGui::Spacing();
            flowOutputIndex++;
        }
        
        // Rebuild if any pin was deleted
        if (needsFlowOutputRebuild)
        {
            auto* groupBlock = dynamic_cast<GroupBlock*>(s_EditingNode->BlockInstance);
            if (groupBlock)
            {
                s_EditingNode->Inputs.clear();
                s_EditingNode->Outputs.clear();
                groupBlock->Build(*s_EditingNode, s_EditingApp);
            }
        }
        
        ImGui::Separator();
        ImGui::Spacing();
    }

    // Output Parameters section
    bool hasOutputParams = false;
    for (const auto& pin : s_EditingNode->Outputs)
    {
        if (pin.Type != PinType::Flow)
        {
            hasOutputParams = true;
            break;
        }
    }

    if (!hasOutputParams)
    {
        ImGui::Text("No output parameters");
    }
    else
    {
        ImGui::Text("Output Parameters:");
        ImGui::Spacing();

        size_t paramOutputIndex = 0;
        bool needsParamOutputRebuild = false;
        for (auto& pin : s_EditingNode->Outputs)
        {
            if (pin.Type == PinType::Flow)
                continue;

            ImGui::PushID(pin.ID.AsPointer());
            
            // Editable parameter name
            ImGui::PushItemWidth(150.0f);
            static std::map<int, std::string> outputNameBuffers; // Per-pin name buffers for outputs
            const int pinId = ToRuntimeId(pin.ID);
            if (outputNameBuffers.find(pinId) == outputNameBuffers.end())
            {
                outputNameBuffers[pinId] = pin.Name;
            }
            
            char nameBuffer[64];
            strncpy(nameBuffer, outputNameBuffers[pinId].c_str(), 63);
            nameBuffer[63] = '\0';
            
            if (ImGui::InputText("##output_param_name", nameBuffer, 64))
            {
                outputNameBuffers[pinId] = nameBuffer;
                pin.Name = nameBuffer;
                
                // Update GroupBlock pin definition if this is a Group block
                if (s_EditingNode->BlockType == "Group")
                {
                    auto* groupBlock = dynamic_cast<GroupBlock*>(s_EditingNode->BlockInstance);
                    if (groupBlock)
                    {
                        groupBlock->UpdatePinDefName(pinId, nameBuffer);
                    }
                }
            }
            ImGui::PopItemWidth();
            ImGui::SameLine();
            
            // Editable parameter type (combo) for outputs
            ImGui::PushItemWidth(100.0f);
            static std::map<int, int> outputTypeIndices; // Per-pin type index buffers for outputs
            const int outputPinId = ToRuntimeId(pin.ID);
            
            const char* outputTypeItems[] = { "Bool", "Int", "Float", "String", "Object", "Function", "Delegate" };
            PinType outputTypeValues[] = { PinType::Bool, PinType::Int, PinType::Float, PinType::String, PinType::Object, PinType::Function, PinType::Delegate };
            const int outputTypeCount = 7;
            
            // Initialize type index if needed
            if (outputTypeIndices.find(outputPinId) == outputTypeIndices.end())
            {
                // Find current type in the array
                for (int i = 0; i < outputTypeCount; i++)
                {
                    if (outputTypeValues[i] == pin.Type)
                    {
                        outputTypeIndices[outputPinId] = i;
                        break;
                    }
                }
            }
            
            int currentOutputTypeIndex = outputTypeIndices[outputPinId];
            if (ImGui::Combo("##output_param_type", &currentOutputTypeIndex, outputTypeItems, outputTypeCount))
            {
                outputTypeIndices[outputPinId] = currentOutputTypeIndex;
                PinType newType = outputTypeValues[currentOutputTypeIndex];
                
                // Update pin type
                pin.Type = newType;
                
                // Update GroupBlock pin definition if this is a Group block
                if (s_EditingNode->BlockType == "Group")
                {
                    auto* groupBlock = dynamic_cast<GroupBlock*>(s_EditingNode->BlockInstance);
                    if (groupBlock)
                    {
                        groupBlock->UpdatePinDefType(outputPinId, newType);
                        // Clear and rebuild node after type change (this will disconnect incompatible links)
                        s_EditingNode->Inputs.clear();
                        s_EditingNode->Outputs.clear();
                        groupBlock->Build(*s_EditingNode, s_EditingApp);
                    }
                }
                // Update ParamOp output type
                else if (s_EditingNode->BlockType == "ParamOp")
                {
                    auto* paramOpBlock = dynamic_cast<ParameterOperationBlock*>(s_EditingNode->BlockInstance);
                    if (paramOpBlock)
                    {
                        // Update output type (ParamOp only has 1 output parameter)
                        paramOpBlock->SetOutputType(newType);
                        
                        // Clear operation when types change
                        paramOpBlock->SetOperation("");
                        
                        // Rebuild node
                        s_EditingNode->Inputs.clear();
                        s_EditingNode->Outputs.clear();
                        paramOpBlock->Build(*s_EditingNode, s_EditingApp);
                    }
                }
            }
            ImGui::PopItemWidth();
            
            // Delete button for Group block output parameters
            if (s_EditingNode->BlockType == "Group")
            {
                ImGui::SameLine();
                if (ImGui::Button("Delete"))
                {
                    auto* groupBlock = dynamic_cast<GroupBlock*>(s_EditingNode->BlockInstance);
                    if (groupBlock)
                    {
                        groupBlock->RemovePinDef(paramOutputIndex, PinKind::Output);
                        needsParamOutputRebuild = true;
                    }
                }
            }

            ImGui::PopID();
            ImGui::Spacing();
            paramOutputIndex++;
        }
        
        // Rebuild if any output parameter was deleted
        if (needsParamOutputRebuild)
        {
            auto* groupBlock = dynamic_cast<GroupBlock*>(s_EditingNode->BlockInstance);
            if (groupBlock)
            {
                s_EditingNode->Inputs.clear();
                s_EditingNode->Outputs.clear();
                groupBlock->Build(*s_EditingNode, s_EditingApp);
            }
        }
    }

    ImGui::Separator();
    ImGui::Spacing();

    // Parameter Operation specific settings
    if (s_EditingNode->BlockType == "ParamOp")
    {
        auto* paramOpBlock = dynamic_cast<ParameterOperationBlock*>(s_EditingNode->BlockInstance);
        if (paramOpBlock)
        {
            ImGui::Text("Operation Selection:");
            ImGui::Spacing();
            
            // Get matching operations based on current types
            auto matchingOps = ParameterOperationRegistry::Instance().GetMatchingOperations(
                paramOpBlock->GetInputAType(),
                paramOpBlock->GetInputBType(),
                paramOpBlock->GetOutputType());
            
            if (!matchingOps.empty())
            {
                // Create list of operation labels
                std::vector<const char*> opLabels;
                std::vector<std::string> opUUIDs;
                int currentOpIndex = -1;
                
                for (size_t i = 0; i < matchingOps.size(); ++i)
                {
                    opLabels.push_back(matchingOps[i].Label.c_str());
                    opUUIDs.push_back(matchingOps[i].UUID);
                    
                    if (matchingOps[i].UUID == paramOpBlock->GetOperationUUID())
                        currentOpIndex = (int)i;
                }
                
                ImGui::PushItemWidth(200.0f);
                if (ImGui::Combo("##operation_select", &currentOpIndex, opLabels.data(), (int)opLabels.size()))
                {
                    if (currentOpIndex >= 0 && currentOpIndex < (int)opUUIDs.size())
                    {
                        paramOpBlock->SetOperation(opUUIDs[currentOpIndex]);
                        // Update node name to reflect operation
                        s_EditingNode->Name = matchingOps[currentOpIndex].Label;
                    }
                }
                ImGui::PopItemWidth();
                
                // Show current operation
                if (currentOpIndex >= 0 && currentOpIndex < (int)matchingOps.size())
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled("(Selected)");
                }
            }
            else
            {
                ImGui::TextDisabled("No operations available for current types");
                ImGui::TextDisabled("(Change input/output types to see operations)");
            }
            
            ImGui::Separator();
            ImGui::Spacing();
        }
    }

    // Block-specific custom UI (virtual function callback)
    if (s_EditingNode->IsBlockBased() && s_EditingNode->BlockInstance)
    {
        s_EditingNode->BlockInstance->RenderEditUI(*s_EditingNode, s_EditingApp);
    }

    // Buttons
    bool confirmPressed = false;
    if (ImGui::Button("Confirm (Enter)", ImVec2(120, 0)))
    {
        confirmPressed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel (Esc)", ImVec2(120, 0)))
    {
        s_BlockEditDialogOpen = false;
        ImGui::CloseCurrentPopup();
    }

    // Handle Enter key to confirm (only if not typing in an input field)
    if (!ImGui::IsAnyItemActive() && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Enter)))
    {
        confirmPressed = true;
    }

    if (confirmPressed)
    {
        // Save graph
        auto* activeContainer = s_EditingApp->GetActiveRootContainer();
        if (activeContainer)
        {
            s_EditingApp->SaveGraph(s_EditingApp->m_GraphFilename, activeContainer);
        }
        s_BlockEditDialogOpen = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
    
    // Resume node editor and re-enable shortcuts after dialog is done
    ed::EnableShortcuts(true);
    ed::Resume();
}

bool IsBlockEditDialogOpen()
{
    return s_BlockEditDialogOpen;
}

