// -------------------------------------------------------------------------------------------------

#include <locale.h>
#include <windows.h>
#include <direct.h>
#include "hooks.h"
#include "inireg.h"
#include "inject.h"
#include "fsredir.h"
#include "../../common/options.h"
#include "../../common/dllexprt.h"
#include "../../common/memstr.h"

// -------------------------------------------------------------------------------------------------

static WCHAR *GetProcessFileName()
{
	WCHAR *Temp;
	R2I_WSTR *Path;
	DWORD Length;

	if((Path = WstrAlloc(MAX_PATH)) == NULL)
		return NULL;

	while(1) {

		Length = GetModuleFileName(NULL, Path->Buf, Path->BufLength);

		if( (Length == 0) || (Length > Path->BufLength) ) {
			free(Path);
			return NULL;
		}

		if(Length < Path->BufLength)
			break;

		if(!WstrExpand(&Path, (Path->BufLength * 3) / 2)) {
			free(Path);
			return NULL;
		}

	}

	if((Temp = _wcsrpbrk(Path->Buf, L"\\/")) != NULL) {
		Temp++;
	} else {
		Temp = Path->Buf;
	}

	Temp = wcsdup(Temp);

	free(Path);

	return Temp;
}

// -------------------------------------------------------------------------------------------------

static void ShowMessageBox(WCHAR *Message)
{
	DWORD ProcId;
	HMODULE user32;
	WCHAR *FileName, *Buf;
	int (__stdcall *MsgBox)(HWND, WCHAR*, WCHAR*, int);

	if((user32 = GetModuleHandle(L"user32.dll")) == NULL) {
		if((user32 = LoadLibrary(L"user32.dll")) == NULL)
			return;
	}

	MsgBox = (void*)GetProcAddress(user32, "MessageBoxW");
	if(MsgBox == NULL)
		return;

	if((FileName = GetProcessFileName()) == NULL) {
		MsgBox(NULL, Message, L"Reg2Ini error", MB_ICONEXCLAMATION|MB_OK);
		return;
	}

	ProcId = GetCurrentProcessId();

	if((Buf = malloc((wcslen(FileName) + 40) * sizeof(WCHAR))) == NULL) {
		free(FileName);
		return;
	}

	swprintf(Buf, L"Reg2Ini error (%s:%u)", FileName, ProcId);
	MsgBox(NULL, Message, Buf, MB_ICONEXCLAMATION|MB_OK);

	free(Buf);
	free(FileName);

}

// -------------------------------------------------------------------------------------------------

static void ShowErrorMsg(WCHAR *cause, DWORD errorcode)
{
	WCHAR *message, *buf;
	DWORD status;

	status = FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER|
		FORMAT_MESSAGE_IGNORE_INSERTS|
		FORMAT_MESSAGE_FROM_SYSTEM,
		NULL,
		errorcode,
		0,
		(void*)&message,
		0,
		NULL);

	if(status != 0) {
		if((buf = malloc((wcslen(cause) + wcslen(message) + 10) * sizeof(WCHAR))) != NULL) {
			swprintf(buf, L"%s\r\n\r\n%s", cause, message);
			ShowMessageBox(buf);
			free(buf);
		}
		LocalFree(message);
		return;
	}

	if((buf = malloc((wcslen(cause) + 20) * sizeof(WCHAR))) != NULL) {
		swprintf(buf, L"%s\r\n\r\nError code %u", cause, errorcode);
		ShowMessageBox(buf);
		free(buf);
	}
}

// -------------------------------------------------------------------------------------------------

DWORD Reg2IniEnableDebugPrivilege()
{
	DWORD error;
	HANDLE hToken;
	TOKEN_PRIVILEGES Privileges;
	HMODULE advapi32;
	TypeOpenProcessToken OpenToken;
	TypeLookupPrivilegeValue LookupPriv;
	TypeAdjustTokenPrivileges SetPriv;

	if((advapi32 = LoadLibrary(_T("advapi32.dll"))) == NULL) {
		return GetLastError();
	}

	OpenToken = (void*)GetProcAddress(advapi32, "OpenProcessToken");
	LookupPriv = (void*)GetProcAddress(advapi32, "LookupPrivilegeValueW");
	SetPriv = (void*)GetProcAddress(advapi32, "AdjustTokenPrivileges");
	if( (OpenToken == NULL) || (LookupPriv == NULL) || (SetPriv == NULL) ) {
		error = GetLastError();
		FreeLibrary(advapi32);
		return error;
	}

	if(!OpenToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
		error = GetLastError();
	} else {
		Privileges.PrivilegeCount = 1;
		Privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
		if(!LookupPriv(NULL, SE_DEBUG_NAME, &(Privileges.Privileges[0].Luid))) {
			error = GetLastError();
		} else {
			if(!SetPriv(hToken, FALSE, &Privileges, sizeof(Privileges), NULL, NULL)) {
				error = GetLastError();
			} else {
				error = ERROR_SUCCESS;
			}
		}
		CloseHandle(hToken);
	}

	FreeLibrary(advapi32);

	return error;
}

// -------------------------------------------------------------------------------------------------

