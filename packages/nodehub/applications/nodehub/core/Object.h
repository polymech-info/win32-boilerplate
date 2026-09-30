#pragma once

#include "../commons.h"
#include "../utilities/uuid_generator.h"
#include <string>

class App;
class Container;
namespace crude_json { struct value; }

class NH_Object
{
public:
    NH_Object()
        : m_ID(0)
        , m_Name("")
        , m_TypeName("")
        , m_Parent(nullptr)
        , m_BehaviorFlags(NHBEHAVIOR_NONE)
        , m_ObjectFlags(NH_OBJECT_DYNAMIC)
    {}

    NH_Object(int id, NH_CSTRING name, NH_CSTRING typeName = nullptr)
        : NH_Object()
    {
        m_ID = id;
        if (name)
            m_Name = name;
        if (typeName)
            m_TypeName = typeName;
    }
    virtual ~NH_Object() = default;

    // Core identification
    int GetID() const { return m_ID; }
    void SetID(int id) { m_ID = id; }

    NH_CSTRING GetName() const { return m_Name.c_str(); }
    const std::string& GetNameString() const { return m_Name; }
    void SetName(NH_CSTRING name)
    {
        m_Name = name ? name : "";
    }

    NH_CSTRING GetTypeName() const { return m_TypeName.c_str(); }
    void SetTypeName(NH_CSTRING typeName)
    {
        m_TypeName = typeName ? typeName : "";
    }

    NH_Object* GetParent() const { return m_Parent; }
    void SetParent(NH_Object* parent) { m_Parent = parent; }

    // Behavior flags (Virtools-style)
    NH_BEHAVIOR_FLAGS GetBehaviorFlags() const { return m_BehaviorFlags; }
    void SetBehaviorFlags(NH_BEHAVIOR_FLAGS flags) { m_BehaviorFlags = flags; }
    void AddBehaviorFlags(NH_BEHAVIOR_FLAGS flags)
    {
        m_BehaviorFlags = static_cast<NH_BEHAVIOR_FLAGS>(
            static_cast<int>(m_BehaviorFlags) | static_cast<int>(flags));
    }

    void RemoveBehaviorFlags(NH_BEHAVIOR_FLAGS flags)
    {
        m_BehaviorFlags = static_cast<NH_BEHAVIOR_FLAGS>(
            static_cast<int>(m_BehaviorFlags) & ~static_cast<int>(flags));
    }

    // Object flags (base Virtools object flags)
    NH_OBJECT_FLAGS GetObjectFlags() const
    {
        return static_cast<NH_OBJECT_FLAGS>(m_ObjectFlags);
    }
    void SetObjectFlags(NH_OBJECT_FLAGS flags)
    {
        m_ObjectFlags = static_cast<NH_DWORD>(flags);
    }
    void AddObjectFlags(NH_OBJECT_FLAGS flags)
    {
        m_ObjectFlags |= static_cast<NH_DWORD>(flags);
    }
    void ClearObjectFlags(NH_OBJECT_FLAGS flags)
    {
        m_ObjectFlags &= ~static_cast<NH_DWORD>(flags);
    }

    // Serialization hooks (mirror block/parameter APIs)
    virtual void SaveState(crude_json::value& data, const Container* container, App* app) {}
    virtual void LoadState(const crude_json::value& data, Container* container, App* app) {}

    Uuid64 m_UUID;
    void SetUUID(Uuid64 uuid) { m_UUID = uuid; }
    Uuid64 GetUUID() const { return m_UUID; }
    
    
    NH_STRING GetClassName();
    // Class Registering
    static NH_STRING m_ClassName;

    virtual NH_CLASS_ID GetClassID() { return NHCID_PARAMETER; }

protected:
    int m_ID;
    std::string m_Name;
    std::string m_TypeName;
    NH_Object* m_Parent;
    NH_BEHAVIOR_FLAGS m_BehaviorFlags;
    NH_DWORD m_ObjectFlags;
    NH_CLASS_ID m_ClassID;

};
