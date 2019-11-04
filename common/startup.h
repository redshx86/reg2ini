// -------------------------------------------------------------------------------------------------

#pragma once
#include <windows.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include "options.h"

// -------------------------------------------------------------------------------------------------

typedef struct __REG2INI_SETUP {
	int show_help;
	int def_flags;
	DWORD flags;
	DWORD save_timeout;
	WCHAR *prog_full_name;
	WCHAR *config_full_name;
	WCHAR *data_dir_full_name;
	WCHAR *command_line;
	WCHAR **no_inject_items;
	DWORD no_inject_count;
	DWORD no_inject_max;
} REG2INI_SETUP;

// -------------------------------------------------------------------------------------------------

DWORD StpFindExecutableFile(WCHAR **pFullName, WCHAR *FileName);
DWORD StpGetDataPath(WCHAR **pFullFileName, WCHAR *ExecutableFileName, WCHAR *FileName, WCHAR *DefFileName, WCHAR *DefExtension);
DWORD StpUpdatePath(WCHAR *DataDir);

void StpSetupFree(REG2INI_SETUP *setup);

// -------------------------------------------------------------------------------------------------