int Reg2IniInstallHooks(REG2INI_OPTIONS * Options)
{
	INI_STATUS ini_status;
	FSR_STATUS fsr_status;
	CODE_HOOK_ERROR hook_error;
	DWORD error;
	REG2INI_OPTIONS * ChildOptions = NULL;
	WCHAR *ConfigName, *DataDirName, *message;
	WCHAR *IgnoreList;
	int VectoredHooks, ForcedHooks;
	int HookRegistry, HookFilesystem;
	int ShellMode, InjectChildsEnabled, TerminateOnError;

	ShellMode = (Options->Flags & REG2INI_SHELL_MODE) != 0;
	InjectChildsEnabled = (Options->Flags & REG2INI_INJECT_CHILDS) != 0;
	TerminateOnError = (Options->Flags & REG2INI_TERMINATE_ON_FAIL) != 0;

	// initialize hook handlers (filesystem, registry)
	if( !ShellMode ) {

		HookRegistry = (Options->Flags & REG2INI_HOOK_REGISTRY) != 0;
		HookFilesystem = (Options->Flags & REG2INI_HOOK_FILESYSTEM) != 0;

		// initialize registry handlers
		if(HookRegistry)
		{
			ConfigName = (void*)((char*)Options + Options->ConfigNameOffset);

			if((ini_status = IniRegInitialize(ConfigName, Options->IniSaveTimeout)) != INI_OK) {

				if(ini_status == INI_NOMEM) {
					goto __no_memory;
				}

				if(ini_status == INI_CANTOPEN) {
					if((message = malloc((wcslen(ConfigName) + 40) * sizeof(WCHAR))) != NULL) {
						swprintf(message, L"Can't create \"%s\".", ConfigName);
						ShowMessageBox(message);
						free(message);
						goto __error_exit;
					}
				}

				ShowMessageBox(L"Can't initialize registry hook handlers.");
				goto __error_exit;
			}
		}

		// initialize filesystem handlers
		if(HookFilesystem)
		{
			DataDirName = (void*)((char*)Options + Options->DataDirectoryOffset);

			if((fsr_status = FsrInit(DataDirName)) != FSR_OK) {

				if(fsr_status == FSR_NOMEM)
					goto __no_memory;

				if(fsr_status == FSR_BADPATH) {
					if((message = malloc((wcslen(DataDirName) + 40) * sizeof(WCHAR))) != NULL) {
						swprintf(message, L"Can't create directory \"%s\".", DataDirName);
						ShowMessageBox(message);
						free(message);
						goto __error_exit;
					}
				}

				if(fsr_status == FSR_PROCNOTFOUND) {
					ShowMessageBox(L"Nessesary ntdll.dll procedure not found.");
					goto __error_exit;
				}

				ShowMessageBox(L"Can't initialize filesystem hook handlers.");
				goto __error_exit;
			}
		}

	}

	// initialize child process injection
	if( InjectChildsEnabled || ShellMode ) {

		IgnoreList = (void*)((char*)Options + Options->NoInjectOffset);

		// enable SE_DEBUG privilege
		if((error = Reg2IniEnableDebugPrivilege()) != ERROR_SUCCESS) {
			ShowErrorMsg(L"Can't get debug privilege.", error);
			goto __error_exit;
		}

		// create options block for injection
		if(!Reg2IniOptionsForChildProcess(&ChildOptions, Options))
			goto __no_memory;

		// enable child injection
		if(!InjectChilds(ChildOptions, IgnoreList))
			goto __error_exit;

	}

	// install hooks
	VectoredHooks = Options->Flags & REG2INI_VECTORED_HOOKS;
	ForcedHooks = Options->Flags & REG2INI_FORCED_HOOKS;

	if((hook_error = HooksInstall(VectoredHooks, ForcedHooks)) != CODE_HOOK_OK) {

		if(hook_error == CODE_HOOK_NOMEM)
			goto __no_memory;

		if(hook_error == CODE_HOOK_CONFLICT) {
			ShowMessageBox(L"Can't install hooks. Conflicting hooks already installed.");
			goto __error_exit;
		}

		if(hook_error == CODE_HOOK_BADREADPTR) {
			ShowMessageBox(L"Can't install hooks. Memory read error.");
			goto __error_exit;
		}

		if(hook_error == CODE_HOOK_BADCODE) {
			ShowMessageBox(L"Can't install hooks. Unable to move code.");
			goto __error_exit;
		}

		if(hook_error == CODE_HOOK_CANTPROTECT) {
			ShowMessageBox(L"Can't install hooks. Unable to change memory protection.");
			goto __error_exit;
		}

		if(hook_error == CODE_HOOK_CANTFIX) {
			ShowMessageBox(L"Can't install hooks. Unable to remove conflicting hooks.");
			goto __error_exit;
		}

		if(hook_error == CODE_HOOK_BADPROC) {
			ShowMessageBox(L"Can't install hooks. Required DLL procedure not found.");
			goto __error_exit;
		}

		ShowMessageBox(L"Can't install hooks.");
		goto __error_exit;
	}

	return 1;

__no_memory:
	ShowMessageBox(L"Initialization failed. Not enough memory.");

__error_exit:

	FsrCleanup();
	IniRegCleanup();
	InjectCleanup();

	if(TerminateOnError) {
		ExitProcess(1);
	}

	return 0;
}

// -------------------------------------------------------------------------------------------------

BOOL WINAPI DllMain(HMODULE hInst, DWORD dwCommand, LPVOID unused)
{
	switch(dwCommand) {

		case DLL_PROCESS_ATTACH:

			DisableThreadLibraryCalls(hInst);

			setlocale(LC_ALL, "");

			if(!InjectInit(hInst))
				return FALSE;

			break;

		case DLL_PROCESS_DETACH:

			FsrCleanup();
			IniRegCleanup();
			InjectCleanup();
			HooksUninstall();

			break;

	}

	return TRUE;
}

// -------------------------------------------------------------------------------------------------
