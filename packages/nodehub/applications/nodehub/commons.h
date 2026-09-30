#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <functional>

#include "./enums.h"
#include "./utilities/uuid_generator.h"


#ifndef NH_CSTRING
#define NH_CSTRING const char*
#endif

typedef char* NH_STRING;
typedef char NH_CHAR;
typedef unsigned short* NH_USTRING;
typedef unsigned short NH_UCHAR;

typedef int NH_BOOL;
typedef unsigned char NH_BYTE;
typedef unsigned int NH_DWORD;
typedef unsigned short NH_WORD;

typedef int NH_ERROR;
typedef int NH_PARAMETER_TYPE;
typedef int NH_OPERATION_TYPE;
typedef int NH_MESSAGE_TYPE;
typedef int NH_ATTRIBUTE_TYPE;
typedef int NH_ATTRIBUTE_CATEGORY;

typedef Uuid64 NH_GUID;
typedef unsigned int NH_ID;
typedef unsigned int NH_CLASS_ID;
