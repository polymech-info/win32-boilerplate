#pragma once
#include "commons.h"
#define IMGUI_DEFINE_MATH_OPERATORS
#include "utilities/uuid_generator.h"
#include <cassert>
#include <cstdint>
#include <imgui.h>

#include <imgui_internal.h> // For ImRect
#include <imgui_node_editor.h>
#include <limits>
#include <map>
#include <string>
#include <vector>

//------------------------------------------------------------------------------
//
// v2
//------------------------------------------------------------------------------

class NH_Parameter;
class NH_DependenciesContext {
public:
  NH_DependenciesContext() {}
  ~NH_DependenciesContext() {}
};
//----------------------------------------------------------//
//		Parameter type functions
////
//----------------------------------------------------------//

struct NH_PluginEntry;

typedef NH_ERROR (*NH_PARAMETERCREATEDEFAULTFUNCTION)(NH_Parameter *);
typedef void (*NH_PARAMETERDELETEFUNCTION)(NH_Parameter *);
typedef void (*NH_PARAMETERCHECKFUNCTION)(NH_Parameter *);
typedef void (*NH_PARAMETERCOPYFUNCTION)(NH_Parameter *, NH_Parameter *);
typedef int (*NH_PARAMETERSTRINGFUNCTION)(NH_Parameter *param,
                                          NH_STRING ValueString,
                                          NH_BOOL ReadFromString);
typedef void (*NH_PARAMETERREMAPFUNCTION)(NH_Parameter *,
                                          NH_DependenciesContext &);
/// typedef void	(*NH_PARAMETERSAVELOADFUNCTION)(NH_Parameter*
/// param,NH_StateChunk **chunk,NH_BOOL load);
// typedef WIN_HANDLE	(*NH_PARAMETERUICREATORFUNCTION)(NH_Parameter*
// param,WIN_HANDLE ParentWindow,CKRECT *rect);

// for custom parameters, in use with CustomObjectParameterDialog
struct NH_CustomParameter {
  int id;
  const char *name;
};
// if iIndex<0 || oObjec==0, the function shall return object count
// otherwise, shall return 0 if failed, 1 otherwise,
// adress of an existing instance of CKCustomParameter should be given
/// typedef int	(*NH_PARAMETERGETCUSTOMOBJECTFUNCTION)(NH_Parameter* iParam,int
/// iIndex,NH_CustomParameter* ioObject);

typedef struct NH_ParameterTypeDesc {
  // Index in the parameter array (used internally)
  NH_PARAMETER_TYPE Index;
  // Glocal Unique identifier to identify this type
  Uuid64 Guid;
  // GUID of the parameter type from which this type is derivated
  Uuid64 DerivedFrom;
  // Name of this type
  std::string TypeName;
  // (used internally)
  int Valid;
  // Default size (in bytes)	of parameters ofthis type
  int DefaultSize;
  // Creation function called each time a parameter of this type is created.
  NH_PARAMETERCREATEDEFAULTFUNCTION CreateDefaultFunction;
  // Deletion function called each time a parameter of this type is deleted.
  NH_PARAMETERDELETEFUNCTION DeleteFunction;
  // Function use to save or load parameters of this type. Only needed if
  // special processing should be done during load and save operations.
  /// NH_PARAMETERSAVELOADFUNCTION		SaveLoadFunction;
  // Function use to check parameters for object utilisation
  NH_PARAMETERCHECKFUNCTION CheckFunction;
  // Function use to copy the value from a parameter to another (Optionnal).
  NH_PARAMETERCOPYFUNCTION CopyFunction;
  // Function to convert a parameter to or from a string.
  NH_PARAMETERSTRINGFUNCTION StringFunction;
  // Function called to create the dialog box when editing this type of
  // parameter.
  /// NH_PARAMETERUICREATORFUNCTION		UICreatorFunction;

  // An index to the registred Dlls from which this type was declared (used
  // internally)
  // NH_PluginEntry *CreatorDll;
  // An application reserved DWORD for placing parameter type specific data.
  NH_DWORD dwParam;
  // Flags specifying special settings for this parameter type
  // (CK_PARAMETERTYPE_FLAGS)
  NH_DWORD dwFlags;
  // Special case for parameter types that refer to CKObjects => corresponding
  // class ID of the object
  NH_DWORD Cid;
  // Updated by parameter manager...: Bitmask for all class this type can be
  // derived from directly or indirectly (used internally)
  // XBitArray  DerivationMask;
  // Int Manager GUID
  // Uuid64 Saver_Manager;

  // for custom parameters, in use with CustomObjectParameterDialog
  // if iIndex<0 || oObjec==0, the function shall return object count
  // otherwise, shall return 0 if failed, 1 otherwise,
  // adress of an existing instance of CKCustomParameter should be given
  /// NH_PARAMETERGETCUSTOMOBJECTFUNCTION	GetCustomObjectFunction;

  NH_ParameterTypeDesc() : Cid(0), DefaultSize(0), Valid(0), dwParam(0), dwFlags(0) {
    StringFunction = nullptr;
    CreateDefaultFunction = nullptr;
    DeleteFunction = nullptr;
    CopyFunction = nullptr;
    CheckFunction = nullptr;
  }
} NH_ParameterTypeDesc;

//------------------------------------------------------------------------------
//
// Legacy code from imgui-node-editor
//------------------------------------------------------------------------------

namespace ed = ax::NodeEditor;

template <typename IdT> inline int ToRuntimeId(IdT id) {
  const auto value = id.Get();
  return static_cast<int>(value);
}

// Return code constants
enum {
  E_OK = 0, // Success return code
  E_NOTIMPL,
  E_ACCESSDENIED,
  E_FAIL
};

