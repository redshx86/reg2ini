// -------------------------------------------------------------------------------------------------

#pragma once
#include <windows.h>
#include "apiproto.h"

// -------------------------------------------------------------------------------------------------

LONG __stdcall HookNtClose(HANDLE);

LONG __stdcall HookNtOpenKey(HANDLE*, DWORD, OBJECT_ATTRIBUTES*);
LONG __stdcall HookNtOpenKeyEx(HANDLE*, DWORD, OBJECT_ATTRIBUTES*, ULONG);
LONG __stdcall HookNtCreateKey(HANDLE*, DWORD, OBJECT_ATTRIBUTES*, ULONG, UNICODE_STRING*, ULONG, ULONG*);
LONG __stdcall HookNtCreateKeyTransacted(HANDLE*, DWORD, OBJECT_ATTRIBUTES*, ULONG, UNICODE_STRING*, ULONG, HANDLE, ULONG*);
LONG __stdcall HookNtQueryKey(HANDLE, int, void*, ULONG, ULONG*);
LONG __stdcall HookNtEnumerateKey(HANDLE, ULONG, int, void*, ULONG, ULONG*);
LONG __stdcall HookNtDeleteKey(HANDLE);
LONG __stdcall HookNtFlushKey(HANDLE);
LONG __stdcall HookNtRenameKey(HANDLE, UNICODE_STRING*);

LONG __stdcall HookNtQueryValueKey(HANDLE, UNICODE_STRING*, int, void*, ULONG, ULONG*);
LONG __stdcall HookNtQueryMultipleValueKey(HANDLE, KEY_VALUE_ENTRY*, ULONG, void*, ULONG*, ULONG*);
LONG __stdcall HookNtEnumerateValueKey(HANDLE, ULONG, int, void*, ULONG, ULONG*);
LONG __stdcall HookNtSetValueKey(HANDLE, UNICODE_STRING*, ULONG, ULONG, void*, ULONG);
LONG __stdcall HookNtDeleteValueKey(HANDLE, UNICODE_STRING*);

LONG __stdcall HookNtResumeThread(HANDLE, ULONG*);

HANDLE __stdcall HookCreateFileW(WCHAR*, DWORD, DWORD, void*, DWORD, DWORD, void*);
DWORD __stdcall HookGetFileAttributesW(WCHAR*);
BOOL __stdcall HookGetFileAttributesExW(WCHAR*, int, void*);
BOOL __stdcall HookSetFileAttributesW(WCHAR*, DWORD);
BOOL __stdcall HookCreateDirectoryW(WCHAR*, void*);
BOOL __stdcall HookRemoveDirectoryW(WCHAR*);
BOOL __stdcall HookDeleteFileW(WCHAR*);

DWORD __stdcall HookSearchPathW(WCHAR*, WCHAR*, WCHAR*, DWORD, WCHAR*, WCHAR**);
HANDLE __stdcall HookFindFirstFileExW(WCHAR*, int, WIN32_FIND_DATA*, int, void*, DWORD);
BOOL __stdcall HookFindNextFileW(HANDLE, WIN32_FIND_DATA*);
BOOL __stdcall HookFindClose(HANDLE search);

BOOL __stdcall HookCopyFileExW(WCHAR*, WCHAR*, void*, void*, BOOL*, DWORD);
BOOL __stdcall HookMoveFileWithProgressW(WCHAR*, WCHAR*, void*, void*, DWORD);

BOOL __stdcall HookGetDiskFreeSpaceW(WCHAR*, DWORD*, DWORD*, DWORD*, DWORD*);
BOOL __stdcall HookGetDiskFreeSpaceExW(WCHAR*, void*, void*, void*);

BOOL __stdcall HookCreateProcessW(WCHAR*, WCHAR*, void*, void*, BOOL, DWORD, void*, WCHAR*, STARTUPINFOW*, PROCESS_INFORMATION*);
BOOL __stdcall HookCreateProcessA(char*, char*, void*, void*, BOOL, DWORD, void*, char*, STARTUPINFOA*, PROCESS_INFORMATION*);
HMODULE __stdcall HookLoadLibraryExW(WCHAR*, HANDLE , DWORD);

BOOL __stdcall HookSetCurrentDirectoryW(WCHAR*);
BOOL __stdcall HookSetCurrentDirectoryA(char*);

DWORD __stdcall HookGetPrivateProfileStringW(WCHAR*, WCHAR*, WCHAR*, WCHAR*, DWORD, WCHAR*);
DWORD __stdcall HookGetPrivateProfileSectionW(WCHAR*, WCHAR*, DWORD, WCHAR*);
BOOL __stdcall HookWritePrivateProfileStringW(WCHAR*, WCHAR*, WCHAR*, WCHAR*);
BOOL __stdcall HookWritePrivateProfileSectionW(WCHAR*, WCHAR*, WCHAR*);

DWORD __stdcall HookGetPrivateProfileStringA(char*, char*, char*, char*, DWORD, char*);
DWORD __stdcall HookGetPrivateProfileSectionA(char*, char*, DWORD, char*);
BOOL __stdcall HookWritePrivateProfileStringA(char*, char*, char*, char*);
BOOL __stdcall HookWritePrivateProfileSectionA(char*, char*, char*);

// -------------------------------------------------------------------------------------------------

typedef LONG (__stdcall * TypeNtClose)(HANDLE);

