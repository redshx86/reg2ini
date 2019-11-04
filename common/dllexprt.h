// -------------------------------------------------------------------------------------------------

#pragma once
#include <windows.h>
#include "options.h"

// -------------------------------------------------------------------------------------------------

#ifdef REG2INI_HOOK_EXPORTS
#	define DLLEXPORT __declspec(dllexport)
#else
#	define DLLEXPORT __declspec(dllimport)
#endif

// -------------------------------------------------------------------------------------------------

DLLEXPORT
DWORD Reg2IniEnableDebugPrivilege();

DLLEXPORT
DWORD Reg2IniCreateProcess(WCHAR *Command, STARTUPINFOW *StartupInfo, REG2INI_OPTIONS * Options);

DLLEXPORT
int Reg2IniInstallHooks(REG2INI_OPTIONS * Options);

// -------------------------------------------------------------------------------------------------
