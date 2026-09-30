#pragma once
#include "config.h"
#include "utilities/uuid_generator.h"


//------------------------------------------------------------------------------
//
//  Objects
//
//------------------------------------------------------------------------------

/***************************************************
{filename:CK_OBJECT_FLAGS}
Summary: CKObject Flags

Remarks:
+ Flags specifying special settings for basic objects.
+ Some of this flags are shared with sub-classes such as CKParameterIn,CKParameterOut and CKBehaviorIO.
+ You rarely need to modify directly this flags through CKObject::SetFlags or CKObject::ModifyObjectFlags instead
you should always use the specific acces function (given between ()) which may need to perform additionnal operations.
See also: CKObject,CKObject::GetObjectFlags,CKObject::ModifyObjectFlags
*************************************************/
typedef enum NH_OBJECT_FLAGS {
	NH_OBJECT_INTERFACEOBJ			=	 0x00000001,		// Reserved for Inteface Use
	NH_OBJECT_PRIVATE				=	 0x00000002,		// The object must not be displayed in interface (Lists,Level view,etc...),nor should it be saved. (CKObject::IsPrivate()
	NH_OBJECT_INTERFACEMARK			=	 0x00000004,		
	NH_OBJECT_FREEID				=	 0x00000008,		// ID of this object can be released safely and is free to be reused by future CKobjects.
	NH_OBJECT_TOBEDELETED			=	 0x00000010,		// This object is being deleted
	NH_OBJECT_NOTTOBESAVED			=	 0x00000020,		// This object must not be saved 
	NH_OBJECT_VISIBLE				=	 0x00000040,		// This object is visible (CKObject::Show)
	NH_OBJECT_NAMESHARED			=	 0x00000080,		// This object has its name from another entity
	NH_OBJECT_DYNAMIC				=	 0x00000108,		// This object may be created or deleted at run-time, it also contails CK_OBJECT_FREEID. (CKObject::IsDynamic,CKContext::CreateObject)
	NH_OBJECT_HIERACHICALHIDE		=	 0x00000200,		// This object hides all its hierarchy (CKObject::Show)
	NH_OBJECT_UPTODATE				=	 0x00000400,		// (Camera,etc..)
	NH_OBJECT_TEMPMARKER			=	 0x00000800,		
	NH_OBJECT_ONLYFORFILEREFERENCE	=	 0x00001000,		
	NH_OBJECT_NOTTOBEDELETED		=	 0x00002000,		// This object must not be deleted in a clear all
	NH_OBJECT_APPDATA				=	 0x00004000,		// This object has app data	
	NH_OBJECT_SINGLEACTIVITY		=	 0x00008000,		// this object has an information of single activity (active at scene start,etc..)
	NH_OBJECT_LOADSKIPBEOBJECT		=	 0x00010000,		// When loading this object the CKBeObject part should be skipped
	NH_OBJECT_KEEPSINGLEACTIVITY	=	 0x00020000,		// this object must keep its information of single activity (active at scene start,etc..)
	NH_OBJECT_LOADREPLACINGOBJECT	=	 0x00040000,		// Indicates the object being loaded is being replaced
	
	NH_OBJECT_NOTTOBELISTEDANDSAVED	=	 0x00000023,		// Combination of Private and Not To Be Saved
	NH_OBJECT_SELECTIONSET			=	 0x00080000,		// if group, then it is a selection set, otherwise, temporary flag used for objects belonging to a selection set. Used by Virtools's Interface
	NH_OBJECT_VR_DISTRIBUTED		=	 0x00100000,		// distributed object for VR (ie mainly used for distributed parameters for VR)


// The following flags are specific to parameters (they are stored here for object's size purposes )
	NH_PARAMETEROUT_SETTINGS		=	0x00400000,			
	NH_PARAMETEROUT_PARAMOP			=	0x00800000,			// This parameter is the output of a CKParameterOperation (Automatically set by Engine)
	NH_PARAMETERIN_DISABLED			=	0x01000000,			// Parameter In or Out is disabled (CKBehavior::EnableInputParameter,CKBehavior::DisableInputParameter)	
	NH_PARAMETERIN_THIS				=	0x02000000,			// Special parameter type : its value and type are always equal to its owner (CKParameter::SetAsMyselfParameter)
	NH_PARAMETERIN_SHARED		    =	0x04000000,			
	NH_PARAMETEROUT_DELETEAFTERUSE 	=	0x08000000,			// When adding parameters to CKMessage, they can be automatically deleted when message is released (CKMessage::AddParameter)
	NH_OBJECT_PARAMMASK			  	=	0x0FC00000,			// Mask for options specific to parameters	

// The Following flags are specific	for Behavior ios (CKBehaviorIO)
	NH_BEHAVIORIO_IN				=	0x10000000,		// This BehaviorIO is a behavior input  (CKBehaviorIO::SetType}
	NH_BEHAVIORIO_OUT				=	0x20000000,		// This BehaviorIO is a behavior output (CKBehaviorIO::SetType)	
	NH_BEHAVIORIO_ACTIVE			=	0x40000000,		// This BehaviorIO is a currently active (CKBehaviorIO::Activate}
	NH_OBJECT_IOTYPEMASK		  	=	0x30000000,		
	NH_OBJECT_IOMASK			  	=	0xF0000000,		

	// The Following flags are specific	for Behavior ios (CKBehaviorIO)
	NH_BEHAVIORLINK_RESERVED			=	0x10000000,		// This BehaviorIO is a behavior input  (CKBehaviorIO::SetType}
	NH_BEHAVIORLINK_ACTIVATEDLASTFRAME =	0x20000000,		// This link had been activated last frame
	NH_OBJECT_BEHAVIORLINKMASK		=	0x30000000,		
} NH_OBJECT_FLAGS;


