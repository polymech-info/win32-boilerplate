#pragma once
#include "../commons.h"
#include "../types.h"
#include "../utilities/node_renderer_base.h"
#include "../core/Object.h"
#include <string>
#include <vector>

// Forward declarations
class App;
class Container;
namespace crude_json { struct value; }

// Parameter display modes
enum class ParameterDisplayMode
{
    NameOnly,       // Just the name (compact)
    NameAndValue,   // Name + editable value (default)
    SmallBox,       // Tiny colored rectangle with value
    Minimal,        // Just a rectangle, no name, no pins, no links
    MinimalLinks    // Minimal rectangle with pins and links (but no name)
};

// Base parameter node class
class ParameterNode : public NH_Object, public ax::NodeRendering::NodeRendererBase
{
public:
    ParameterNode(int id, NH_CSTRING name, PinType type)
        : NH_Object(id, name)
        , m_Type(type)
        , m_DisplayMode(ParameterDisplayMode::NameAndValue)
        , m_IsSource(false)
        , m_SourceID(0)
    {
        InitializeDefaultValue();
    }
    
    virtual ~ParameterNode() = default;
    
    // Core properties
    int GetID() const { return NH_Object::GetID(); }
    NH_CSTRING GetName() const { return NH_Object::GetName(); }
    void SetName(NH_CSTRING name) { NH_Object::SetName(name); }
    PinType GetType() const { return m_Type; }
    void SetType(PinType type) { m_Type = type; InitializeDefaultValue(); }
    
    // Display mode
    ParameterDisplayMode GetDisplayMode() const { return m_DisplayMode; }
    void SetDisplayMode(ParameterDisplayMode mode) { m_DisplayMode = mode; }
    void CycleDisplayMode();
    
    // Source/Shortcut management
    bool IsSource() const { return m_IsSource; }
    void SetIsSource(bool isSource) { m_IsSource = isSource; }
    int GetSourceID() const { return m_SourceID; }
    void SetSourceID(int sourceID) { m_SourceID = sourceID; }
    
    // Value access
    virtual bool GetBool() const { return m_BoolValue; }
    virtual int GetInt() const { return m_IntValue; }
    virtual float GetFloat() const { return m_FloatValue; }
    virtual const std::string& GetString() const { return m_StringValue; }
    
    virtual void SetBool(bool v) { m_BoolValue = v; }
    virtual void SetInt(int v) { m_IntValue = v; }
    virtual void SetFloat(float v) { m_FloatValue = v; }
    virtual void SetString(const std::string& v) { m_StringValue = v; }
    
    // String representation for display
    virtual std::string GetValueString() const;
    
    // Execution: retrieve value from connected input pin
    int Run(Node& node, App* app);
    
    // Context menu hook - add custom menu items
    void OnMenu(Node& node, App* app);
    
    // Build the node structure (add to Example's node list)
    void Build(Node& node, App* app);
    
    // Render (different modes render differently)
    void Render(Node& node, App* app, Pin* newLinkPin);
    
    // State save/load callbacks (optional - can override to save/load custom state)
    // These are called from App::SaveGraph() / App::LoadGraph()
    virtual void SaveState(Node& node, crude_json::value& nodeData, const Container* container, App* app);
    virtual void LoadState(Node& node, const crude_json::value& nodeData, Container* container, App* app);
    
    // Shortcut creation (only callable on source nodes)
    Node* CreateShortcut(Node& sourceNode, App* app);
    
    // Helper to sync value to source (called from shortcut setters and edit dialog)
    void SyncValueToSource(App* app);
    
    // Helper to sync value from source to all shortcuts
    void SyncValueToAllShortcuts(Node& node, App* app);
    
    // Helper to sync name from shortcut to source
    void SyncNameToSource(App* app);
    
    // Helper to sync name from source to all shortcuts
    void SyncNameToAllShortcuts(Node& node, App* app);
    
protected:
    // Internal run with depth limit to prevent infinite recursion
    int RunInternal(Node& node, App* app, int depth);
    
    void InitializeDefaultValue();
    
    // Rendering for each mode
    void RenderNameOnly(Node& node, App* app, Pin* newLinkPin);
    void RenderNameAndValue(Node& node, App* app, Pin* newLinkPin);
    void RenderSmallBox(Node& node, App* app, Pin* newLinkPin);
    void RenderMinimal(Node& node, App* app, Pin* newLinkPin);
    void RenderMinimalLinks(Node& node, App* app, Pin* newLinkPin);
    
    PinType     m_Type;
    ParameterDisplayMode m_DisplayMode;
    
    // Source/Shortcut state
    bool m_IsSource;      // true if this node is a source node
    int  m_SourceID;     // If > 0, this is a shortcut referencing source node with this ID
    
    // Value storage
    union {
        bool   m_BoolValue;
        int    m_IntValue;
        float  m_FloatValue;
    };
    std::string m_StringValue;
};

// Parameter registry for creating parameter nodes
class ParameterRegistry
{
public:
    static ParameterRegistry& Instance()
    {
        static ParameterRegistry instance;
        return instance;
    }
    
    ParameterNode* CreateParameter(PinType type, int id, NH_CSTRING name = nullptr);
    
private:
    ParameterRegistry() = default;
};


