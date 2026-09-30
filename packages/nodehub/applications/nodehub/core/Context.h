#ifndef NH_CONTEXT_H
#define NH_CONTEXT_H

#include "../commons.h"
#include "../types.h"

class App;
class NH_ParameterManager;
class NH_ParameterIn;
class NH_ParameterOut;
struct Uuid64;

class NH_Context {
public:
  NH_Context();
  virtual ~NH_Context();

  NH_ParameterManager* GetParameterManager() { return m_ParameterManager; }
  void SetParameterManager(NH_ParameterManager* parameterManager) { m_ParameterManager = parameterManager; }

  App* GetApp() { return m_App; }
  void SetApp(App* app) { m_App = app; }

  //-----------------------------------------
  // Lifecycle
  //-----------------------------------------
  NH_ERROR init(App *app);

  // Object Management
  NH_ParameterIn* CreateParameterIn(NH_CSTRING name, const Uuid64& guid);
  NH_ParameterOut* CreateParameterOut(NH_CSTRING name, const Uuid64& guid);

protected:
  NH_ParameterManager* m_ParameterManager;

  // legacy support
  App* m_App;

};

#endif
