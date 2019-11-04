// -------------------------------------------------------------------------------------------------

#pragma once
#include <windows.h>
#include <malloc.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <tchar.h>
#include "hooks.h"
#include "../../common/memstr.h"

// -------------------------------------------------------------------------------------------------

#define FSR_SUBDIR_SYSTEMDRIVE			L"c"
#define FSR_SUBDIR_WINDOWS				L"windows"
#define FSR_SUBDIR_SYSTEM				L"system32"
#define FSR_SUBDIR_TEMP					L"temp"
#define FSR_SUBDIR_USER					L"user"

// -------------------------------------------------------------------------------------------------

typedef enum __FSR_STATUS {
	FSR_OK,
	FSR_ERROR,
	FSR_NOMEM,
	FSR_NOTFOUND,
	FSR_EXISTS,
	FSR_BADPATH,
	FSR_DEVICEPATH,
	FSR_PROCNOTFOUND,
} FSR_STATUS;

// -------------------------------------------------------------------------------------------------

typedef enum __FSR_NODE_TYPE {
	FSR_NODE_INSIGNIFICANT,
	FSR_NODE_REDIRECT,
	FSR_NODE_NO_REDIRECT,
} FSR_NODE_TYPE;

// -------------------------------------------------------------------------------------------------

typedef struct __FSR_NODE {
	struct __FSR_NODE *ParentNode;
	FSR_NODE_TYPE Type;
	WCHAR *Name;
	R2I_WSTR *TargetPath;
	int IsRootNode;
	DWORD SubnodeCount, SubnodeMaxCount;
	struct __FSR_NODE **Subnode;
} FSR_NODE;

// -------------------------------------------------------------------------------------------------

typedef enum __FSR_PATH_TYPE {
	FSR_PATH_UNKNOWN,
	FSR_PATH_ABSOLUTE,
	FSR_PATH_ABSOLUTE_LONG,
	FSR_PATH_NETWORK,
	FSR_PATH_NETWORK_LONG,
	FSR_PATH_CURDRIVE_RELATIVE,
	FSR_PATH_CURDIR_RELATIVE,
	FSR_PATH_DRIVE_RELATIVE,
	FSR_PATH_DEVICE,
} FSR_PATH_TYPE;

// -------------------------------------------------------------------------------------------------

typedef struct __FSR_SEARCH_ENTRY {
	DWORD attributes;
	FILETIME ctime;
	FILETIME atime;
	FILETIME mtime;
	DWORD fsizelow;
	DWORD fsizehigh;
	WCHAR *name;
	WCHAR *shortname;
} FSR_SEARCH_ENTRY;

// -------------------------------------------------------------------------------------------------

typedef struct __FSR_SEARCH {
	struct __FSR_SEARCH *next;
	HANDLE hsearch;
	DWORD entrymaxcnt, entrycnt, entrypos;
	FSR_SEARCH_ENTRY **entry;
} FSR_SEARCH;

// -------------------------------------------------------------------------------------------------

typedef struct __FSR_SEARCH_PATH_LIST {
	struct __FSR_SEARCH_PATH_LIST *next;
	WCHAR *source_name;
	WCHAR *target_name;
} FSR_SEARCH_PATH_LIST;

// -------------------------------------------------------------------------------------------------

typedef struct __FSR_DATA {
	int HooksEnabled;
	FSR_NODE *RootNode;
	R2I_WSTR *DataDir;
	R2I_WSTR *WinDir;
	R2I_WSTR *ProgramStartPath;
	FSR_SEARCH *searches;
	FSR_SEARCH_PATH_LIST *SrchPthItems;
	CRITICAL_SECTION searches_lock;
} FSR_DATA;

// -------------------------------------------------------------------------------------------------

int FsrInit(WCHAR *DataDir);
void FsrCleanup();

// -------------------------------------------------------------------------------------------------
