#ifndef NH_PARAMETER_MANAGER_H
#define NH_PARAMETER_MANAGER_H

#include "../commons.h"
#include "../enums.h"
#include "../types.h"
#include "./BaseManager.h"
#include "./Parameter.h"
#include <map>
#include <vector>

class NH_ParameterManager : public NH_BaseManager {
public:
  NH_ParameterManager(NH_Context *context);
  virtual ~NH_ParameterManager() = default;

  virtual NH_ERROR OnInit() override;

  NH_ERROR RegisterParameterType(NH_ParameterTypeDesc *parameterType);

protected:
  std::vector<NH_ParameterTypeDesc> m_ParameterTypes;
  std::map<Uuid64, int> m_ParameterGuids;
};

#endif