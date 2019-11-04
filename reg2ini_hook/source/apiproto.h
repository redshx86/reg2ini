// -------------------------------------------------------------------------------------------------

#pragma once
#include <windows.h>

// -------------------------------------------------------------------------------------------------

#define STATUS_SUCCESS						0x00000000

#define STATUS_BUFFER_OVERFLOW				0x80000005
#define STATUS_NO_MORE_ENTRIES				0x8000001A

#define STATUS_UNSUCCESSFUL					0xC0000001
#define STATUS_INVALID_INFO_CLASS			0xC0000003
#define STATUS_INFO_LENGTH_MISMATCH			0xC0000004
#define STATUS_ACCESS_DENIED				0xC0000022
#define STATUS_BUFFER_TOO_SMALL				0xC0000023
#define STATUS_OBJECT_NAME_NOT_FOUND		0xC0000034
#define STATUS_CANNOT_DELETE				0xC0000121
#define STATUS_KEY_DELETED					0xC000017C

// -------------------------------------------------------------------------------------------------

typedef struct _UNICODE_STRING {
  USHORT Length;
  USHORT MaximumLength;
  PWSTR  Buffer;
} UNICODE_STRING, *PUNICODE_STRING;

// -------------------------------------------------------------------------------------------------

typedef struct _OBJECT_ATTRIBUTES {
  ULONG           Length;
  HANDLE          RootDirectory;
  PUNICODE_STRING ObjectName;
  ULONG           Attributes;
  PVOID           SecurityDescriptor;
  PVOID           SecurityQualityOfService;
} OBJECT_ATTRIBUTES, *POBJECT_ATTRIBUTES;

// -------------------------------------------------------------------------------------------------

typedef enum _KEY_VALUE_INFORMATION_CLASS {
  KeyValueBasicInformation           = 0,
  KeyValueFullInformation            = 1,
  KeyValuePartialInformation         = 2,
} KEY_VALUE_INFORMATION_CLASS;

// -------------------------------------------------------------------------------------------------

typedef enum _KEY_INFORMATION_CLASS {
  KeyBasicInformation           = 0,
  KeyNodeInformation            = 1,
  KeyFullInformation            = 2,
  KeyNameInformation            = 3,
  KeyCachedInformation          = 4,
  KeyFlagsInformation           = 5,
  KeyVirtualizationInformation  = 6,
  KeyHandleTagsInformation      = 7,
  MaxKeyInfoClass               = 8
} KEY_INFORMATION_CLASS;

// -------------------------------------------------------------------------------------------------

typedef enum _THREAD_INFORMATION_CLASS {
	ThreadBasicInformation,
	ThreadTimes,
	ThreadPriority,
	ThreadBasePriority,
	ThreadAffinityMask,
	ThreadImpersonationToken,
	ThreadDescriptorTableEntry,
	ThreadEnableAlignmentFaultFixup,
	ThreadEventPair,
	ThreadQuerySetWin32StartAddress,
	ThreadZeroTlsCell,
	ThreadPerformanceCount,
	ThreadAmILastThread,
	ThreadIdealProcessor,
	ThreadPriorityBoost,
	ThreadSetTlsArrayAddress,
	ThreadIsIoPending,
	ThreadHideFromDebugger
} THREAD_INFORMATION_CLASS, *PTHREAD_INFORMATION_CLASS;

// -------------------------------------------------------------------------------------------------

typedef enum _PROCESS_INFORMATION_CLASS {
	ProcessBasicInformation = 0,
	ProcessDebugPort = 7,
	ProcessImageFileName = 27,
} PROCESS_INFORMATION_CLASS, *PPROCESS_INFORMATION_CLASS;

// -------------------------------------------------------------------------------------------------

typedef struct _KEY_VALUE_BASIC_INFORMATION {
  ULONG TitleIndex;
  ULONG Type;
  ULONG NameLength;
  WCHAR Name[];
} KEY_VALUE_BASIC_INFORMATION, *PKEY_VALUE_BASIC_INFORMATION;

// -------------------------------------------------------------------------------------------------

typedef struct _KEY_VALUE_PARTIAL_INFORMATION {
  ULONG TitleIndex;
  ULONG Type;
  ULONG DataLength;
  UCHAR Data[];
} KEY_VALUE_PARTIAL_INFORMATION, *PKEY_VALUE_PARTIAL_INFORMATION;

// -------------------------------------------------------------------------------------------------

typedef struct _KEY_VALUE_FULL_INFORMATION {
  ULONG TitleIndex;
  ULONG Type;
  ULONG DataOffset;
  ULONG DataLength;
  ULONG NameLength;
  WCHAR Name[];
} KEY_VALUE_FULL_INFORMATION, *PKEY_VALUE_FULL_INFORMATION;

// -------------------------------------------------------------------------------------------------

typedef struct _KEY_BASIC_INFORMATION {
  LARGE_INTEGER LastWriteTime;
  ULONG         TitleIndex;
  ULONG         NameLength;
  WCHAR         Name[1];
} KEY_BASIC_INFORMATION, *PKEY_BASIC_INFORMATION;

