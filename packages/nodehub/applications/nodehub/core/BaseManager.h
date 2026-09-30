#ifndef NH_BASE_MANAGER_H
#define NH_BASE_MANAGER_H

#include "../commons.h"
#include "../types.h"

class NH_Context;

class NH_BaseManager {
public:
  NH_BaseManager() {}
  virtual ~NH_BaseManager() = default;

  virtual NH_ERROR OnInit() { return E_OK; }

  NH_Context* GetContext() { return m_Context; }
  void SetContext(NH_Context* context) { m_Context = context; } 

  Uuid64 GetGuid() { return m_Guid; }
  std::string GetName() { return m_Name; }
  
  void SetName(std::string name) { m_Name = name; }
  void SetGuid(Uuid64 guid) { m_Guid = guid; }

  protected:
    NH_Context* m_Context;
    Uuid64 m_Guid;
    std::string m_Name;

};


#endif