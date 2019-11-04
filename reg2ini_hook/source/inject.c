// -------------------------------------------------------------------------------------------------

#include "inject.h"

// -------------------------------------------------------------------------------------------------

static TypeNtQueryInformationThread QueryInfoThread;
static TypeNtQueryInformationProcess QueryInfoProcess;

static WCHAR *InjectMutexName = L"reg2ini_inject_%u";

static int InjectEnabled;
static HANDLE InjectMutex;
static ULONG CurrentProcessId;
static INJECTED_BLOCK InjectedBlock;

static int ChildInjectEnabled;
static REG2INI_OPTIONS *ChildOptions;
static WCHAR *ChildInjectIgnore;

// -------------------------------------------------------------------------------------------------

__declspec(naked)
static void shellcode()
{
	__asm {

		// code size: 0x44 bytes

		push	eax					// will copy return address here later

		push	ebp
		push	esi

		pushfd

		mov		ebp,esp				// store stack pointer to esp (entry esp = ebp+10h)

		call	$+5					// calculate data block address
		pop		esi
		sub		esi,24bh

		pushad						// push all registers

		lea		eax,[esi+8]			// call LoadLibrary(Block.DllName)
		push	eax
		call	[esi]
		or		eax,eax
		jz		__exit_thread

		lea		ecx,[esi+210h]		// call GetProcAddress(hDll, block.ProcName)
		push	ecx
		push	eax
		call	[esi+4]
		or		eax,eax
		jz		__exit_thread

		lea		ecx,[esi+2c4h]		// call procedure (InstallHooks)
		push	ecx
		call	eax
		pop		ecx

__exit_thread:

		mov		eax,[esi+2c0h]		// set return address
		mov		[ebp+0ch],eax

		popad						// pop all saved
		popfd

		pop		esi
		pop		ebp

		retn						// return to process

	}
}

// -------------------------------------------------------------------------------------------------

static void *real_proc_entry(void *proc)
{
	unsigned char *data = proc;
	unsigned long move;

	if(*data == 0xe9) {
		move = *(unsigned long*)(data + 1);
		return (void*)((unsigned long)proc + 5 + move);
	}
	return proc;
}

// -------------------------------------------------------------------------------------------------

static ULONG GetThreadProcessId(HANDLE hThread)
{
	THREAD_BASIC_INFORMATION ThreadInfo;

	if(QueryInfoThread(hThread, ThreadBasicInformation, &ThreadInfo, sizeof(ThreadInfo), NULL) != 0) {
		return 0;
	}

	return ThreadInfo.ClientId.ProcessId;
}

// -------------------------------------------------------------------------------------------------

static ULONG GetParentProcessId(HANDLE hProcess)
{
	PROCESS_BASIC_INFORMATION ProcessInfo;

	if(QueryInfoProcess(hProcess, ProcessBasicInformation, &ProcessInfo, sizeof(ProcessInfo), NULL) != 0)
		return 0;

	return ProcessInfo.InheritedFromUniqueProcessId;
}

// -------------------------------------------------------------------------------------------------

static int GetProcessImageName(HANDLE hProcess, UNICODE_STRING **pImgName)
{
	UNICODE_STRING *ImgName, *TempImgName;
	ULONG ImgNameBufSize, ImgNameSize, status;

	ImgNameBufSize = 512;
	if((ImgName = malloc(ImgNameBufSize)) == NULL)
		return 0;

	// ProcessImageFileName: XP or later
	// On Win2000 returns STATUS_INVALID_INFO_CLASS
	status = QueryInfoProcess(hProcess, ProcessImageFileName, ImgName, ImgNameBufSize, &ImgNameSize);
	if(status == STATUS_INFO_LENGTH_MISMATCH) {
		if((TempImgName = realloc(ImgName, ImgNameSize)) == NULL) {
			free(ImgName);
			return 0;
		}
		ImgName = TempImgName;
		ImgNameBufSize = ImgNameSize;
		status = QueryInfoProcess(hProcess, ProcessImageFileName, ImgName, ImgNameBufSize, &ImgNameSize);
	}

	if(status != STATUS_SUCCESS) {
		free(ImgName);
		return 0;
	}

	*pImgName = ImgName;
	return 1;
}

// -------------------------------------------------------------------------------------------------

