// -------------------------------------------------------------------------------------------------

#pragma once
#include "hookproc.h"

// -------------------------------------------------------------------------------------------------

enum {
	HOOK_MODULE_NTDLL,
	HOOK_MODULE_KERNEL32,

	HOOK_MODULE_COUNT,
};

// -------------------------------------------------------------------------------------------------

#define HOOK_MODULE_NAME_LIST\
	L"ntdll.dll",\
	L"kernel32.dll",\

// -------------------------------------------------------------------------------------------------

typedef struct hook_proc_entry {
	int module_id;
	char *name;
	void *target;
	int required;
} hook_proc_entry_t;

// -------------------------------------------------------------------------------------------------

enum {
	HOOK_PROC_NTCLOSE,
	HOOK_PROC_NTOPENKEY,
	HOOK_PROC_NTOPENKEYEX,
	HOOK_PROC_NTCREATEKEY,
	HOOK_PROC_NTCREATEKEYTRANSACTED,
	HOOK_PROC_NTENUMERATEKEY,
	HOOK_PROC_NTENUMERATEVALUEKEY,
	HOOK_PROC_NTQUERYKEY,
	HOOK_PROC_NTQUERYVALUEKEY,
	HOOK_PROC_NTQUERYMULTIPLEVALUEKEY,
	HOOK_PROC_NTSETVALUEKEY,
	HOOK_PROC_NTDELETEKEY,
	HOOK_PROC_NTFLUSHKEY,
	HOOK_PROC_NTRENAMEKEY,
	HOOK_PROC_NTDELETEVALUEKEY,
	HOOK_PROC_NTRESUMETHREAD,
	HOOK_PROC_CREATEFILEW,
	HOOK_PROC_GETFILEATTRIBUTESW,
	HOOK_PROC_GETFILEATTRIBUTESEXW,
	HOOK_PROC_SETFILEATTRIBUTESW,
	HOOK_PROC_CREATEDIRECTORYW,
	HOOK_PROC_REMOVEDIRECTORYW,
	HOOK_PROC_DELETEFILEW,
	HOOK_PROC_COPYFILEEXW,
	HOOK_PROC_MOVEFILEWITHPROGRESSW,
	HOOK_PROC_SEARCHPATHW,
	HOOK_PROC_FINDFIRSTFILEEXW,
	HOOK_PROC_FINDNEXTFILEW,
	HOOK_PROC_FINDCLOSE,
	HOOK_PROC_GETDISKFREESPACEW,
	HOOK_PROC_GETDISKFREESPACEEXW,
	HOOK_PROC_CREATEPROCESSW,
	HOOK_PROC_CREATEPROCESSA,
	HOOK_PROC_LOADLIBRARYEXW,
	HOOK_PROC_SETCURRENTDIRECTORYW,
	HOOK_PROC_SETCURRENTDIRECTORYA,
	HOOK_PROC_GETPRIVATEPROFILESTRINGW,
	HOOK_PROC_GETPRIVATEPROFILESECTIONW,
	HOOK_PROC_GETPRIVATEPROFILESTRINGA,
	HOOK_PROC_GETPRIVATEPROFILESECTIONA,
	HOOK_PROC_WRITEPRIVATEPROFILESTRINGW,
	HOOK_PROC_WRITEPRIVATEPROFILESECTIONW,
	HOOK_PROC_WRITEPRIVATEPROFILESTRINGA,
	HOOK_PROC_WRITEPRIVATEPROFILESECTIONA,

	HOOK_PROC_COUNT,
};

// -------------------------------------------------------------------------------------------------

