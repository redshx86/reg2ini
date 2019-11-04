// -------------------------------------------------------------------------------------------------

#include "startup.h"

// -------------------------------------------------------------------------------------------------

static WCHAR *StpAppendPath(WCHAR *BasePath, WCHAR *FileName)
{
	DWORD BasePathLen, FileNameLen;
	WCHAR *Buffer, *Cursor;

	BasePathLen = wcslen(BasePath);
	FileNameLen = wcslen(FileName);

	if((Cursor = Buffer = malloc((BasePathLen + FileNameLen + 2) * sizeof(WCHAR))) == NULL)
		return NULL;

	wcscpy(Cursor, BasePath);
	Cursor += BasePathLen;

	if( (BasePathLen > 0) && (BasePath[BasePathLen-1] != L'\\') && (BasePath[BasePathLen-1] != L'/') )
		*(Cursor++) = L'\\';

	wcscpy(Cursor, FileName);

	return Buffer;
}

// -------------------------------------------------------------------------------------------------

DWORD StpFindExecutableFile(WCHAR **pFullName, WCHAR *FileName)
{
	WCHAR *FullName = NULL;
	DWORD FullNameBufLen, FullNameLen;
	void *temp;

	FullNameBufLen = MAX_PATH;
	if((FullName = malloc(FullNameBufLen * sizeof(WCHAR))) == NULL) {
		return ERROR_NOT_ENOUGH_MEMORY;
	}

	FullNameLen = SearchPath(NULL, FileName, L".exe", FullNameBufLen, FullName, NULL);
	if(FullNameLen > FullNameBufLen) {
		FullNameBufLen = FullNameLen;
		if((temp = realloc(FullName, FullNameBufLen * sizeof(WCHAR))) == NULL) {
			free(FullName);
			return ERROR_NOT_ENOUGH_MEMORY;
		}
		FullName = temp;
	}
	FullNameLen = SearchPath(NULL, FileName, L".exe", FullNameBufLen, FullName, NULL);
	if(FullNameLen == 0) {
		return GetLastError();
	}

	*pFullName = FullName;
	return ERROR_SUCCESS;
}

// -------------------------------------------------------------------------------------------------

DWORD StpGetDataPath(WCHAR **pFullFileName, WCHAR *ExecutableFileName, WCHAR *FileName, WCHAR *DefFileName, WCHAR *DefExtension)
{
	DWORD LenNoFilename, LenNoExtension, FullNameBufLen, FullNameLen;
	WCHAR *FileNamepart, *ExtPart, *RawPath, *RawPathBuf, *Buffer, *temp;
	int FileNameRelative;

	FileNamepart = ExecutableFileName;
	while((temp = wcspbrk(FileNamepart, L"\\/")) != NULL)
		FileNamepart = temp + 1;

	LenNoFilename = FileNamepart - ExecutableFileName;

	if(FileName == NULL) {
		// change extension to default
		if(DefExtension != NULL) {
			if((ExtPart = wcsrchr(FileNamepart, L'.')) != NULL) {
				LenNoExtension = ExtPart - ExecutableFileName;
			} else {
				LenNoExtension = wcslen(ExecutableFileName);
			}
			FullNameBufLen = LenNoExtension + wcslen(DefExtension) + 1;
			if((Buffer = malloc(FullNameBufLen * sizeof(WCHAR))) == NULL)
				return ERROR_NOT_ENOUGH_MEMORY;
			wcsncpy(Buffer, ExecutableFileName, LenNoExtension);
			wcscpy(Buffer + LenNoExtension, DefExtension);
			*pFullFileName = Buffer;
			return ERROR_SUCCESS;
		}
		// change fileFileName to default
		FullNameBufLen = LenNoFilename + wcslen(DefFileName) + 1;
		if((Buffer = malloc(FullNameBufLen * sizeof(WCHAR))) == NULL)
			return ERROR_NOT_ENOUGH_MEMORY;
		wcsncpy(Buffer, ExecutableFileName, LenNoFilename);
		wcscpy(Buffer + LenNoFilename, DefFileName);
		*pFullFileName = Buffer;
		return ERROR_SUCCESS;
	}

	// relative pathFileName magic check
	FileNameRelative = ((FileName[0] == 0) || ((FileName[0] != L'\\') && (FileName[1] != L':')));

	// combine directory and fileFileName
	if(FileNameRelative) {
		FullNameBufLen = LenNoFilename + wcslen(FileName) + 1;
		if((RawPath = RawPathBuf = malloc(FullNameBufLen * sizeof(WCHAR))) == NULL)
			return ERROR_NOT_ENOUGH_MEMORY;
		wcsncpy(RawPath, ExecutableFileName, LenNoFilename);
		wcscpy(RawPath + LenNoFilename, FileName);
	} else {
		RawPathBuf = NULL;
		RawPath = FileName;
	}

	// normalize full fileFileName
	FullNameBufLen = MAX_PATH;
	if((Buffer = malloc(FullNameBufLen * sizeof(WCHAR))) == NULL) {
		free(RawPathBuf);
		return ERROR_NOT_ENOUGH_MEMORY;
	}
	FullNameLen = GetFullPathName(RawPath, FullNameBufLen, Buffer, NULL);
	if(FullNameLen > FullNameBufLen) {
		FullNameBufLen = FullNameLen;
		if((temp = realloc(Buffer, FullNameBufLen * sizeof(WCHAR))) == NULL) {
			free(RawPathBuf);
			return ERROR_NOT_ENOUGH_MEMORY;
		}
		Buffer = temp;
		FullNameLen = GetFullPathName(RawPath, FullNameBufLen, Buffer, NULL);
	}
	free(RawPathBuf);
	if(FullNameLen == 0) {
		return GetLastError();
	}

	*pFullFileName = Buffer;
	return ERROR_SUCCESS;
}

