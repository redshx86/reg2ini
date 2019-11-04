// -------------------------------------------------------------------------------------------------

#include "options.h"
#include <string.h>
#include <tchar.h>
#include <malloc.h>

// -------------------------------------------------------------------------------------------------

int Reg2IniOptionsInitialize(REG2INI_OPTIONS **pOptions, WCHAR *DataDirectory, WCHAR *ConfigName, WCHAR **NoInjectItems, ULONG NoInjectCount)
{
	REG2INI_OPTIONS *Options;
	ULONG DirectorySize, ConfigNameSize, OptionsSize, NoInjectSize, offset, i;
	WCHAR *NoInject;
	char *cursor;

	NoInjectSize = sizeof(WCHAR);
	for(i = 0; i < NoInjectCount; i++)
		NoInjectSize += (wcslen(NoInjectItems[i]) + 1) * sizeof(WCHAR);

	DirectorySize = (wcslen(DataDirectory) + 1) * sizeof(WCHAR);
	ConfigNameSize = (wcslen(ConfigName) + 1) * sizeof(WCHAR);

	OptionsSize = sizeof(REG2INI_OPTIONS) + DirectorySize + ConfigNameSize + NoInjectSize;
	if((Options = malloc(OptionsSize)) == NULL)
		return 0;

	cursor = (void*)Options;
	cursor += sizeof(REG2INI_OPTIONS);
	offset = sizeof(REG2INI_OPTIONS);

	memset(Options, 0, sizeof(REG2INI_OPTIONS));
	Options->Size = OptionsSize;
	Options->Flags = REG2INI_DEFAULT_FLAGS;
	Options->IniSaveTimeout = REG2INI_DEFAULT_SAVETIMEOUT;

	Options->NoInjectOffset = offset;
	Options->NoInjectSize = NoInjectSize;
	offset += NoInjectSize;
	NoInject = (void*)cursor;
	cursor += NoInjectSize;

	for(i = 0; i < NoInjectCount; i++) {
		wcscpy(NoInject, NoInjectItems[i]);
		NoInject += wcslen(NoInjectItems[i]) + 1;
	}
	*NoInject = 0;

	Options->DataDirectoryOffset = offset;
	Options->DataDirectorySize = DirectorySize;
	offset += DirectorySize;
	memcpy(cursor, DataDirectory, DirectorySize);
	cursor += DirectorySize;

	Options->ConfigNameOffset = offset;
	Options->ConfigNameSize = ConfigNameSize;
	offset += ConfigNameSize;
	memcpy(cursor, ConfigName, ConfigNameSize);
	cursor += ConfigNameSize;

	*pOptions = Options;

	return 1;
}

// -------------------------------------------------------------------------------------------------

int Reg2IniOptionsForChildProcess(REG2INI_OPTIONS **pOptions, REG2INI_OPTIONS *CurOptions)
{
	REG2INI_OPTIONS *options;

	if((options = malloc(CurOptions->Size)) == NULL)
		return 0;

	memcpy(options, CurOptions, CurOptions->Size);
	options->Flags &= ~REG2INI_SHELL_MODE;

	*pOptions = options;
	return 1;
}

// -------------------------------------------------------------------------------------------------