#define HOOK_PROC_LIST\
	{HOOK_MODULE_NTDLL,		"NtClose",						HookNtClose,						1 },\
	{HOOK_MODULE_NTDLL,		"NtOpenKey",					HookNtOpenKey,						1 },\
	{HOOK_MODULE_NTDLL,		"NtOpenKeyEx",					HookNtOpenKeyEx,					0 },\
	{HOOK_MODULE_NTDLL,		"NtCreateKey",					HookNtCreateKey,					1 },\
	{HOOK_MODULE_NTDLL,		"NtCreateKeyTransacted",		HookNtCreateKeyTransacted,			0 },\
	{HOOK_MODULE_NTDLL,		"NtEnumerateKey",				HookNtEnumerateKey,					1 },\
	{HOOK_MODULE_NTDLL,		"NtEnumerateValueKey",			HookNtEnumerateValueKey,			1 },\
	{HOOK_MODULE_NTDLL,		"NtQueryKey",					HookNtQueryKey,						1 },\
	{HOOK_MODULE_NTDLL,		"NtQueryValueKey",				HookNtQueryValueKey,				1 },\
	{HOOK_MODULE_NTDLL,		"NtQueryMultipleValueKey",		HookNtQueryMultipleValueKey,		1 },\
	{HOOK_MODULE_NTDLL,		"NtSetValueKey",				HookNtSetValueKey,					1 },\
	{HOOK_MODULE_NTDLL,		"NtDeleteKey",					HookNtDeleteKey,					1 },\
	{HOOK_MODULE_NTDLL,		"NtFlushKey",					HookNtFlushKey,						0 },\
	{HOOK_MODULE_NTDLL,		"NtRenameKey",					HookNtRenameKey,					0 },\
	{HOOK_MODULE_NTDLL,		"NtDeleteValueKey",				HookNtDeleteValueKey,				1 },\
	{HOOK_MODULE_NTDLL,		"NtResumeThread",				HookNtResumeThread,					1 },\
	{HOOK_MODULE_KERNEL32,	"CreateFileW",					HookCreateFileW,					1 },\
	{HOOK_MODULE_KERNEL32,	"GetFileAttributesW",			HookGetFileAttributesW,				1 },\
	{HOOK_MODULE_KERNEL32,	"GetFileAttributesExW",			HookGetFileAttributesExW,			0 },\
	{HOOK_MODULE_KERNEL32,	"SetFileAttributesW",			HookSetFileAttributesW,				1 },\
	{HOOK_MODULE_KERNEL32,	"CreateDirectoryW",				HookCreateDirectoryW,				1 },\
	{HOOK_MODULE_KERNEL32,	"RemoveDirectoryW",				HookRemoveDirectoryW,				1 },\
	{HOOK_MODULE_KERNEL32,	"DeleteFileW",					HookDeleteFileW,					1 },\
	{HOOK_MODULE_KERNEL32,	"CopyFileExW",					HookCopyFileExW,					1 },\
	{HOOK_MODULE_KERNEL32,	"MoveFileWithProgressW",		HookMoveFileWithProgressW,			1 },\
	{HOOK_MODULE_KERNEL32,	"SearchPathW",					HookSearchPathW,					1 },\
	{HOOK_MODULE_KERNEL32,	"FindFirstFileExW",				HookFindFirstFileExW,				1 },\
	{HOOK_MODULE_KERNEL32,	"FindNextFileW",				HookFindNextFileW,					1 },\
	{HOOK_MODULE_KERNEL32,	"FindClose",					HookFindClose,						1 },\
	{HOOK_MODULE_KERNEL32,	"GetDiskFreeSpaceW",			HookGetDiskFreeSpaceW,				1 },\
	{HOOK_MODULE_KERNEL32,	"GetDiskFreeSpaceExW",			HookGetDiskFreeSpaceExW,			0 },\
	{HOOK_MODULE_KERNEL32,	"CreateProcessW",				HookCreateProcessW,					1 },\
	{HOOK_MODULE_KERNEL32,	"CreateProcessA",				HookCreateProcessA,					1 },\
	{HOOK_MODULE_KERNEL32,	"LoadLibraryExW",				HookLoadLibraryExW,					1 },\
	{HOOK_MODULE_KERNEL32,	"SetCurrentDirectoryW",			HookSetCurrentDirectoryW,			1 },\
	{HOOK_MODULE_KERNEL32,	"SetCurrentDirectoryA",			HookSetCurrentDirectoryA,			1 },\
	{HOOK_MODULE_KERNEL32,	"GetPrivateProfileStringW",		HookGetPrivateProfileStringW,		0 },\
	{HOOK_MODULE_KERNEL32,	"GetPrivateProfileSectionW",	HookGetPrivateProfileSectionW,		0 },\
	{HOOK_MODULE_KERNEL32,	"GetPrivateProfileStringA",		HookGetPrivateProfileStringA,		0 },\
	{HOOK_MODULE_KERNEL32,	"GetPrivateProfileSectionA",	HookGetPrivateProfileSectionA,		0 },\
	{HOOK_MODULE_KERNEL32,	"WritePrivateProfileStringW",	HookWritePrivateProfileStringW,		0 },\
	{HOOK_MODULE_KERNEL32,	"WritePrivateProfileSectionW",	HookWritePrivateProfileSectionW,	0 },\
	{HOOK_MODULE_KERNEL32,	"WritePrivateProfileStringA",	HookWritePrivateProfileStringA,		0 },\
	{HOOK_MODULE_KERNEL32,	"WritePrivateProfileSectionA",	HookWritePrivateProfileSectionA,	0 },\

// -------------------------------------------------------------------------------------------------