// -------------------------------------------------------------------------------------------------

void StpSetupFree(REG2INI_SETUP *setup)
{
	DWORD i;

	for(i = 0; i < setup->no_inject_count; ++i)
		free(setup->no_inject_items[i]);

	free(setup->prog_full_name);
	free(setup->config_full_name);
	free(setup->data_dir_full_name);
	free(setup->command_line);
	free(setup->no_inject_items);
}

// -------------------------------------------------------------------------------------------------

WCHAR *StpGetEnv(WCHAR *Name)
{
	DWORD BufLen, VarLen;
	WCHAR *Buffer, *temp;

	BufLen = 1024;
	if((Buffer = malloc(BufLen * sizeof(WCHAR))) == NULL)
		return NULL;

	VarLen = GetEnvironmentVariable(Name, Buffer, BufLen);
	if(VarLen > BufLen) {
		BufLen = VarLen;
		if((temp = realloc(Buffer, BufLen * sizeof(WCHAR))) == NULL) {
			free(Buffer);
			return NULL;
		}
		Buffer = temp;
		VarLen = GetEnvironmentVariable(Name, Buffer, BufLen);
	}
	if( (VarLen == 0) || (VarLen >= BufLen) ) {
		free(Buffer);
		return NULL;
	}

	return Buffer;
}

// -------------------------------------------------------------------------------------------------

#define PATH_PREPEND_COUNT			2
#define PATH_PREPEND_SYSTEMROOT		L"system32"
#define PATH_PREPEND_WINDIR			L"windows"

DWORD StpUpdatePath(WCHAR *DataDir)
{
	DWORD Error;
	WCHAR *Buffer = NULL, *Cursor;
	WCHAR *Path, *PathBuffer = NULL;
	WCHAR *Prepend[PATH_PREPEND_COUNT];
	DWORD PrependCount = 0, BufferLen, i, UsedQuote;

	if((Path = PathBuffer = StpGetEnv(L"PATH")) == NULL)
		Path = L"";

	if((Prepend[PrependCount] = StpAppendPath(DataDir, PATH_PREPEND_SYSTEMROOT)) == NULL) {
		Error = ERROR_NOT_ENOUGH_MEMORY;
		goto __cleanup;
	}
	PrependCount++;

	if((Prepend[PrependCount] = StpAppendPath(DataDir, PATH_PREPEND_WINDIR)) == NULL) {
		Error = ERROR_NOT_ENOUGH_MEMORY;
		goto __cleanup;
	}
	PrependCount++;

	BufferLen = wcslen(Path) + 1;
	for(i = 0; i < PrependCount; ++i) {
		BufferLen += wcslen(Prepend[i]) + 3;
	}

	if((Cursor = Buffer = malloc(BufferLen * sizeof(WCHAR))) == NULL) {
		Error = ERROR_NOT_ENOUGH_MEMORY;
		goto __cleanup;
	}

	for(i = 0; i < PrependCount; ++i) {
		UsedQuote = 0;
		if(i > 0) *(Cursor++) = L';';
		//if(wcspbrk(Prepend[i], L";% ")) {
		//	UsedQuote = 1;
		//	*(Cursor++) = L'\"';
		//}
		wcscpy(Cursor, Prepend[i]);
		Cursor += wcslen(Prepend[i]);
		//if(UsedQuote) {
		//	*(Cursor++) = L'\"';
		//}
	}

	if(*Path != 0) {
		*(Cursor++) = L';';
		wcscpy(Cursor, Path);
		Cursor += wcslen(Path);
	}

	*Cursor = 0;

	if(!SetEnvironmentVariable(L"PATH", Buffer)) {
		Error = GetLastError();
		goto __cleanup;
	}

	Error = ERROR_SUCCESS;

__cleanup:
	free(Buffer);
	free(PathBuffer);
	for(i = 0; i < PrependCount; ++i)
		free(Prepend[i]);
	return Error;
}

// -------------------------------------------------------------------------------------------------