typedef LONG (__stdcall * TypeNtOpenKey)(HANDLE*, DWORD, OBJECT_ATTRIBUTES*);
typedef LONG (__stdcall * TypeNtOpenKeyEx)(HANDLE*, DWORD, OBJECT_ATTRIBUTES*, ULONG);
typedef LONG (__stdcall * TypeNtCreateKey)(HANDLE*, DWORD, OBJECT_ATTRIBUTES*, ULONG, UNICODE_STRING*, ULONG, ULONG*);
typedef LONG (__stdcall * TypeNtCreateKeyTransacted)(HANDLE*, DWORD, OBJECT_ATTRIBUTES*, ULONG, UNICODE_STRING*, ULONG, HANDLE, ULONG*);
typedef LONG (__stdcall * TypeNtQueryKey)(HANDLE, int, void*, ULONG, ULONG*);
typedef LONG (__stdcall * TypeNtEnumerateKey)(HANDLE, ULONG, int, void*, ULONG, ULONG*);
typedef LONG (__stdcall * TypeNtDeleteKey)(HANDLE);
typedef LONG (__stdcall * TypeNtFlushKey)(HANDLE);
typedef LONG (__stdcall * TypeNtRenameKey)(HANDLE, UNICODE_STRING*);

typedef LONG (__stdcall * TypeNtQueryValueKey)(HANDLE, UNICODE_STRING*, int, void*, ULONG, ULONG*);
typedef LONG (__stdcall * TypeNtQueryMultipleValueKey)(HANDLE, KEY_VALUE_ENTRY*, ULONG, void*, ULONG*, ULONG*);
typedef LONG (__stdcall * TypeNtEnumerateValueKey)(HANDLE, ULONG, int, void*, ULONG, ULONG*);
typedef LONG (__stdcall * TypeNtSetValueKey)(HANDLE, UNICODE_STRING*, ULONG, ULONG, void*, ULONG);
typedef LONG (__stdcall * TypeNtDeleteValueKey)(HANDLE, UNICODE_STRING*);

typedef LONG (__stdcall * TypeNtResumeThread)(HANDLE, ULONG*);

typedef HANDLE (__stdcall * TypeCreateFileW)(WCHAR*, DWORD, DWORD, void*, DWORD, DWORD, void*);
typedef DWORD (__stdcall * TypeGetFileAttributesW)(WCHAR*);
typedef BOOL (__stdcall * TypeGetFileAttributesExW)(WCHAR*, int, void*);
typedef BOOL (__stdcall * TypeSetFileAttributesW)(WCHAR*, DWORD);
typedef BOOL (__stdcall * TypeCreateDirectoryW)(WCHAR*, void*);
typedef BOOL (__stdcall * TypeRemoveDirectoryW)(WCHAR*);
typedef BOOL (__stdcall * TypeDeleteFileW)(WCHAR*);

typedef DWORD (__stdcall * TypeSearchPathW)(WCHAR*, WCHAR*, WCHAR*, DWORD, WCHAR*, WCHAR**);
typedef HANDLE (__stdcall * TypeFindFirstFileExW)(WCHAR*, int, WIN32_FIND_DATA*, int, void*, DWORD);
typedef BOOL (__stdcall * TypeFindNextFileW)(HANDLE, WIN32_FIND_DATA*);
typedef BOOL (__stdcall * TypeFindClose)(HANDLE search);

typedef BOOL (__stdcall * TypeCopyFileExW)(WCHAR*, WCHAR*, void*, void*, BOOL*, DWORD);
typedef BOOL (__stdcall * TypeMoveFileWithProgressW)(WCHAR*, WCHAR*, void*, void*, DWORD);

typedef BOOL (__stdcall * TypeGetDiskFreeSpaceW)(WCHAR*, DWORD*, DWORD*, DWORD*, DWORD*);
typedef BOOL (__stdcall * TypeGetDiskFreeSpaceExW)(WCHAR*, void*, void*, void*);

typedef BOOL (__stdcall * TypeCreateProcessW)(WCHAR*, WCHAR*, void*, void*, BOOL, DWORD, void*, WCHAR*, STARTUPINFOW*, PROCESS_INFORMATION*);
typedef BOOL (__stdcall * TypeCreateProcessA)(char*, char*, void*, void*, BOOL, DWORD, void*, char*, STARTUPINFOA*, PROCESS_INFORMATION*);
typedef HMODULE (__stdcall * TypeLoadLibraryExW)(WCHAR*, HANDLE , DWORD);

typedef BOOL (__stdcall * TypeSetCurrentDirectoryW)(WCHAR*);
typedef BOOL (__stdcall * TypeSetCurrentDirectoryA)(char*);

typedef DWORD (__stdcall * TypeGetPrivateProfileStringW)(WCHAR*, WCHAR*, WCHAR*, WCHAR*, DWORD, WCHAR*);
typedef DWORD (__stdcall * TypeGetPrivateProfileSectionW)(WCHAR*, WCHAR*, DWORD, WCHAR*);
typedef BOOL (__stdcall * TypeWritePrivateProfileStringW)(WCHAR*, WCHAR*, WCHAR*, WCHAR*);
typedef BOOL (__stdcall * TypeWritePrivateProfileSectionW)(WCHAR*, WCHAR*, WCHAR*);

typedef DWORD (__stdcall * TypeGetPrivateProfileStringA)(char*, char*, char*, char*, DWORD, char*);
typedef DWORD (__stdcall * TypeGetPrivateProfileSectionA)(char*, char*, DWORD, char*);
typedef BOOL (__stdcall * TypeWritePrivateProfileStringA)(char*, char*, char*, char*);
typedef BOOL (__stdcall * TypeWritePrivateProfileSectionA)(char*, char*, char*);

// -------------------------------------------------------------------------------------------------
