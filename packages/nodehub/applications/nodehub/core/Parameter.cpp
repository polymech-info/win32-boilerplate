#include "Parameter.h"
#include "Context.h"
#include "ParameterManager.h"
#include <cstring>

NH_Parameter::NH_Parameter(int id, NH_CSTRING name) : NH_Object(id, name) {
    m_Owner = nullptr;
    m_AllocatedSize = 0;
    m_Buffer = nullptr;
    m_ParamType = nullptr;
}

NH_Parameter::~NH_Parameter() {
    if (m_Buffer) {
        delete[] m_Buffer;
        m_Buffer = nullptr;
    }
}

NH_ERROR NH_Parameter::GetValue(void *buf, NH_BOOL update) {
    if (!buf || !m_Buffer) return E_FAIL;
    memcpy(buf, m_Buffer, m_DataSize);
    return E_OK;
}

NH_ERROR NH_Parameter::SetValue(const void *buf, int size) {
    if (!buf) return E_FAIL;
    
    int dataSize = size;
    if (dataSize == 0 && m_ParamType) {
        dataSize = m_ParamType->DefaultSize;
    }

    if (dataSize == 0) return E_FAIL;

    if (m_AllocatedSize < dataSize) {
        if (m_Buffer) {
            delete[] m_Buffer;
        }
        m_Buffer = new NH_BYTE[dataSize];
        m_AllocatedSize = dataSize;
    }

    memcpy(m_Buffer, buf, dataSize);
    m_DataSize = dataSize;

    return E_OK;
}

NH_ERROR NH_Parameter::CopyValue(NH_Parameter *param, NH_BOOL UpdateParam) {
  return E_NOTIMPL;
}

void *NH_Parameter::GetReadDataPtr(NH_BOOL update) {
    return m_Buffer;
}

void *NH_Parameter::GetWriteDataPtr() {
  return nullptr;
}

NH_ERROR NH_Parameter::SetStringValue(NH_STRING Value) {
    return E_NOTIMPL;
}

int NH_Parameter::GetStringValue(NH_STRING Value, NH_BOOL update) {
    return E_NOTIMPL;
}