// -------------------------------------------------------------------------------------------------

typedef struct _KEY_NODE_INFORMATION {
  LARGE_INTEGER LastWriteTime;
  ULONG         TitleIndex;
  ULONG         ClassOffset;
  ULONG         ClassLength;
  ULONG         NameLength;
  WCHAR         Name[1];
} KEY_NODE_INFORMATION, *PKEY_NODE_INFORMATION;

// -------------------------------------------------------------------------------------------------

typedef struct _KEY_FULL_INFORMATION {
  LARGE_INTEGER LastWriteTime;
  ULONG         TitleIndex;
  ULONG         ClassOffset;
  ULONG         ClassLength;
  ULONG         SubKeys;
  ULONG         MaxNameLen;
  ULONG         MaxClassLen;
  ULONG         Values;
  ULONG         MaxValueNameLen;
  ULONG         MaxValueDataLen;
  WCHAR         Class[1];
} KEY_FULL_INFORMATION, *PKEY_FULL_INFORMATION;

// -------------------------------------------------------------------------------------------------

typedef struct _KEY_NAME_INFORMATION {
  ULONG NameLength;
  WCHAR Name[1];
} KEY_NAME_INFORMATION, *PKEY_NAME_INFORMATION;

// -------------------------------------------------------------------------------------------------

typedef struct _KEY_CACHED_INFORMATION {
  LARGE_INTEGER LastWriteTime;
  ULONG         TitleIndex;
  ULONG         SubKeys;
  ULONG         MaxNameLen;
  ULONG         Values;
  ULONG         MaxValueNameLen;
  ULONG         MaxValueDataLen;
  ULONG         NameLength;
} KEY_CACHED_INFORMATION, *PKEY_CACHED_INFORMATION;

// -------------------------------------------------------------------------------------------------

typedef struct _KEY_VIRTUALIZATION_INFORMATION {
  ULONG VirtualizationCandidate  :1;
  ULONG VirtualizationEnabled  :1;
  ULONG VirtualTarget  :1;
  ULONG VirtualStore  :1;
  ULONG VirtualSource  :1;
  ULONG Reserved  :27;
} KEY_VIRTUALIZATION_INFORMATION, *PKEY_VIRTUALIZATION_INFORMATION;

// -------------------------------------------------------------------------------------------------

typedef struct _KEY_VALUE_ENTRY {
  PUNICODE_STRING         ValueName;
  ULONG                   DataLength;
  ULONG                   DataOffset;
  ULONG                   Type;
} KEY_VALUE_ENTRY, *PKEY_VALUE_ENTRY;

// -------------------------------------------------------------------------------------------------

typedef struct _CLIENT_ID {
	ULONG ProcessId;
	ULONG ThreadId;
} CLIENT_ID, *PCLIENT_ID;

// -------------------------------------------------------------------------------------------------

typedef struct _THREAD_BASIC_INFORMATION {
	ULONG ExitStatus;
	PVOID TebBaseAddress;
	CLIENT_ID ClientId;
	ULONG AffinityMask;
	ULONG Priority;
	ULONG BasePriority;
} THREAD_BASIC_INFORMATION, *PTHREAD_BASIC_INFORMATION;

// -------------------------------------------------------------------------------------------------

typedef struct _PROCESS_BASIC_INFORMATION {
    ULONG ExitStatus;
    PVOID PebBaseAddress;
    ULONG AffinityMask;
    ULONG BasePriority;
    ULONG UniqueProcessId;
    ULONG InheritedFromUniqueProcessId;
} PROCESS_BASIC_INFORMATION,*PPROCESS_BASIC_INFORMATION;

// -------------------------------------------------------------------------------------------------

typedef void (__stdcall * TypeRtlFreeUnicodeString)(UNICODE_STRING*);
typedef ULONG (__stdcall * TypeRtlFormatCurrentUserKeyPath)(UNICODE_STRING*);
typedef ULONG (__stdcall * TypeRtlConvertSidToUnicodeString)(UNICODE_STRING*, void*, BOOL);

typedef LONG (__stdcall * TypeNtOpenProcessToken)(HANDLE, ULONG, HANDLE*);

typedef LONG (__stdcall * TypeNtQueryInformationThread)(HANDLE hThread, int InfoClass, void *Buf, ULONG BufSize, ULONG *ResultSize);
typedef LONG (__stdcall * TypeNtQueryInformationProcess)(HANDLE hProcess, int InfoClass, void *Buf, ULONG BufSize, ULONG *ResultSize);
typedef LONG (__stdcall * TypeNtQueryInformationToken)(HANDLE, int, void*, ULONG, ULONG*);

// -------------------------------------------------------------------------------------------------

typedef BOOL (__stdcall * TypeOpenProcessToken)(HANDLE, DWORD, HANDLE*);
typedef BOOL (__stdcall * TypeLookupPrivilegeValue)(TCHAR*, TCHAR*, void*);
typedef BOOL (__stdcall * TypeAdjustTokenPrivileges)(HANDLE, BOOL, void*, DWORD, void*, DWORD*);

// -------------------------------------------------------------------------------------------------