// Forward declarations
class ParameterNode;
enum class ParameterDisplayMode;

// Block display modes
enum class BlockDisplayMode {
  NameOnly,         // Just block name (compact)
  NameAndParameters // Block name + parameter labels (default)
};

enum class PinType {
  Flow,
  Bool,
  Int,
  Float,
  String,
  Object,
  Function,
  Delegate,
};

enum class PinKind { Output, Input };

enum class NodeType {
  Blueprint,
  Simple,
  Tree,
  Comment,
  Houdini,
  Parameter, // Standalone parameter value node
  Group      // Group block node
};

struct Node;

struct Pin {
  ed::PinId ID; // Runtime ID (dynamic, for imgui-node-editor)
  Uuid64 UUID;  // Persistent ID (stable across sessions)
  ::Node *Node;
  std::string Name;
  PinType Type;
  PinKind Kind;

  // Position tracking (updated during rendering)
  ImVec2 LastPivotPosition; // Where links connect (screen space)
  ImRect LastRenderBounds;  // Full pin area for hit testing
  bool HasPositionData;     // True after first render

  Pin(int id, const char *name, PinType type)
      : ID(id), UUID(0, 0), Node(nullptr), Name(name), Type(type),
        Kind(PinKind::Input), LastPivotPosition(0, 0),
        LastRenderBounds(ImVec2(0, 0), ImVec2(0, 0)), HasPositionData(false) {}

  // Get pin position relative to its node (implementation after Node is
  // defined)
  ImVec2 GetRelativePivotPosition() const;
};

struct Node {
  ed::NodeId ID; // Runtime ID (dynamic, for imgui-node-editor)
  Uuid64 UUID;   // Persistent ID (stable across sessions)
  std::string Name;
  std::vector<Pin> Inputs;
  std::vector<Pin> Outputs;
  ImColor Color;
  NodeType Type;
  ImVec2 Size;

  std::string State;
  std::string SavedState;

  // Block metadata (only used when (Type == NodeType::Blueprint || Type ==
  // NodeType::Group) && IsBlockBased())
  std::string
      BlockType; // e.g. "Math.Add" or "Group" - empty for hardcoded nodes
  bool IsBlockBased() const { return !BlockType.empty(); }
  ::BlockDisplayMode BlockDisplay; // Display mode for blocks
  class Block *BlockInstance; // Block class instance (owns rendering logic) -
                              // ONLY set for block-based nodes

  // Safe getter for BlockInstance - returns nullptr for non-block nodes
  // Full validation (ID match, node type) should be done at call site after
  // including block.h
  class Block *GetBlockInstance() const {
    // Only return BlockInstance for actual block-based nodes
    if (Type == NodeType::Parameter || !IsBlockBased())
      return nullptr;
    return BlockInstance;
  }

  // Parameter node metadata (only used when Type == NodeType::Parameter)
  PinType ParameterType;
  union {
    bool BoolValue;
    int IntValue;
    float FloatValue;
  };
  std::string StringValue;          // For string parameters
  ParameterNode *ParameterInstance; // Parameter class instance (owned by node)
                                    // - ONLY set for parameter nodes

  // Safe getter for ParameterInstance - returns nullptr for non-parameter nodes
  ParameterNode *GetParameterInstance() const {
    // Only return ParameterInstance for actual parameter nodes
    if (Type != NodeType::Parameter)
      return nullptr;
    return ParameterInstance;
  }

  // Unconnected parameter values (for block input parameters)
  // Maps pin ID -> value (stored as string, parsed based on pin type)
  std::map<int, std::string> UnconnectedParamValues;

  Node(int id, const char *name, ImColor color = ImColor(255, 255, 255))
      : ID(id), UUID(0, 0), Name(name), Color(color), Type(NodeType::Blueprint),
        Size(0, 0), BlockType(""),
        BlockDisplay(::BlockDisplayMode::NameAndParameters),
        BlockInstance(nullptr), ParameterType(PinType::Float), FloatValue(0.0f),
        StringValue(""), ParameterInstance(nullptr) {}
};

struct Link {
  ed::LinkId ID; // Runtime ID (dynamic, for imgui-node-editor)
  Uuid64 UUID;   // Persistent ID (stable across sessions)

  ed::PinId StartPinID;
  ed::PinId EndPinID;

  ImColor Color;
  bool IsParameterLink; // True if parameter → block input (for styling)
  bool UserManipulatedWaypoints; // True if user manually edited waypoints
                                 // (preserve path, disable auto-adjust)
  float Delay; // Custom delay unit (displayed on hover, editable via
               // double-click)

  Link(ed::LinkId id, ed::PinId startPinId, ed::PinId endPinId)
      : ID(id), UUID(0, 0), StartPinID(startPinId), EndPinID(endPinId),
        Color(255, 255, 255), IsParameterLink(false),
        UserManipulatedWaypoints(false), Delay(0.0f) {}
};

struct NodeIdLess {
  bool operator()(const ed::NodeId &lhs, const ed::NodeId &rhs) const {
    return lhs.AsPointer() < rhs.AsPointer();
  }
};

struct LinkIdLess {
  bool operator()(const ed::LinkId &lhs, const ed::LinkId &rhs) const {
    return lhs.AsPointer() < rhs.AsPointer();
  }
};

//------------------------------------------------------------------------------
// Pin method implementations (after Node is fully defined)
//------------------------------------------------------------------------------

inline ImVec2 Pin::GetRelativePivotPosition() const {
  if (Node && HasPositionData) {
    ImVec2 nodePos = ed::GetNodePosition(Node->ID);
    return LastPivotPosition - nodePos;
  }
  return ImVec2(0, 0);
}
