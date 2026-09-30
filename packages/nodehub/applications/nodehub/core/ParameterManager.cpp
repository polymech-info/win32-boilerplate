#include "ParameterManager.h"
#include <string>

NH_ParameterManager::NH_ParameterManager(NH_Context *context) : NH_BaseManager() {
  SetContext(context);
}

NH_ERROR NH_ParameterManager::OnInit() {
  NH_ParameterTypeDesc pdesc;

  // Float
  pdesc.Guid = NHP_GUID_FLOAT;
  pdesc.TypeName = "Float";
  pdesc.DefaultSize = sizeof(float);
  RegisterParameterType(&pdesc);

  // Int
  pdesc.Guid = NHP_GUID_INT;
  pdesc.TypeName = "Int";
  pdesc.DefaultSize = sizeof(int);
  RegisterParameterType(&pdesc);

  // Bool
  pdesc.Guid = NHP_GUID_BOOL;
  pdesc.TypeName = "Bool";
  pdesc.DefaultSize = sizeof(bool);
  RegisterParameterType(&pdesc);

  // String
  pdesc.Guid = NHP_GUID_STRING;
  pdesc.TypeName = "String";
  pdesc.DefaultSize = sizeof(char*);
  RegisterParameterType(&pdesc);

  // Vector
  pdesc.Guid = NHP_GUID_VECTOR;
  pdesc.TypeName = "Vector";
  pdesc.DefaultSize = sizeof(float) * 3;
  RegisterParameterType(&pdesc);
  
  return E_OK;
}

NH_ERROR
NH_ParameterManager::RegisterParameterType(NH_ParameterTypeDesc *parameterType) {
  if (!parameterType)
    return E_FAIL;

  // Check if GUID is valid
  if (!parameterType->Guid.IsValid())
    return E_FAIL;

  // Check if GUID already exists
  if (m_ParameterGuids.find(parameterType->Guid) != m_ParameterGuids.end()) {
    return E_OK; // Or an error for duplicate
  }

  m_ParameterTypes.push_back(*parameterType);
  int newIndex = m_ParameterTypes.size() - 1;
  m_ParameterTypes.back().Index = newIndex;
  m_ParameterGuids[parameterType->Guid] = newIndex;

  return E_OK;
}