//------------------------------------------------------------------------------
//
//  Building Blocks
//
//------------------------------------------------------------------------------

/************************************************************
{filename:NH_BEHAVIOR_FLAGS}
Summary: Flags settings for behaviors.
Remarks:
	+ When creating a prototype, you can precise various flags
	about how your behavior will act: whether it will send or receive message, 
	does the user may add inputs,outputs or parameters, is it active, etc. 

See also: NHBehaviorPrototype::SetBehaviorFlags,Behavior Prototype Creation
**********************************************************/
typedef enum NH_BEHAVIOR_FLAGS {
    NHBEHAVIOR_NONE							=0x00000000,	// Reserved for future use
    NHBEHAVIOR_ACTIVE						=0x00000001,	// This behavior is active
    NHBEHAVIOR_SCRIPT						=0x00000002,	// This behavior is a script
    NHBEHAVIOR_RESERVED1					=0x00000004,	// Reserved for internal use.
    NHBEHAVIOR_USEFUNCTION					=0x00000008,	// Behavior uses a function and not a graph
    NHBEHAVIOR_RESERVED2					=0x00000010,	// Reserved for internal use.
    NHBEHAVIOR_CUSTOMSETTINGSEDITDIALOG		=0x00000020,	// Behavior has a custom Dialog Box for settings edition .
    NHBEHAVIOR_WAITSFORMESSAGE				=0x00000040,	// Behavior is waiting for a message to activate one of its outputs
    NHBEHAVIOR_VARIABLEINPUTS				=0x00000080,	// Behavior may have its inputs changed by editing them
    NHBEHAVIOR_VARIABLEOUTPUTS				=0x00000100,	// Behavior may have its outputs changed by editing them
    NHBEHAVIOR_VARIABLEPARAMETERINPUTS		=0x00000200,	// Behavior may have its number of input parameters changed by editing them
    NHBEHAVIOR_VARIABLEPARAMETEROUTPUTS		=0x00000400,	// Behavior may have its number of output parameters changed by editing them
    NHBEHAVIOR_TOPMOST						=0x00004000,	// No other Behavior includes this one
    NHBEHAVIOR_BUILDINGBLOCK				=0x00008000,	// This Behavior is a building block. Automatically set by the engine when coming from a DLL.
    NHBEHAVIOR_MESSAGESENDER				=0x00010000,	// Behavior may send messages during its execution.
    NHBEHAVIOR_MESSAGERECEIVER				=0x00020000,	// Behavior may check messages during its execution.
    NHBEHAVIOR_TARGETABLE					=0x00040000,	// Behavior may be owned by a different object that the one to which its execution will apply.
    NHBEHAVIOR_CUSTOMEDITDIALOG				=0x00080000,	// This Behavior have a custom Dialog Box for parameters edition .
    NHBEHAVIOR_RESERVED0					=0x00100000,	// Reserved for internal use.
    NHBEHAVIOR_EXECUTEDLASTFRAME			=0x00200000,	// This behavior has been executed during last process. (Available only in profile mode )
    NHBEHAVIOR_DEACTIVATENEXTFRAME			=0x00400000,	// Behavior will be deactivated next frame
    NHBEHAVIOR_RESETNEXTFRAME				=0x00800000,	// Behavior will be reseted next frame
    NHBEHAVIOR_INTERNALLYCREATEDINPUTS		=0x01000000,	// Behavior execution may create/delete inputs
    NHBEHAVIOR_INTERNALLYCREATEDOUTPUTS		=0x02000000,	// Behavior execution may create/delete outputs
    NHBEHAVIOR_INTERNALLYCREATEDINPUTPARAMS	=0x04000000,	// Behavior execution may create/delete input parameters or change their type
    NHBEHAVIOR_INTERNALLYCREATEDOUTPUTPARAMS=0x08000000,	// Behavior execution may create/delete output parameters or change their type
    NHBEHAVIOR_INTERNALLYCREATEDLOCALPARAMS	=0x40000000,	// Behavior execution may create/delete local parameters or change their type
    NHBEHAVIOR_ACTIVATENEXTFRAME			=0x10000000,	// Behavior will be activated next frame
    NHBEHAVIOR_LOCKED						=0x20000000,	// Behavior is locked for utilisation in Virtools
    NHBEHAVIOR_LAUNCHEDONCE					=0x80000000,	// Behavior has not yet been launched...
}	NH_BEHAVIOR_FLAGS;            

