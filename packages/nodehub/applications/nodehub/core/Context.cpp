#include "Context.h"
#include "../logging.h"
#include "ParameterManager.h"
#include "ParameterIn.h"
#include "ParameterOut.h"
#include "../utilities/uuid_generator.h"

NH_Context::NH_Context() : m_ParameterManager(nullptr), m_App(nullptr) {}

NH_Context::~NH_Context()
{
    delete m_ParameterManager;
}

NH_ERROR NH_Context::init(App *app)
{
    m_App = app;
    m_ParameterManager = new NH_ParameterManager(this);
    m_ParameterManager->OnInit();
    LOG_INFO("Context initialized");
    return E_OK;
}

NH_ParameterIn* NH_Context::CreateParameterIn(NH_CSTRING name, const Uuid64& guid)
{
    // A proper implementation would use an object manager to handle IDs and memory.
    auto* param = new NH_ParameterIn(0, name);
    // TODO: Set the parameter type based on the GUID by looking it up in the ParameterManager.
    return param;
}

NH_ParameterOut* NH_Context::CreateParameterOut(NH_CSTRING name, const Uuid64& guid)
{
    // A proper implementation would use an object manager.
    auto* param = new NH_ParameterOut(0, name);
    // TODO: Set the parameter type based on the GUID.
    return param;
}