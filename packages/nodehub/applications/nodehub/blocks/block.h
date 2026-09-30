#pragma once

#include "../commons.h"
#include "../core/Object.h"
#include "../utilities/node_renderer_base.h"
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <imgui.h>


// Forward declarations
class App;
class Container;
namespace crude_json { struct value; }

class Block : public NH_Object
{
public:
    Block(int id, NH_CSTRING name)
        : NH_Object(id, name)
        , m_Type(NodeType::Blueprint)
        , m_Color(ImColor(255, 255, 255))        
    {        
    }
    
    virtual ~Block() = default;
    
    // Core identification
    int GetID() const { return NH_Object::GetID(); }
    NH_CSTRING GetName() const { return NH_Object::GetName(); }
    NH_CSTRING GetTypeName() const { return NH_Object::GetTypeName(); }
    NH_BEHAVIOR_FLAGS GetFlags() const { return GetBehaviorFlags(); }
    void SetFlags(NH_BEHAVIOR_FLAGS flags) { SetBehaviorFlags(flags); }
    void AddFlags(NH_BEHAVIOR_FLAGS flags) { AddBehaviorFlags(flags); }
    void RemoveFlags(NH_BEHAVIOR_FLAGS flags) { RemoveBehaviorFlags(flags); }
    
    // Node building (like NHBehavior's CreateInput/CreateOutput)
    virtual void Build(Node& node, App* app) = 0;
    
    // Rendering (each block renders itself)
    virtual void Render(Node& node, App* app, Pin* newLinkPin) = 0;
    
    // Serialization support
    virtual NH_CSTRING GetBlockType() const = 0;  // Unique type identifier
    
    // State save/load callbacks (optional - subclasses can override to save/load custom state)
    // These are called from App::SaveGraph() / App::LoadGraph()
    virtual void SaveState(Node& node, crude_json::value& nodeData, const Container* container, App* app) {}
    virtual void LoadState(Node& node, const crude_json::value& nodeData, Container* container, App* app) {}
    
    // Parameter management (like NHBehavior)
    virtual int GetInputParameterCount() const { return 0; }
    virtual int GetOutputParameterCount() const { return 0; }
    virtual int GetInputCount() const { return 0; }   // Flow inputs (triggers)
    virtual int GetOutputCount() const { return 0; }  // Flow outputs (events)
    
    // Execution (stub: returns E_OK = 0)
    virtual int Run(Node& node, App* app) { return E_OK; }
    
    // Activation API (for flow control)
    virtual void ActivateOutput(int pos, bool active = true);
    virtual bool IsOutputActive(int pos) const;
    virtual void ActivateInput(int pos, bool active = true);
    virtual bool IsInputActive(int pos) const;
    
    // Context menu hook - add custom menu items
    virtual void OnMenu(Node& node, App* app) {}
    
    // Edit dialog UI hook - add custom UI elements to block edit dialog
    // Called from RenderBlockEditDialog() to allow blocks to append their own settings
    virtual void RenderEditUI(Node& node, App* app) {}

    // Flow handling
    virtual bool ShouldAutoActivateDefaultOutput() const { return true; }
    
protected:
    // Activation state (stored per-block instance)
    std::vector<bool> m_OutputActive;  // Output activation states
    std::vector<bool> m_InputActive;   // Input activation states
    NodeType m_Type;
    ImColor m_Color;
};

// Extended block with parameters - like NHBehavior with params
class ParameterizedBlock : public Block, public ax::NodeRendering::NodeRendererBase
{
public:
    ParameterizedBlock(int id, NH_CSTRING name)
        : Block(id, name)
    {
    }
    
    virtual ~ParameterizedBlock() = default;
    
    // Parameter creation helpers (like NHBehavior::CreateInputParameter)
    void AddInputParameter(App* app, Node& node, NH_CSTRING name, PinType type);
    void AddOutputParameter(App* app, Node& node, NH_CSTRING name, PinType type);
    
    // I/O creation (flow control)
    void AddInput(App* app, Node& node, NH_CSTRING name);
    void AddOutput(App* app, Node& node, NH_CSTRING name);
    
    int GetInputParameterCount() const override { return (int)m_InputParams.size(); }
    int GetOutputParameterCount() const override { return (int)m_OutputParams.size(); }
    int GetInputCount() const override { return (int)m_Inputs.size(); }
    int GetOutputCount() const override { return (int)m_Outputs.size(); }
    
    // Default Virtools-style rendering
    void Render(Node& node, App* app, Pin* newLinkPin) override;
    
    // Context menu hook - default implementation for parameterized blocks
    void OnMenu(Node& node, App* app) override;
    
    // State save/load - override to save/load unconnected parameter input values
    void SaveState(Node& node, crude_json::value& nodeData, const Container* container, App* app) override;
    void LoadState(Node& node, const crude_json::value& nodeData, Container* container, App* app) override;
    
protected:
    // Parameter value helpers (for use in Run() implementations)
    // Get input parameter value - reads from connected parameter node or default value
    static int GetInputParamValueInt(const Pin& pin, Node& node, App* app, int defaultValue = 0);
    static float GetInputParamValueFloat(const Pin& pin, Node& node, App* app, float defaultValue = 0.0f);
    static bool GetInputParamValueBool(const Pin& pin, Node& node, App* app, bool defaultValue = false);
    static std::string GetInputParamValueString(const Pin& pin, Node& node, App* app, const std::string& defaultValue = "");
    
    // Set output parameter value - stores value and propagates to connected parameter nodes
    static void SetOutputParamValueInt(const Pin& pin, Node& node, App* app, int value);
    static void SetOutputParamValueFloat(const Pin& pin, Node& node, App* app, float value);
    static void SetOutputParamValueBool(const Pin& pin, Node& node, App* app, bool value);
    static void SetOutputParamValueString(const Pin& pin, Node& node, App* app, const std::string& value);
    
    std::vector<int> m_InputParams;   // Parameter pin IDs (top)
    std::vector<int> m_OutputParams;  // Parameter pin IDs (bottom)
    std::vector<int> m_Inputs;        // Flow input IDs (left)
    std::vector<int> m_Outputs;       // Flow output IDs (right)
};

// Block factory function signature
using BlockFactory = std::function<Block*(int id)>;

// Block registry - like Virtools' behavior manager
class BlockRegistry
{
public:
    static BlockRegistry& Instance()
    {
        static BlockRegistry instance;
        return instance;
    }
    
    void RegisterBlock(NH_CSTRING typeName, BlockFactory factory)
    {
        m_Factories[typeName] = factory;
    }
    
    Block* CreateBlock(NH_CSTRING typeName, int id)
    {
        auto it = m_Factories.find(typeName);
        if (it != m_Factories.end())
            return it->second(id);
        return nullptr;
    }
    
    std::vector<std::string> GetRegisteredTypes() const
    {
        std::vector<std::string> types;
        for (const auto& pair : m_Factories)
            types.push_back(pair.first);
        return types;
    }
    
private:
    BlockRegistry() = default;
    std::map<std::string, BlockFactory> m_Factories;
};

// Helper macro for auto-registration
#define REGISTER_BLOCK(ClassName, TypeName) \
    namespace { \
        struct ClassName##_Registrar { \
            ClassName##_Registrar() { \
                BlockRegistry::Instance().RegisterBlock(TypeName, \
                    [](int id) -> Block* { return new ClassName(id); }); \
            } \
        }; \
        static ClassName##_Registrar g_##ClassName##_Registrar; \
    }

