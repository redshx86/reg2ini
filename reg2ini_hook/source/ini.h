// -------------------------------------------------------------------------------------------------

#pragma once
#include <windows.h>
#include <malloc.h>
#include <string.h>
#include <tchar.h>
#include <stdlib.h>
#include <stdio.h>

// -------------------------------------------------------------------------------------------------

#define INI_LOCK_LEN		0xffffffff

// -------------------------------------------------------------------------------------------------

typedef enum __INI_STATUS {
	INI_OK,
	INI_ERROR,
	INI_BADCALL,
	INI_BADTYPE,
	INI_NOMEM,
	INI_NOTFOUND,
	INI_FOUND,
	INI_CANTOPEN,
} INI_STATUS;

// -------------------------------------------------------------------------------------------------

typedef enum __INI_NODE_STATE {
	INI_NODE_DELETED,
	INI_NODE_TRANSPARENT,
	INI_NODE_OPAQUE,

	INI_NODE_STATE_COUNT,
} INI_NODE_STATE;

// -------------------------------------------------------------------------------------------------

typedef enum __INI_VALUE_TYPE {
	INI_VALUE_DELETED,
	INI_VALUE_NOTYPE,
	INI_VALUE_BINARY,
	INI_VALUE_DWORD,
	INI_VALUE_QWORD,
	INI_VALUE_STRING,
	INI_VALUE_STRING_ENV,
	INI_VALUE_STRING_MULTI,

	INI_VALUE_TYPE_COUNT,
} INI_VALUE_TYPE;

// -------------------------------------------------------------------------------------------------

typedef struct __INI_NODE INI_NODE;
typedef struct __INI_VALUE INI_VALUE;

// -------------------------------------------------------------------------------------------------

typedef struct __INI_LIST {
	ULONG MaxCount;
	ULONG Count;
	union {
		INI_NODE **Node;
		INI_VALUE **Value;
		void **Item;
	};
} INI_LIST;

// -------------------------------------------------------------------------------------------------

struct __INI_NODE {
	INI_NODE *ParentNode;
	INI_NODE_STATE State;
	WCHAR *Name;
	int IsTouched;
	INI_LIST Subnodes;
	INI_LIST Values;
};

// -------------------------------------------------------------------------------------------------

struct __INI_VALUE {
	INI_VALUE_TYPE Type;
	WCHAR *Name;
	ULONG MaxDataSize;
	ULONG DataSize;
	void *Data;
	int IsTouched;
};

// -------------------------------------------------------------------------------------------------

typedef struct __INI_DATA {
	WCHAR *LockFilename;
	HANDLE hFile;
	INI_NODE RootNode;
	int Unsaved;
	FILETIME LastSaved;
	WCHAR *Buffer;
	ULONG BufLen;
	ULONG BufUsed;
} INI_DATA;

// -------------------------------------------------------------------------------------------------

INI_STATUS IniInitialize(WCHAR *Filename);
void IniCleanup();

INI_STATUS IniNodeStateInfo(WCHAR *NodePath, int *pIsExists, int *pIsOpaque);
INI_STATUS IniNodeCreate(INI_NODE **pNode, INI_NODE *ParentNode, WCHAR *NodeName);
INI_STATUS IniNodeCreateByPath(INI_NODE **pNode, WCHAR *NodePath);
INI_STATUS IniNodeDelete(INI_NODE *Node);

INI_STATUS IniNodeCopy(INI_NODE *DstNode, INI_NODE *SrcNode);
INI_NODE *IniNodeLookup(INI_NODE *ParentNode, WCHAR *NodeName);

INI_STATUS IniValueDelete(INI_NODE *Node, WCHAR *Name);
INI_STATUS IniValueSet(INI_NODE *Node, WCHAR *Name, INI_VALUE_TYPE Type, void *Data, ULONG DataSize);
INI_STATUS IniValueSetRaw(INI_NODE *Node, WCHAR *Name, INI_VALUE_TYPE Type, void *Data, ULONG DataSize);

INI_VALUE *IniValueLookup(INI_NODE *Node, WCHAR *Name);

void IniPrintNode(INI_NODE *Node, ULONG level);
void IniPrint();

INI_STATUS IniSave();
INI_STATUS IniLoad();

ULONGLONG IniGetLastSaved();

// -------------------------------------------------------------------------------------------------