//------------------------------------------------------------------------------
//
//  Parameters
//
//------------------------------------------------------------------------------

/*************************************************
{filename:NH_PARAMETERTYPE_FLAGS}
Summary:  Flags settings for new parameter types

Remarks:
+These flags specify special settings for a parameter type.
+Parameter may have a fixed or variable buffer size, some may be hidden
so that they are not displayed in the interface.
See also: NHParameterTypeDesc
*************************************************/
typedef enum NH_PARAMETERTYPE_FLAGS {
			NHPARAMETERTYPE_VARIABLESIZE =	   0x00000001,	// Size of the buffer stored by the parameter may change
			NHPARAMETERTYPE_RESERVED     =	   0x00000002,	// Reserved
			NHPARAMETERTYPE_HIDDEN		 =	   0x00000004,	// This parameter type should not be shown in the interface
			NHPARAMETERTYPE_FLAGS		 =	   0x00000008,	// This parameter type is a flag (See NHParameterManager::RegisterNewFlags)
			NHPARAMETERTYPE_STRUCT		 =	   0x00000010,	// This parameter type is a structure of parameters (See NHParameterManager::RegisterNewStructure)
			NHPARAMETERTYPE_ENUMS		 =	   0x00000020,	// This parameter type is an enumeration (See NHParameterManager::RegisterNewEnum)
			NHPARAMETERTYPE_USER		 =	   0x00000040,	// This parameter type is a user-defined one created in the interface
			NHPARAMETERTYPE_NOENDIANCONV =	   0x00000080,	// Do not try to convert this parameter buffer On Big-Endian processors (strings, void buffer have this flags)
			NHPARAMETERTYPE_TOSAVE		 =	   0x00000100,	// Temporary flag set in CKFile::EndSave(). This parameter type is to be saved in the SaveData callback of managers. Used in Parameter Manager (used user's flags & enums)
} NH_PARAMETERTYPE_FLAGS;

//----------------------------------------------------------//
//		Standard Parameter GUIDs							//
//----------------------------------------------------------//

constexpr Uuid64 NHP_GUID_NONE(0x1cb10760, 0x419f50c5);
constexpr Uuid64 NHP_GUID_VOIDBUF(0x4d082c90, 0x0c8339a2);
constexpr Uuid64 NHP_GUID_FLOAT(0x47884c3f, 0x432c2c20);
constexpr Uuid64 NHP_GUID_ANGLE(0x11262cf5, 0x30b0233a);
constexpr Uuid64 NHP_GUID_PERCENTAGE(0xf3c84b4e, 0x0ffacc34);
constexpr Uuid64 NHP_GUID_FLOATSLIDER(0x429d42cf, 0x211c0cc2);
constexpr Uuid64 NHP_GUID_INT(0x5a5716fd, 0x44e276d7);
constexpr Uuid64 NHP_GUID_KEY(0xfa6e1bdd, 0x62d2abd7);
constexpr Uuid64 NHP_GUID_BOOL(0x1ad52a8e, 0x5e741920);
constexpr Uuid64 NHP_GUID_STRING(0x6bd010e2, 0x115617ea);
constexpr Uuid64 NHP_GUID_RECT(0x7ab20d20, 0x693044a9);
constexpr Uuid64 NHP_GUID_VECTOR(0x48824eae, 0x2fe47960);
constexpr Uuid64 NHP_GUID_VECTOR4(0x6c439ee0, 0x2fe47960);
constexpr Uuid64 NHP_GUID_2DVECTOR(0x4efcb34a, 0x6079e42f);
constexpr Uuid64 NHP_GUID_QUATERNION(0x06c439ee, 0x45b50fc2);
constexpr Uuid64 NHP_GUID_EULERANGLES(0x13b01b3c, 0x1942583e);
constexpr Uuid64 NHP_GUID_MATRIX(0x643f046e, 0x65211b71);
constexpr Uuid64 NHP_GUID_COLOR(0x57d42fee, 0x7cbb3b91);
constexpr Uuid64 NHP_GUID_BOX(0x668649c8, 0x283e2ee1);
constexpr Uuid64 NHP_GUID_OBJECTARRAY(0x71df7142, 0xc437133a);
constexpr Uuid64 NHP_GUID_OBJECT(0x30ec20ab, 0x6df6517d);
constexpr Uuid64 NHP_GUID_BEOBJECT(0x71d80779, 0x402f42f3);

