// -------------------------------------------------------------------------------------------------

#pragma once
#include <windows.h>
#include <tchar.h>
#include <malloc.h>
#include <stdio.h>
#include "hooks.h"
#include "../../common/options.h"
#include "../../common/dllexprt.h"

// -------------------------------------------------------------------------------------------------

typedef struct __INJECTED_BLOCK {

	void *ptrLoadLibraryW;				// 0
	void *ptrGetProcAddress;			// 4

	WCHAR ModuleToLoadPath[MAX_PATH];	// 8
	char ProcToCallName[32];			// 210

	char nops[16];						// 230
	char Shellcode[128];				// 240

	void *ReturnAddress;				// 2C0
	char Options[];						// 2C4

} INJECTED_BLOCK;

// -------------------------------------------------------------------------------------------------

int InjectInit(HMODULE hInst);
int InjectChilds(REG2INI_OPTIONS * Options, WCHAR * IgnoreList);
void InjectCleanup();

// -------------------------------------------------------------------------------------------------
