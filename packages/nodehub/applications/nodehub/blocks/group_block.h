#pragma once
#include "block.h"
#include <vector>
#include <string>

// Group display modes
enum class GroupDisplayMode
{
    Expanded,    // Full expanded view (default)
    Collapsed  // Compact collapsed view
};

// Pin definition for variable I/O
struct GroupPinDef
{
    std::string Name;
    PinType Type;
    PinKind Kind;  // Input or Output
    int PinId;     // Stable pin ID (preserved across rebuilds)
    
    GroupPinDef() : Name(""), Type(PinType::Flow), Kind(PinKind::Input), PinId(-1) {}
    GroupPinDef(const std::string& name, PinType type, PinKind kind, int pinId = -1)
        : Name(name), Type(type), Kind(kind), PinId(pinId) {}
};

// Group block - inherits from ParameterizedBlock
class GroupBlock : public ParameterizedBlock
{
public:
    GroupBlock(int id) : ParameterizedBlock(id, "Group") 
    {
        SetTypeName("Group");
        m_Type = NodeType::Group;
        m_Color = ImColor(200, 150, 200);  // Purple-ish color for groups
        SetFlags(static_cast<NH_BEHAVIOR_FLAGS>(
            NHBEHAVIOR_SCRIPT | NHBEHAVIOR_VARIABLEINPUTS | NHBEHAVIOR_VARIABLEOUTPUTS | 
            NHBEHAVIOR_VARIABLEPARAMETERINPUTS | NHBEHAVIOR_VARIABLEPARAMETEROUTPUTS));
        m_DisplayMode = GroupDisplayMode::Expanded;  // Default to expanded
        m_CollapsedSize = ImVec2(150.0f, 80.0f);      // Default collapsed size
    }
    
    void Build(Node& node, App* app) override;
    int Run(Node& node, App* app) override;
    NH_CSTRING GetBlockType() const override { return "Group"; }
    
    // Rendering (uses base class rendering like any other block)
    void Render(Node& node, App* app, Pin* newLinkPin) override;
    
    // Context menu for adding/removing pins
    void OnMenu(Node& node, App* app) override;
    
    // Display mode management (kept for future use)
    GroupDisplayMode GetDisplayMode() const { return m_DisplayMode; }
    void SetDisplayMode(GroupDisplayMode mode) { m_DisplayMode = mode; }
    void ToggleDisplayMode();
    
    // Save/load pin definitions
    void SaveState(Node& node, crude_json::value& nodeData, const Container* container, App* app) override;
    void LoadState(Node& node, const crude_json::value& nodeData, Container* container, App* app) override;
    
    // Update pin name by ID (called from edit dialog)
    void UpdatePinDefName(int pinId, const std::string& newName);
    // Update pin type by ID (called from edit dialog)
    void UpdatePinDefType(int pinId, PinType newType);
    
    // Public methods for adding flow pins (used by keyboard shortcuts)
    void AddFlowInput(const std::string& name);
    void AddFlowOutput(const std::string& name);
    
    // Public method for removing pins (used by edit dialog)
    void RemovePinDef(size_t index, PinKind kind);
    
private:
    std::vector<GroupPinDef> m_PinDefinitions;  // Store pin definitions for save/load
    GroupDisplayMode m_DisplayMode;              // Current display mode
    ImVec2 m_CollapsedSize;                      // Size when collapsed (resizable)
    
    void AddPinDef(const std::string& name, PinType type, PinKind kind);
    void RebuildPins(Node& node, App* app);  // Helper to rebuild pins and update Node pointers
    
    // Rendering methods for different display modes
    void RenderExpanded(Node& node, App* app, Pin* newLinkPin);
    void RenderCollapsed(Node& node, App* app, Pin* newLinkPin);
};