constexpr Uuid64 NHP_GUID_ENUMS(0x4dd37f6b, 0x240f5fa2);
constexpr Uuid64 NHP_GUID_STRUCTS(0x38df566a, 0x30f77b9e);
constexpr Uuid64 NHP_GUID_FLAGS(0x2b49245d, 0x582d60d6);

//----------------------------------------------------------////
// Data Array Flags and Enums              								////
//----------------------------------------------------------////

/************************************************
{filename:NH_BINARYOPERATOR}
Summary: Available operations between colums of a DataArray

See Also: CKDataArray::ColumnTransform, CKDataArray::ColumnsOperate
************************************************/
typedef enum NH_BINARYOPERATOR{
	NHADD	= 1,	// Addition
	NHSUB	= 2,	// Substraction
	NHMUL	= 3,	// Multiplication	
	NHDIV	= 4		// Division
} NH_BINARYOPERATOR;

/************************************************
{filename:NH_COMPOPERATOR}
Summary: Available comparisons between colums of a DataArray
Remarks:

See Also: CKDataArray::CreateGroup, CKDataArray::FindLine
************************************************/
typedef enum NH_COMPOPERATOR{
	NHEQUAL			= 1,	
	NHNOTEQUAL		= 2,
	NHLESSER		= 3,
	NHLESSEREQUAL	= 4,
	NHGREATER		= 5,
	NHGREATEREQUAL	= 6
} NH_COMPOPERATOR;


//----------------------------------------------------------//
//		Class Identifier List								//
//----------------------------------------------------------//

#define  NHCID_OBJECT					1		
	#define  NHCID_PARAMETERIN				2	
	#define  NHCID_PARAMETEROPERATION		4	
	#define  NHCID_STATE					5	
	#define  NHCID_BEHAVIORLINK				6	
	#define  NHCID_BEHAVIOR					8	
	#define  NHCID_BEHAVIORIO				9	
	#define  NHCID_RENDERCONTEXT			12	
	#define  NHCID_KINEMATICCHAIN			13	
	#define  NHCID_SCENEOBJECT				11		
		#define  NHCID_OBJECTANIMATION			15	
		#define  NHCID_ANIMATION				16	
			#define  NHCID_KEYEDANIMATION		18	
		#define  NHCID_BEOBJECT					19	
			#define	 NHCID_DATAARRAY			52	
			#define  NHCID_SCENE				10	
			#define  NHCID_LEVEL				21	
			#define  NHCID_PLACE				22	
			#define  NHCID_GROUP				23	
			#define  NHCID_SOUND				24	
				#define  NHCID_WAVESOUND		25	
				#define  NHCID_MIDISOUND		26	
			#define  NHCID_MATERIAL				30	
			#define  NHCID_TEXTURE				31	
			#define  NHCID_MESH					32	
				#define NHCID_PATCHMESH			53	
			#define  NHCID_RENDEROBJECT			47	
				#define  NHCID_2DENTITY			27	
					#define  NHCID_SPRITE		28	
					#define  NHCID_SPRITETEXT	29	
			#define  NHCID_3DENTITY				33	
				#define NHCID_GRID				50	
				#define  NHCID_CURVEPOINT		36	
				#define  NHCID_SPRITE3D			37	
				#define  NHCID_CURVE			43	
				#define  NHCID_CAMERA			34	
					#define  NHCID_TARGETCAMERA	35	
				#define  NHCID_LIGHT			38	
					#define  NHCID_TARGETLIGHT	39	
				#define  NHCID_CHARACTER		40	
				#define  NHCID_3DOBJECT			41	
					#define  NHCID_BODYPART		42	
	#define  NHCID_PARAMETER				46		
		#define  NHCID_PARAMETERLOCAL		45	
			#define  NHCID_PARAMETERVARIABLE	55	
		#define  NHCID_PARAMETEROUT			3	
	#define NHCID_INTERFACEOBJECTMANAGER	48	
	#define NHCID_CRITICALSECTION			49	
	#define NHCID_LAYER						51	
	#define NHCID_PROGRESSIVEMESH			54	
	#define NHCID_SYNCHRO					20	

#ifdef NH_GUI
	#include "enums_gui.h"
#endif