static int CheckInjectIgnoreList(UNICODE_STRING *ProcessName)
{
	WCHAR *NamePart, *IgnoreName;
	ULONG NamePartLen, ignoreNameLen;

	// get filename part (without path)
	NamePartLen = 0;
	NamePart = ProcessName->Buffer + ProcessName->Length / sizeof(WCHAR);
	while(NamePart >= ProcessName->Buffer) {
		if((*NamePart == L'\\') || (*NamePart == L'/')) {
			NamePart++;
			NamePartLen--;
			break;
		}
		NamePart--;
		NamePartLen++;
	}

	// check image name vs ignore list
	for(IgnoreName = ChildInjectIgnore; *IgnoreName != 0; IgnoreName += ignoreNameLen + 1) {
		ignoreNameLen = wcslen(IgnoreName);
		if( (ignoreNameLen == NamePartLen) && (wcsnicmp(NamePart, IgnoreName, ignoreNameLen) == 0) )
			return 1;
	}

	return 0;
}

// -------------------------------------------------------------------------------------------------

static int IsInjected(DWORD ProcessId)
{
	int injected = 0;
	WCHAR MutexName[64];
	HANDLE mutex;
	DWORD status;

	// get mutex name for ProcessId
	swprintf(MutexName, InjectMutexName, ProcessId);

	// check mutex exists
	if((mutex = CreateMutex(NULL, FALSE, MutexName)) != NULL) {
		status = GetLastError();
		injected = (status == ERROR_ALREADY_EXISTS);
		CloseHandle(mutex);
	}

	return injected;
}

// -------------------------------------------------------------------------------------------------

static DWORD AttachProcess(HANDLE hProcess, HANDLE hThread, REG2INI_OPTIONS * Options)
{
	DWORD status;
	DWORD BlockSize;
	INJECTED_BLOCK *Block, *RemoteBlock;
	CONTEXT context;

	// prepare "injection block"
	BlockSize = (sizeof(INJECTED_BLOCK) + Options->Size + 0xFFF) & ~0xFFF;
	if((Block = malloc(BlockSize)) == NULL)
		return ERROR_NOT_ENOUGH_MEMORY;

	memcpy(Block, &InjectedBlock, sizeof(INJECTED_BLOCK));
	memcpy(Block->Options, Options, Options->Size);

	// get current EIP
	memset(&context, 0, sizeof(context));
	context.ContextFlags = CONTEXT_CONTROL;
	if(!GetThreadContext(hThread, &context)) {
		status = GetLastError();
	} else {
		Block->ReturnAddress = (void*)(context.Eip);
		// allocate block in target process memory
		if((RemoteBlock = VirtualAllocEx(hProcess, NULL, BlockSize, MEM_COMMIT, PAGE_EXECUTE_READWRITE)) == NULL) {
			status = GetLastError();
		} else {
			// copy block to target process memory
			if(!WriteProcessMemory(hProcess, RemoteBlock, Block, BlockSize, NULL)) {
				status = GetLastError();
			} else {
				// set EIP to shellcode
				context.Eip = (ULONG)(RemoteBlock->Shellcode);
				if(!SetThreadContext(hThread, &context)) {
					status = GetLastError();
				} else {
					// success
					free(Block);
					return ERROR_SUCCESS;
				}
			}
		}
		VirtualFreeEx(hProcess, RemoteBlock, 0, MEM_RELEASE);
	}

	free(Block);
	return status;
}

// -------------------------------------------------------------------------------------------------

LONG __stdcall HookNtResumeThread(HANDLE hThread, ULONG *SuspendCount)
{
	UNICODE_STRING *ProcessName;
	ULONG ProcessId, ParentProcessId;
	TypeNtResumeThread ResThread;
	HANDLE hProcess;

	if(ChildInjectEnabled) {
		// new thread and process should have ALL_ACCESS
		if((ProcessId = GetThreadProcessId(hThread)) != 0) {
			// check target process is not current and have not injection now
			if( (ProcessId != CurrentProcessId) && (!IsInjected(ProcessId)) ) {
				// open target process for debug operations
				hProcess = OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_OPERATION|
					PROCESS_VM_READ|PROCESS_VM_WRITE, FALSE, ProcessId);
				if(hProcess != NULL) {
					// check target process is have current procress as parent
					ParentProcessId = GetParentProcessId(hProcess);
					if(ParentProcessId == CurrentProcessId) {
						// get target process imagename
						if(GetProcessImageName(hProcess, &ProcessName)) {
							// check process name vs inject ignore list
							if(!CheckInjectIgnoreList(ProcessName)) {
								// inject to target process
								AttachProcess(hProcess, hThread, ChildOptions);
							}
							free(ProcessName);
						} else {
							// check process name vs inject ignore list
							AttachProcess(hProcess, hThread, ChildOptions);
						}
					}
					CloseHandle(hProcess);
				}
			}
		}
	}

	ResThread = HookGetOEP(HOOK_PROC_NTRESUMETHREAD);
	return ResThread(hThread, SuspendCount);
}

