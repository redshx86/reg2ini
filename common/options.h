// -------------------------------------------------------------------------------------------------

#pragma once
#include <windows.h>

// -------------------------------------------------------------------------------------------------

#define REG2INI_TERMINATE_ON_FAIL			0x00000001

#define REG2INI_VECTORED_HOOKS				0x00000002
#define REG2INI_FORCED_HOOKS				0x00000004

#define REG2INI_INJECT_CHILDS				0x00000008
#define REG2INI_SHELL_MODE					0x00000010

#define REG2INI_HOOK_REGISTRY				0x00010000
#define REG2INI_HOOK_FILESYSTEM				0x00020000

// -------------------------------------------------------------------------------------------------

#define REG2INI_DEFAULT_FLAGS			REG2INI_TERMINATE_ON_FAIL | REG2INI_VECTORED_HOOKS | \
										REG2INI_HOOK_REGISTRY

#define REG2INI_DEFAULT_SAVETIMEOUT		200

// -------------------------------------------------------------------------------------------------

typedef struct __REG2INI_OPTIONS {

	ULONG Size;

	ULONG Flags;

	ULONG IniSaveTimeout;

	ULONG DataDirectoryOffset;
	ULONG DataDirectorySize;

	ULONG ConfigNameOffset;
	ULONG ConfigNameSize;

	ULONG NoInjectOffset;
	ULONG NoInjectSize;

} REG2INI_OPTIONS;

// -------------------------------------------------------------------------------------------------

int Reg2IniOptionsInitialize(REG2INI_OPTIONS **pOptions, WCHAR *DataDirectory, WCHAR *ConfigName, WCHAR **NoInjectItems, ULONG NoInjectCount);
int Reg2IniOptionsForChildProcess(REG2INI_OPTIONS **pOptions, REG2INI_OPTIONS *CurOptions);

// -------------------------------------------------------------------------------------------------
