#define IMGUI_DEFINE_MATH_OPERATORS
#include "parameter_edit_dialog.h"
#include "../app.h"
#include "../types.h"
#include "parameter_node.h"
#include <imgui.h>
#include <imgui_node_editor.h>

namespace ed = ax::NodeEditor;

//------------------------------------------------------------------------------
// Parameter Edit Dialog
//------------------------------------------------------------------------------
static bool s_ParameterEditDialogOpen = false;
static Node* s_EditingParameterNode = nullptr;
static App* s_EditingParameterApp = nullptr;

void OpenParameterEditDialog(Node* node, App* app)
{
    s_EditingParameterNode = node;
    s_EditingParameterApp = app;
    s_ParameterEditDialogOpen = true;
}

void RenderParameterEditDialog()
{
    if (!s_ParameterEditDialogOpen || !s_EditingParameterNode || !s_EditingParameterApp)
        return;

    // Suspend node editor and disable shortcuts to prevent input conflicts
    ed::Suspend();
    ed::EnableShortcuts(false);
    
    // Center modal window
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(400, 0), ImGuiCond_Appearing);

    // Open popup if not already open
    if (!ImGui::IsPopupOpen("Edit Parameter", ImGuiPopupFlags_AnyPopupId))
        ImGui::OpenPopup("Edit Parameter");

    // Use proper modal flags
    bool isOpen = s_ParameterEditDialogOpen;
    if (!ImGui::BeginPopupModal("Edit Parameter", &isOpen, ImGuiWindowFlags_AlwaysAutoResize))
    {
        if (!isOpen)
            s_ParameterEditDialogOpen = false;
        ed::EnableShortcuts(true);
        ed::Resume();
        return;
    }
    
    // Update our state if popup was closed via X button
    if (!isOpen)
    {
        s_ParameterEditDialogOpen = false;
        ImGui::EndPopup();
        ed::EnableShortcuts(true);
        ed::Resume();
        return;
    }

    // Handle ESC to close (only if not typing in an input field)
    if (!ImGui::IsAnyItemActive() && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Escape)))
    {
        s_ParameterEditDialogOpen = false;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        ed::EnableShortcuts(true);
        ed::Resume();
        return;
    }

    if (s_EditingParameterNode->Type != NodeType::Parameter || !s_EditingParameterNode->ParameterInstance)
    {
        ImGui::Text("Error: Not a parameter node");
        if (ImGui::Button("Close"))
        {
            s_ParameterEditDialogOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
        ed::EnableShortcuts(true);
        ed::Resume();
        return;
    }

    auto* paramInstance = s_EditingParameterNode->ParameterInstance;
    
    // Parameter Name
    ImGui::Text("Parameter Name:");
    ImGui::SameLine();
    ImGui::PushItemWidth(200.0f);
    static std::map<int, std::string> nameBuffers;
    int nodeId = s_EditingParameterNode->ID.Get();
    if (nameBuffers.find(nodeId) == nameBuffers.end())
    {
        nameBuffers[nodeId] = s_EditingParameterNode->Name;
    }
    
    char nameBuffer[128];
    strncpy(nameBuffer, nameBuffers[nodeId].c_str(), 127);
    nameBuffer[127] = '\0';
    
    if (ImGui::InputText("##param_name", nameBuffer, 128))
    {
        nameBuffers[nodeId] = nameBuffer;
        s_EditingParameterNode->Name = nameBuffer;
        paramInstance->SetName(nameBuffer);
        
        // Sync to source or shortcuts
        if (paramInstance->IsSource())
            paramInstance->SyncNameToAllShortcuts(*s_EditingParameterNode, s_EditingParameterApp);
        else if (paramInstance->GetSourceID() > 0)
            paramInstance->SyncNameToSource(s_EditingParameterApp);
    }
    ImGui::PopItemWidth();
    
    ImGui::Separator();
    ImGui::Spacing();

    // Parameter Type (editable combo)
    ImGui::Text("Type:");
    ImGui::SameLine();
    ImGui::PushItemWidth(150.0f);
    
    static std::map<int, int> typeIndices; // Per-node type index
    
    const char* typeItems[] = { "Bool", "Int", "Float", "String" };
    PinType typeValues[] = { PinType::Bool, PinType::Int, PinType::Float, PinType::String };
    const int typeCount = 4;
    
    // Initialize type index if needed
    if (typeIndices.find(nodeId) == typeIndices.end())
    {
        // Find current type in the array
        for (int i = 0; i < typeCount; i++)
        {
            if (typeValues[i] == s_EditingParameterNode->ParameterType)
            {
                typeIndices[nodeId] = i;
                break;
            }
        }
    }
    
    int currentTypeIndex = typeIndices[nodeId];
    if (ImGui::Combo("##param_type_combo", &currentTypeIndex, typeItems, typeCount))
    {
        typeIndices[nodeId] = currentTypeIndex;
        PinType newType = typeValues[currentTypeIndex];
        
        // Update parameter type
        s_EditingParameterNode->ParameterType = newType;
        paramInstance->SetType(newType);
        
        // Clear all input pins and rebuild
        s_EditingParameterNode->Inputs.clear();
        s_EditingParameterNode->Outputs.clear();
        paramInstance->Build(*s_EditingParameterNode, s_EditingParameterApp);
        
        // Initialize default value for the new type
        switch (newType)
        {
            case PinType::Bool:
                s_EditingParameterNode->BoolValue = false;
                paramInstance->SetBool(false);
                break;
            case PinType::Int:
                s_EditingParameterNode->IntValue = 0;
                paramInstance->SetInt(0);
                break;
            case PinType::Float:
                s_EditingParameterNode->FloatValue = 0.0f;
                paramInstance->SetFloat(0.0f);
                break;
            case PinType::String:
                s_EditingParameterNode->StringValue = "";
                paramInstance->SetString("");
                break;
            default:
                break;
        }
    }
    ImGui::PopItemWidth();
    
    ImGui::Separator();
    ImGui::Spacing();

    // Value Editor
    ImGui::Text("Value:");
    ImGui::PushItemWidth(200.0f);
    
    bool valueChanged = false;
    switch (s_EditingParameterNode->ParameterType)
    {
        case PinType::Bool:
            if (ImGui::Checkbox("##value", &s_EditingParameterNode->BoolValue))
            {
                paramInstance->SetBool(s_EditingParameterNode->BoolValue);
                valueChanged = true;
            }
            break;
            
        case PinType::Int:
            if (ImGui::DragInt("##value", &s_EditingParameterNode->IntValue, 1.0f))
            {
                paramInstance->SetInt(s_EditingParameterNode->IntValue);
                valueChanged = true;
            }
            break;
            
        case PinType::Float:
            if (ImGui::DragFloat("##value", &s_EditingParameterNode->FloatValue, 0.01f))
            {
                paramInstance->SetFloat(s_EditingParameterNode->FloatValue);
                valueChanged = true;
            }
            break;
            
        case PinType::String:
        {
            static std::map<int, std::string> stringBuffers;
            if (stringBuffers.find(nodeId) == stringBuffers.end())
            {
                stringBuffers[nodeId] = s_EditingParameterNode->StringValue;
            }
            
            char strBuffer[256];
            strncpy(strBuffer, stringBuffers[nodeId].c_str(), 255);
            strBuffer[255] = '\0';
            
            if (ImGui::InputText("##value", strBuffer, 256))
            {
                stringBuffers[nodeId] = strBuffer;
                s_EditingParameterNode->StringValue = strBuffer;
                paramInstance->SetString(strBuffer);
                valueChanged = true;
            }
            break;
        }
        default:
            ImGui::Text("Unknown type");
            break;
    }
    
    // Sync value if changed
    if (valueChanged)
    {
        if (paramInstance->IsSource())
            paramInstance->SyncValueToAllShortcuts(*s_EditingParameterNode, s_EditingParameterApp);
        else if (paramInstance->GetSourceID() > 0)
            paramInstance->SyncValueToSource(s_EditingParameterApp);
    }
    
    ImGui::PopItemWidth();
    
    ImGui::Separator();
    ImGui::Spacing();

    // Display Mode
    ImGui::Text("Display Mode:");
    auto currentMode = paramInstance->GetDisplayMode();
    const char* modeNames[] = {"Name Only", "Name + Value", "Small Box", "Minimal", "Minimal Links"};
    int currentModeIndex = static_cast<int>(currentMode);
    
    ImGui::PushItemWidth(200.0f);
    if (ImGui::Combo("##display_mode", &currentModeIndex, modeNames, 5))
    {
        paramInstance->SetDisplayMode(static_cast<ParameterDisplayMode>(currentModeIndex));
        // Notify editor that display mode changed
        ed::NotifyBlockDisplayModeChanged(s_EditingParameterNode->ID);
    }
    ImGui::PopItemWidth();
    
    ImGui::Separator();
    ImGui::Spacing();

    // Source/Shortcut Settings
    ImGui::Text("Source/Shortcut:");
    
    // Only allow marking as source if not already a shortcut
    if (paramInstance->GetSourceID() == 0)
    {
        bool isSource = paramInstance->IsSource();
        if (ImGui::Checkbox("Mark as Source", &isSource))
        {
            paramInstance->SetIsSource(isSource);
            if (!isSource)
            {
                paramInstance->SetSourceID(0);
            }
        }
        
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Source parameters can have shortcuts created from them");
    }
    else
    {
        // This is a shortcut - show source info
        ImGui::TextDisabled("This is a shortcut to source: %d", paramInstance->GetSourceID());
        if (ImGui::Button("Clear Shortcut Reference"))
        {
            paramInstance->SetSourceID(0);
            paramInstance->SetIsSource(false);
        }
    }

    ImGui::Separator();
    ImGui::Spacing();

    // Buttons
    bool confirmPressed = false;
    if (ImGui::Button("Confirm (Enter)", ImVec2(120, 0)))
    {
        confirmPressed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel (Esc)", ImVec2(120, 0)))
    {
        s_ParameterEditDialogOpen = false;
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
        auto* activeContainer = s_EditingParameterApp->GetActiveRootContainer();
        if (activeContainer)
        {
            s_EditingParameterApp->SaveGraph(s_EditingParameterApp->m_GraphFilename, activeContainer);
        }
        s_ParameterEditDialogOpen = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
    
    // Resume node editor and re-enable shortcuts
    ed::EnableShortcuts(true);
    ed::Resume();
}

bool IsParameterEditDialogOpen()
{
    return s_ParameterEditDialogOpen;
}