// -------------------------------------------------------------------------------------------------

DWORD Reg2IniCreateProcess(WCHAR *Command, STARTUPINFOW *StartupInfo, REG2INI_OPTIONS * Options)
{
	DWORD status;
	PROCESS_INFORMATION ProcessInfo;

	// create process suspended
	if(!CreateProcess(NULL, Command, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, StartupInfo, &ProcessInfo)) {
		status = GetLastError();
	} else {
		// inject to process
		status = AttachProcess(ProcessInfo.hProcess, ProcessInfo.hThread, Options);
		// terminate process on failure (if required), resume otherwise
		if( (status != ERROR_SUCCESS) && (Options->Flags & REG2INI_TERMINATE_ON_FAIL) ) {
			TerminateProcess(ProcessInfo.hProcess, 1);
		} else {
			ResumeThread(ProcessInfo.hThread);
		}
		// close process handles
		CloseHandle(ProcessInfo.hProcess);
		CloseHandle(ProcessInfo.hThread);
	}

	return status;
}

// -------------------------------------------------------------------------------------------------

static int InjectedBlockInit(HINSTANCE hInst)
{
	HMODULE kernel32;
	void *ptrLoadLibraryW;
	void *ptrGetProcAddress;

	// find procedures (should be at fixed address in all processes)
	if((kernel32 = GetModuleHandle(_T("kernel32.dll"))) == NULL)
		return 0;

	ptrLoadLibraryW = GetProcAddress(kernel32, "LoadLibraryW");
	ptrGetProcAddress = GetProcAddress(kernel32, "GetProcAddress");
	if( (ptrLoadLibraryW == NULL) || (ptrGetProcAddress == NULL) )
		return 0;

	// initialize injection block
	memset(&InjectedBlock, 0, sizeof(InjectedBlock));

	// procedure addresses
	InjectedBlock.ptrLoadLibraryW = ptrLoadLibraryW;
	InjectedBlock.ptrGetProcAddress = ptrGetProcAddress;

	// injection dll filename
	if(!GetModuleFileName(hInst, InjectedBlock.ModuleToLoadPath, MAX_PATH))
		return 0;

	// procedure name
	strcpy(InjectedBlock.ProcToCallName, "Reg2IniInstallHooks");

	// shellcode
	memset(InjectedBlock.nops, 0x90, sizeof(InjectedBlock.nops));
	memcpy(InjectedBlock.Shellcode, real_proc_entry(shellcode), 0x44);

	return 1;
}

// -------------------------------------------------------------------------------------------------

int InjectChilds(REG2INI_OPTIONS * Options, WCHAR * IgnoreList)
{
	ChildOptions = Options;
	ChildInjectIgnore = IgnoreList;
	ChildInjectEnabled = 1;
	return 1;
}

// -------------------------------------------------------------------------------------------------

int InjectInit(HMODULE hInst)
{
	HMODULE ntdll;
	WCHAR MutexName[64];

	if(!InjectEnabled) {

		// get current process id
		CurrentProcessId = GetCurrentProcessId();

		// create injection status object (mutex)
		swprintf(MutexName, InjectMutexName, CurrentProcessId);
		if((InjectMutex = CreateMutex(NULL, FALSE, MutexName)) == NULL)
			return 0;

		// find procedures
		if((ntdll = GetModuleHandle(_T("ntdll.dll"))) == NULL) {
			CloseHandle(InjectMutex);
			return 0;
		}

		QueryInfoThread = (void*)GetProcAddress(ntdll, "NtQueryInformationThread");
		QueryInfoProcess = (void*)GetProcAddress(ntdll, "NtQueryInformationProcess");
		if( (QueryInfoThread == NULL) || (QueryInfoProcess == NULL) ) {
			CloseHandle(InjectMutex);
			return 0;
		}

		// initialize "injection block"
		if(!InjectedBlockInit(hInst)) {
			CloseHandle(InjectMutex);
			return 0;
		}

		InjectEnabled = 1;

	}

	return 1;
}

// -------------------------------------------------------------------------------------------------

void InjectCleanup()
{
	if(InjectEnabled) {

		InjectEnabled = 0;
		ChildInjectEnabled = 0;

		free(ChildOptions);
		ChildOptions = NULL;

		CloseHandle(InjectMutex);
		InjectMutex = NULL;

	}
}

// -------------------------------------------------------------------------------------------------
