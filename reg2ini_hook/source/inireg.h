// -------------------------------------------------------------------------------------------------

#pragma once
#include <windows.h>
#include "ini.h"
#include "hookproc.h"
#include "hooks.h"

// -------------------------------------------------------------------------------------------------

#define INIREG_HANDLE_LISTS			64

// -------------------------------------------------------------------------------------------------

typedef struct __INIREG_HANDLE INIREG_HANDLE;

typedef struct __INIREG_HANDLE {

	INIREG_HANDLE *Next;

	int FakeHandle;
	int CloseIdHandle;
	HANDLE IdHandle;
	HANDLE SysHandle;

	WCHAR *NodePath;
	WCHAR *FullPath;

	ULONG EnumValueIndex;
	ULONG EnumValueIndexNode;
	ULONG EnumValueIndexReg;
	ULONG EnumKeyIndex;
	ULONG EnumKeyIndexNode;
	ULONG EnumKeyIndexReg;
};

// -------------------------------------------------------------------------------------------------

typedef struct __INIREG_DATA {

	int HooksEnabled;

	CRITICAL_SECTION Lock;

	INIREG_HANDLE *HandleList[INIREG_HANDLE_LISTS];

	HANDLE SaveThread;
	HANDLE SaveRequest;
	HANDLE StopRequest;
	DWORD SaveRequestTick;
	DWORD SaveTimeout;

	WCHAR *CurrentUserPath;
	WCHAR *ClassesRootPath;
	WCHAR *MachinePath;
	WCHAR *UsersPath;
	ULONG CurrentUserPathLen;
	ULONG ClassesRootPathLen;
	ULONG MachinePathLen;
	ULONG UsersPathLen;

} INIREG_DATA;

// -------------------------------------------------------------------------------------------------

INI_STATUS IniRegInitialize(WCHAR *Filename, ULONG SaveTimeout);
void IniRegCleanup();

// -------------------------------------------------------------------------------------------------
