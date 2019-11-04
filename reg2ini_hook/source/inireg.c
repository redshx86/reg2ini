// -------------------------------------------------------------------------------------------------

#include "inireg.h"

// -------------------------------------------------------------------------------------------------

static ULONG IniRegKeyOpen(HANDLE *pHandleId, DWORD Access, OBJECT_ATTRIBUTES *Attributes);
static ULONG IniRegKeyCreate(HANDLE *pHandleId, DWORD Access, OBJECT_ATTRIBUTES *Attributes, ULONG *Disposition);
static ULONG IniRegKeyQuery(HANDLE KeyHandleId, int InfoClass, void *Buffer, ULONG BufSize, ULONG *ResultSize);
static ULONG IniRegKeyEnum(HANDLE KeyHandleId, ULONG Index, int InfoClass, void *Buffer, ULONG BufSize, ULONG *ResultSize);
static ULONG IniRegKeyDelete(HANDLE HandleId);
static ULONG IniRegKeyRename(HANDLE HandleId, UNICODE_STRING*);

static ULONG IniRegValueQuery(HANDLE KeyHandleId, UNICODE_STRING *usValueName, int InfoClass, void *Buffer, ULONG BufLength, ULONG *ResultLength);
static ULONG IniRegValueQueryMulti(HANDLE KeyHandleId, KEY_VALUE_ENTRY *Entries, ULONG Count, void *Buffer, ULONG *BufSize, ULONG *RequiredSize);
static ULONG IniRegValueEnum(HANDLE KeyHandleId, ULONG Index, int InfoClass, void *Buffer, ULONG BufSize, ULONG *ResultSize);
static ULONG IniRegValueSet(HANDLE KeyHandleId, UNICODE_STRING *usValueName, ULONG Type, void *Data, ULONG DataSize);
static ULONG IniRegValueDelete(HANDLE KeyHandleId, UNICODE_STRING *usValueName);

static ULONG IniRegFillValueInfo(INI_VALUE *Value, int InfoClass, void *Buf, ULONG BufSize, ULONG *ResultSize);

static INIREG_HANDLE * IniRegHandleAlloc(WCHAR *FullPath);
static ULONG IniRegHandleInit(INIREG_HANDLE **pHdl, INIREG_HANDLE *ParentHdl, UNICODE_STRING *usSubNodePath);
static int IniRegHandleClose(HANDLE IdHandle);
static INIREG_HANDLE *IniRegHandleLookup(HANDLE IdHandle);
static INIREG_HANDLE **IniRegHandleLookupPtr(HANDLE IdHandle);
static HANDLE IniRegHandleAdd(INIREG_HANDLE *Hdl, HANDLE SysHandle);
static ULONG IniRegHandleWrap(INIREG_HANDLE **pHdl, HANDLE SysHandle);
static INIREG_HANDLE *IniRegHandleLookupOrWrap(HANDLE SysHandle);

static WCHAR * IniRegMakeNodePath(WCHAR *FullPath);
static WCHAR * IniRegChangeNamePart(WCHAR *FullPathSrc, UNICODE_STRING * usNewName);
static ULONG IniRegKeyCopyToNode(INI_NODE *Node, HANDLE KeyHandle);

static UNICODE_STRING *WcharBufToUnicodeString(WCHAR *Buf);
static WCHAR *UnicodeStringToWcharBuf(UNICODE_STRING *UnicodeString);
static int MakeZeroString(WCHAR **pBuf, ULONG *pBufSize, WCHAR *Data, ULONG DataSize);

static ULONG IniStatusToNtStatus(INI_STATUS IniStatus);
static INI_VALUE_TYPE RegToIniValueType(ULONG RegType);
static ULONG IniToRegValueType(INI_VALUE_TYPE ValueType);

void IniRegSaveRequest();

// -------------------------------------------------------------------------------------------------

static INIREG_DATA IniRegData;

// -------------------------------------------------------------------------------------------------

LONG __stdcall HookNtClose(HANDLE Handle)
{
	int closed;
	TypeNtClose Close;

	Close = HookGetOEP(HOOK_PROC_NTCLOSE);

	if(IniRegData.HooksEnabled) {

		EnterCriticalSection(&(IniRegData.Lock));
		closed = IniRegHandleClose(Handle);
		LeaveCriticalSection(&(IniRegData.Lock));

		if(closed) {
			return STATUS_SUCCESS;
		}

	}

	return Close(Handle);
}

// -------------------------------------------------------------------------------------------------

LONG __stdcall HookNtOpenKey(HANDLE *KeyHandle, DWORD Access, OBJECT_ATTRIBUTES *Attributes)
{
	LONG status;
	TypeNtOpenKey OpenKey;

	if(IniRegData.HooksEnabled) {
		EnterCriticalSection(&(IniRegData.Lock));
		IniLoad();
		status = IniRegKeyOpen(KeyHandle, Access, Attributes);
		LeaveCriticalSection(&(IniRegData.Lock));
		return status;
	}

	OpenKey = HookGetOEP(HOOK_PROC_NTOPENKEY);
	return OpenKey(KeyHandle, Access, Attributes);
}

// -------------------------------------------------------------------------------------------------

LONG __stdcall HookNtOpenKeyEx(HANDLE *KeyHandle, DWORD Access, OBJECT_ATTRIBUTES *Attributes, ULONG Options)
{
	LONG status;
	TypeNtOpenKeyEx OpenKeyEx;

	if(IniRegData.HooksEnabled) {
		EnterCriticalSection(&(IniRegData.Lock));
		IniLoad();
		status = IniRegKeyOpen(KeyHandle, Access, Attributes);
		LeaveCriticalSection(&(IniRegData.Lock));
		return status;
	}

	OpenKeyEx = HookGetOEP(HOOK_PROC_NTOPENKEYEX);
	return OpenKeyEx(KeyHandle, Access, Attributes, Options);
}

// -------------------------------------------------------------------------------------------------

LONG __stdcall HookNtCreateKey(HANDLE *KeyHandle, DWORD Access, OBJECT_ATTRIBUTES *Attributes,
							   ULONG TitleIndex, UNICODE_STRING *Class, ULONG CreateOptions, ULONG *Disposition)
{
	LONG status;
	TypeNtCreateKey CreateKey;

	if(IniRegData.HooksEnabled) {
		EnterCriticalSection(&(IniRegData.Lock));
		IniLoad();
		status = IniRegKeyCreate(KeyHandle, Access, Attributes, Disposition);
		LeaveCriticalSection(&(IniRegData.Lock));
		return status;
	}

	CreateKey = HookGetOEP(HOOK_PROC_NTCREATEKEY);
	return CreateKey(KeyHandle, Access, Attributes, TitleIndex, Class, CreateOptions, Disposition);
}

// -------------------------------------------------------------------------------------------------

LONG __stdcall HookNtCreateKeyTransacted(HANDLE *KeyHandle, DWORD Access, OBJECT_ATTRIBUTES *Attributes, ULONG TitleIndex,
										 UNICODE_STRING *Class, ULONG Options, HANDLE Transaction, ULONG *Disposition)
{
	LONG status;
	TypeNtCreateKeyTransacted CreateKey;

	if(IniRegData.HooksEnabled) {
		EnterCriticalSection(&(IniRegData.Lock));
		IniLoad();
		status = IniRegKeyCreate(KeyHandle, Access, Attributes, Disposition);
		LeaveCriticalSection(&(IniRegData.Lock));
		return status;
	}

	CreateKey = HookGetOEP(HOOK_PROC_NTCREATEKEYTRANSACTED);
	return CreateKey(KeyHandle, Access, Attributes, TitleIndex, Class, Options, Transaction, Disposition);
}

// -------------------------------------------------------------------------------------------------

LONG __stdcall HookNtQueryKey(HANDLE KeyHandle, int InfoClass, void *Buffer, ULONG BufLength, ULONG *ResultLength)
{
	LONG status;
	TypeNtQueryKey QueryKey;

	if(IniRegData.HooksEnabled) {
		EnterCriticalSection(&(IniRegData.Lock));
		IniLoad();
		status = IniRegKeyQuery(KeyHandle, InfoClass, Buffer, BufLength, ResultLength);
		LeaveCriticalSection(&(IniRegData.Lock));
		return status;
	}

	QueryKey = HookGetOEP(HOOK_PROC_NTQUERYKEY);
	return QueryKey(KeyHandle, InfoClass, Buffer, BufLength, ResultLength);
}

// -------------------------------------------------------------------------------------------------

LONG __stdcall HookNtEnumerateKey(HANDLE KeyHandle, ULONG Index, int InfoClass, void *Buffer, ULONG BufLength, ULONG *ResultLength)
{
	LONG status;
	TypeNtEnumerateKey EnumKey;

	if(IniRegData.HooksEnabled) {
		EnterCriticalSection(&(IniRegData.Lock));
		IniLoad();
		status = IniRegKeyEnum(KeyHandle, Index, InfoClass, Buffer, BufLength, ResultLength);
		LeaveCriticalSection(&(IniRegData.Lock));
		return status;
	}

	EnumKey = HookGetOEP(HOOK_PROC_NTENUMERATEKEY);
	return EnumKey(KeyHandle, Index, InfoClass, Buffer, BufLength, ResultLength);
}

// -------------------------------------------------------------------------------------------------

LONG __stdcall HookNtDeleteKey(HANDLE KeyHandle)
{
	LONG status;
	TypeNtDeleteKey DeleteKey;

	if(IniRegData.HooksEnabled) {
		EnterCriticalSection(&(IniRegData.Lock));
		IniLoad();
		status = IniRegKeyDelete(KeyHandle);
		LeaveCriticalSection(&(IniRegData.Lock));
		return status;
	}

	DeleteKey = HookGetOEP(HOOK_PROC_NTDELETEKEY);
	return DeleteKey(KeyHandle);
}

// -------------------------------------------------------------------------------------------------

LONG __stdcall HookNtFlushKey(HANDLE KeyHandle)
{
	return STATUS_SUCCESS;
}

// -------------------------------------------------------------------------------------------------

LONG __stdcall HookNtRenameKey(HANDLE KeyHandle, UNICODE_STRING *NewName)
{
	LONG status;
	TypeNtRenameKey RenameKey;

	if(IniRegData.HooksEnabled) {
		EnterCriticalSection(&(IniRegData.Lock));
		IniLoad();
		status = IniRegKeyRename(KeyHandle, NewName);
		LeaveCriticalSection(&(IniRegData.Lock));
		return status;
	}

	RenameKey = HookGetOEP(HOOK_PROC_NTDELETEKEY);
	return RenameKey(KeyHandle, NewName);
}

// -------------------------------------------------------------------------------------------------

LONG __stdcall HookNtQueryValueKey(HANDLE KeyHandle, UNICODE_STRING *ValueName, int InfoClass, void *Buffer, ULONG BufLength, ULONG *ResultLength)
{
	LONG status;
	TypeNtQueryValueKey QueryValueKey;

	if(IniRegData.HooksEnabled) {
		EnterCriticalSection(&(IniRegData.Lock));
		IniLoad();
		status = IniRegValueQuery(KeyHandle, ValueName, InfoClass, Buffer, BufLength, ResultLength);
		LeaveCriticalSection(&(IniRegData.Lock));
		return status;
	}

	QueryValueKey = HookGetOEP(HOOK_PROC_NTQUERYVALUEKEY);
	return QueryValueKey(KeyHandle, ValueName, InfoClass, Buffer, BufLength, ResultLength);
}

// -------------------------------------------------------------------------------------------------

LONG __stdcall HookNtQueryMultipleValueKey(HANDLE KeyHandle, KEY_VALUE_ENTRY *ValueEntries, ULONG EntryCount,
										   void *ValueBuffer, ULONG *BufLength, ULONG *RequiredLength)
{
	LONG status;
	TypeNtQueryMultipleValueKey QueryMultipleValue;

	if(IniRegData.HooksEnabled) {
		EnterCriticalSection(&(IniRegData.Lock));
		IniLoad();
		status = IniRegValueQueryMulti(KeyHandle, ValueEntries, EntryCount, ValueBuffer, BufLength, RequiredLength);
		LeaveCriticalSection(&(IniRegData.Lock));
		return status;
	}

	QueryMultipleValue = HookGetOEP(HOOK_PROC_NTQUERYMULTIPLEVALUEKEY);
	return QueryMultipleValue(KeyHandle, ValueEntries, EntryCount, ValueBuffer, BufLength, RequiredLength);
}

// -------------------------------------------------------------------------------------------------

LONG __stdcall HookNtEnumerateValueKey(HANDLE KeyHandle, ULONG Index, int InfoClass, void *Buffer, ULONG BufLength, ULONG *ResultLength)
{
	LONG status;
	TypeNtEnumerateValueKey EnumValueKey;

	if(IniRegData.HooksEnabled) {
		EnterCriticalSection(&(IniRegData.Lock));
		IniLoad();
		status = IniRegValueEnum(KeyHandle, Index, InfoClass, Buffer, BufLength, ResultLength);
		LeaveCriticalSection(&(IniRegData.Lock));
		return status;
	}

	EnumValueKey = HookGetOEP(HOOK_PROC_NTENUMERATEVALUEKEY);
	return EnumValueKey(KeyHandle, Index, InfoClass, Buffer, BufLength, ResultLength);
}

// -------------------------------------------------------------------------------------------------

LONG __stdcall HookNtSetValueKey(HANDLE KeyHandle, UNICODE_STRING *ValueName, ULONG TitleIndex, ULONG Type, void *Data, ULONG DataSize)
{
	LONG status;
	TypeNtSetValueKey SetValue;

	if(IniRegData.HooksEnabled) {
		EnterCriticalSection(&(IniRegData.Lock));
		IniLoad();
		status = IniRegValueSet(KeyHandle, ValueName, Type, Data, DataSize);
		LeaveCriticalSection(&(IniRegData.Lock));
		return status;
	}

	SetValue = HookGetOEP(HOOK_PROC_NTSETVALUEKEY);
	return SetValue(KeyHandle, ValueName, TitleIndex, Type, Data, DataSize);
}

// -------------------------------------------------------------------------------------------------

LONG __stdcall HookNtDeleteValueKey(HANDLE KeyHandle, UNICODE_STRING *ValueName)
{
	LONG status;
	TypeNtDeleteValueKey DeleteValue;

	if(IniRegData.HooksEnabled) {
		EnterCriticalSection(&(IniRegData.Lock));
		IniLoad();
		status = IniRegValueDelete(KeyHandle, ValueName);
		LeaveCriticalSection(&(IniRegData.Lock));
		return status;
	}

	DeleteValue = HookGetOEP(HOOK_PROC_NTDELETEVALUEKEY);
	return DeleteValue(KeyHandle, ValueName);
}

// -------------------------------------------------------------------------------------------------

static ULONG IniRegKeyOpen(HANDLE *pHandleId, DWORD Access, OBJECT_ATTRIBUTES *Attributes)
{
	ULONG status;
	INI_STATUS IniStatus;
	INIREG_HANDLE *Hdl, *ParentHdl;
	HANDLE SysHandle;
	int NodeExists = 0, NodeOpaque = 0;
	TypeNtOpenKey OpenKey;
	int FakeParentHandle;

	OpenKey = HookGetOEP(HOOK_PROC_NTOPENKEY);

	ParentHdl = NULL;
	FakeParentHandle = 0;

	// find (or create) parent node handle if specifed parent key
	if(Attributes->RootDirectory != NULL) {
		if((ParentHdl = IniRegHandleLookupOrWrap(Attributes->RootDirectory)) == NULL)
			return OpenKey(pHandleId, KEY_READ, Attributes);
		FakeParentHandle = ParentHdl->FakeHandle;
	}

	// initialize new handle for node
	if((status = IniRegHandleInit(&Hdl, ParentHdl, Attributes->ObjectName)) != STATUS_SUCCESS)
		return status;

	// check node state
	if((IniStatus = IniNodeStateInfo(Hdl->NodePath, &NodeExists, &NodeOpaque)) != INI_OK) {
		free(Hdl);
		return IniStatusToNtStatus(IniStatus);
	}

	// fake parent handle or opaque node (will not open registry key)
	if(FakeParentHandle || NodeOpaque) {

		// node not found
		if(!NodeExists) {
			free(Hdl);
			return STATUS_OBJECT_NAME_NOT_FOUND;
		}

		// return new handle
		*pHandleId = IniRegHandleAdd(Hdl, NULL);

		return STATUS_SUCCESS;
	}

	// node transparent or not exists, open registry key
	if((status = OpenKey(&SysHandle, KEY_READ, Attributes)) != STATUS_SUCCESS) {
		if(status != STATUS_OBJECT_NAME_NOT_FOUND) {
			free(Hdl);
			return status;
		}
		SysHandle = NULL;
	}

	// node not exists and registry key not exists
	if( (!NodeExists) && (!SysHandle) ) {
		free(Hdl);
		return STATUS_OBJECT_NAME_NOT_FOUND;
	}

	// return new handle
	*pHandleId = IniRegHandleAdd(Hdl, SysHandle);

	return STATUS_SUCCESS;
}

// -------------------------------------------------------------------------------------------------

static ULONG IniRegKeyCreate(HANDLE *pHandleId, DWORD Access, OBJECT_ATTRIBUTES *Attributes, ULONG *Disposition)
{
	ULONG status;
	INI_STATUS IniStatus;
	INIREG_HANDLE *Hdl, *ParentHdl;
	INI_NODE *Node;
	HANDLE SysHandle;
	int NodeExists = 0, NodeOpaque = 0, FakeParentHandle;
	TypeNtOpenKey OpenKey;
	int CreateNode;

	OpenKey = HookGetOEP(HOOK_PROC_NTOPENKEY);

	ParentHdl = NULL;
	FakeParentHandle = 0;

	// find (or create) parent node handle if specifed parent key
	if(Attributes->RootDirectory != NULL) {
		if((ParentHdl = IniRegHandleLookupOrWrap(Attributes->RootDirectory)) == NULL)
			return OpenKey(pHandleId, KEY_READ, Attributes);
		FakeParentHandle = ParentHdl->FakeHandle;
	}

	// initialize new node handle
	if((status = IniRegHandleInit(&Hdl, ParentHdl, Attributes->ObjectName)) != STATUS_SUCCESS)
		return status;

	// check node state
	if((IniStatus = IniNodeStateInfo(Hdl->NodePath, &NodeExists, &NodeOpaque)) != INI_OK) {
		free(Hdl);
		return IniStatusToNtStatus(IniStatus);
	}

	SysHandle = NULL;

	// have parent registry key handle and node is transparent, open registry key
	if( (!FakeParentHandle) && (!NodeOpaque) ) {

		if((status = OpenKey(&SysHandle, KEY_READ, Attributes)) != STATUS_SUCCESS) {
			if(status != STATUS_OBJECT_NAME_NOT_FOUND) {
				free(Hdl);
				return status;
			}
		}

	}

	// if registry key not exists and node not exists, create node
	CreateNode = ((!SysHandle) && (!NodeExists));

	if(CreateNode) {
		IniNodeCreateByPath(&Node, Hdl->NodePath);
		IniRegSaveRequest();
	}

	// return creation status
	if(Disposition != NULL) {
		*Disposition = CreateNode ? REG_CREATED_NEW_KEY : REG_OPENED_EXISTING_KEY;
	}

	// return new handle
	*pHandleId = IniRegHandleAdd(Hdl, SysHandle);

	return STATUS_SUCCESS;
}

// -------------------------------------------------------------------------------------------------

static ULONG IniRegKeyQuery(HANDLE KeyHandleId, int InfoClass, void *Buffer, ULONG BufSize, ULONG *ResultSize)
{
	KEY_BASIC_INFORMATION *BasicInfo;
	KEY_NODE_INFORMATION *NodeInfo;
	KEY_VALUE_FULL_INFORMATION *ValueInfo;
	KEY_FULL_INFORMATION *FullInfo;
	KEY_NAME_INFORMATION *NameInfo;
	KEY_CACHED_INFORMATION *CachedInfo;
	INIREG_HANDLE *Hdl;
	INI_STATUS IniStatus;
	INI_NODE *Node, *SubNode;
	INI_VALUE *Value;
	ULONG status;
	ULONG BufSizeRequired, NameSize;
	ULONG Index, SubKeyCount = 0, ValueCount = 0;
	ULONG MaxSubKeyNameSize = 0, MaxSubKeyClassSize = 0;
	ULONG MaxValueNameSize = 0, MaxValueDataSize = 0;
	ULONG InfoBufSize = 0, InfoBufReturnedSize = 0;
	void *InfoBuf = NULL, *TempMem;
	int NodeExists, NodeOpaque;
	TypeNtQueryKey QueryKey;
	TypeNtEnumerateKey EnumKey;
	TypeNtEnumerateValueKey EnumValue;
	WCHAR *String = NULL;
	ULONG StringSize = 0;

	QueryKey = HookGetOEP(HOOK_PROC_NTQUERYKEY);
	EnumKey = HookGetOEP(HOOK_PROC_NTENUMERATEKEY);
	EnumValue = HookGetOEP(HOOK_PROC_NTENUMERATEVALUEKEY);

	// find (or create) node handle
	if((Hdl = IniRegHandleLookupOrWrap(KeyHandleId)) == NULL) {
		return QueryKey(KeyHandleId, InfoClass, Buffer, BufSize, ResultSize);
	}

	// get node info
	if((IniStatus = IniNodeStateInfo(Hdl->NodePath, &NodeExists, &NodeOpaque)) != INI_OK)
		return IniStatusToNtStatus(IniStatus);

	// if node not exists
	if(!NodeExists) {

		// if not using transparent registry access, return error
		if( NodeOpaque || Hdl->FakeHandle )
			return STATUS_KEY_DELETED;

		// use registry key
		return QueryKey(KeyHandleId, InfoClass, Buffer, BufSize, ResultSize);
	}

	// lookup node
	if((IniStatus = IniNodeCreateByPath(&Node, Hdl->NodePath)) != INI_OK)
		return IniStatusToNtStatus(IniStatus);

	// collect subkey and value data if required one
	if( (InfoClass == KeyFullInformation) || (InfoClass == KeyCachedInformation) ) {

		// if using transparent registry access, collect data from registry key
		if( (!NodeOpaque) && (!Hdl->FakeHandle) ) {

			// allocate buffer for querying info
			InfoBufSize = 256;
			if((InfoBuf = malloc(InfoBufSize)) == NULL)
				return STATUS_NO_MEMORY;

			// enumerate subkeys from registry key
			for(Index = 0; ; Index++) {

				// get node (subkey name) information
				status = EnumKey(KeyHandleId, Index, KeyNodeInformation, InfoBuf, InfoBufSize, &InfoBufReturnedSize);
				if( (status == STATUS_BUFFER_OVERFLOW) || (status == STATUS_BUFFER_TOO_SMALL) ) {
					InfoBufSize = (InfoBufReturnedSize + 63) & ~63;
					if((TempMem = realloc(InfoBuf, InfoBufSize)) == NULL) {
						free(String);
						free(InfoBuf);
						return STATUS_NO_MEMORY;
					}
					InfoBuf = TempMem;
					status = EnumKey(KeyHandleId, Index, KeyNodeInformation, InfoBuf, InfoBufSize, &InfoBufReturnedSize);
				}

				if(status != STATUS_SUCCESS) {
					if(status == STATUS_NO_MORE_ENTRIES)
						break;
					free(String);
					free(InfoBuf);
					return status;
				}

				NodeInfo = InfoBuf;

				// get subkey name as zero-terminated string
				if(!MakeZeroString(&String, &StringSize, NodeInfo->Name, NodeInfo->NameLength)) {
					free(String);
					free(InfoBuf);
					return STATUS_NO_MEMORY;
				}

				// if not exists subnode with such name
				if((SubNode = IniNodeLookup(Node, String)) == NULL) {

					// increment subnode count
					SubKeyCount++;

					// update maximum subkey name size
					if(NodeInfo->NameLength > MaxSubKeyNameSize)
						MaxSubKeyNameSize = NodeInfo->NameLength;

					// update maximim class name size
					if( (NodeInfo->ClassOffset != 0xFFFFFFFF) && (NodeInfo->ClassOffset != 0) ) {
						if(NodeInfo->ClassLength > MaxSubKeyClassSize)
							MaxSubKeyClassSize = NodeInfo->ClassLength;
					}

				}
			}

			// enumerate values in registry key
			for(Index = 0; ; Index++) {

				// get value information
				status = EnumValue(KeyHandleId, Index, KeyValueFullInformation, InfoBuf, InfoBufSize, &InfoBufReturnedSize);
				if( (status == STATUS_BUFFER_OVERFLOW) || (status == STATUS_BUFFER_TOO_SMALL) ) {
					InfoBufSize = (InfoBufReturnedSize + 63) & ~63;
					if((TempMem = realloc(InfoBuf, InfoBufSize)) == NULL) {
						free(String);
						free(InfoBuf);
						return STATUS_NO_MEMORY;
					}
					InfoBuf = TempMem;
					status = EnumValue(KeyHandleId, Index, KeyValueFullInformation, InfoBuf, InfoBufSize, &InfoBufReturnedSize);
				}

				if(status != STATUS_SUCCESS) {
					if(status == STATUS_NO_MORE_ENTRIES)
						break;
					free(String);
					free(InfoBuf);
					return status;
				}

				ValueInfo = InfoBuf;

				// convert value name to zero-terminated string
				if(!MakeZeroString(&String, &StringSize, ValueInfo->Name, ValueInfo->NameLength)) {
					free(String);
					free(InfoBuf);
					return STATUS_NO_MEMORY;
				}

				// if not exists node value with such name
				if((Value = IniValueLookup(Node, String)) == NULL) {

					// increment value counter
					ValueCount++;

					// update value maximum name size
					if(ValueInfo->NameLength > MaxValueNameSize)
						MaxValueNameSize = ValueInfo->NameLength;

					// update value maximum data size
					if(ValueInfo->DataLength > MaxValueDataSize)
						MaxValueDataSize = ValueInfo->DataLength;

				}

			}

			free(String);
			free(InfoBuf);

		}

		// collect subnode data (count, maximum name size)
		for(Index = 0; Index < Node->Subnodes.Count; ++Index) {
			SubNode = Node->Subnodes.Node[Index];
			if(SubNode->State != INI_NODE_DELETED) {
				SubKeyCount++;
				NameSize = wcslen(SubNode->Name) * sizeof(WCHAR);
				if(NameSize > MaxSubKeyNameSize)
					MaxSubKeyNameSize = NameSize;
			}
		}

		// collect value data from node (count, max name and data size)
		for(Index = 0; Index < Node->Values.Count; ++Index) {
			Value = Node->Values.Value[Index];
			if(Value->Type != INI_VALUE_DELETED) {
				ValueCount++;
				NameSize = (Value->Name != NULL) ? (wcslen(Value->Name) * sizeof(WCHAR)) : 0;
				if(NameSize > MaxValueNameSize)
					MaxValueNameSize = NameSize;
				if(Value->DataSize > MaxValueDataSize)
					MaxValueDataSize = Value->DataSize;
			}
		}

	}

	// return required info
	switch(InfoClass) {

		// basic information
		case KeyBasicInformation:
			NameSize = wcslen(Node->Name) * sizeof(WCHAR);
			BufSizeRequired = sizeof(KEY_BASIC_INFORMATION) + NameSize;
			*ResultSize = BufSizeRequired;
			if(BufSize >= sizeof(KEY_BASIC_INFORMATION)) {
				BasicInfo = Buffer;
				BasicInfo->LastWriteTime.QuadPart = IniGetLastSaved();
				BasicInfo->TitleIndex = 0;
				BasicInfo->NameLength = NameSize;
				if(BufSize >= BufSizeRequired) {
					memcpy(BasicInfo->Name, Node->Name, NameSize);
					return STATUS_SUCCESS;
				}
				return STATUS_BUFFER_OVERFLOW;
			}
			return STATUS_BUFFER_TOO_SMALL;

		// node information
		case KeyNodeInformation:
			NameSize = wcslen(Node->Name) * sizeof(WCHAR);
			BufSizeRequired = sizeof(KEY_NODE_INFORMATION) + NameSize;
			*ResultSize = BufSizeRequired;
			if(BufSize >= sizeof(KEY_NODE_INFORMATION)) {
				NodeInfo = Buffer;
				NodeInfo->LastWriteTime.QuadPart = IniGetLastSaved();
				NodeInfo->TitleIndex = 0;
				NodeInfo->ClassOffset = 0xFFFFFFFF;
				NodeInfo->ClassLength = 0;
				NodeInfo->NameLength = NameSize;
				if(BufSize >= BufSizeRequired) {
					memcpy(NodeInfo->Name, Node->Name, NameSize);
					return STATUS_SUCCESS;
				}
				return STATUS_BUFFER_OVERFLOW;
			}
			return STATUS_BUFFER_TOO_SMALL;

		// full information
		case KeyFullInformation:
			*ResultSize = sizeof(KEY_FULL_INFORMATION);
			if(BufSize >= sizeof(KEY_FULL_INFORMATION)) {
				FullInfo = Buffer;
				FullInfo->LastWriteTime.QuadPart = IniGetLastSaved();
				FullInfo->TitleIndex = 0;
				FullInfo->ClassOffset = 0xFFFFFFFF;
				FullInfo->ClassLength = 0;
				FullInfo->SubKeys = SubKeyCount;
				FullInfo->MaxNameLen = MaxSubKeyNameSize;
				FullInfo->MaxClassLen = MaxSubKeyClassSize;
				FullInfo->Values = ValueCount;
				FullInfo->MaxValueNameLen = MaxValueNameSize;
				FullInfo->MaxValueDataLen = MaxValueDataSize;
				return STATUS_SUCCESS;
			}
			return STATUS_BUFFER_TOO_SMALL;

		// full name information
		case KeyNameInformation:
			NameSize = wcslen(Hdl->FullPath) * sizeof(WCHAR);
			BufSizeRequired = sizeof(KEY_NAME_INFORMATION) + NameSize;
			*ResultSize = BufSizeRequired;
			if(BufSize >= sizeof(KEY_NAME_INFORMATION)) {
				NameInfo = Buffer;
				NameInfo->NameLength = NameSize;
				if(BufSize >= BufSizeRequired) {
					memcpy(NameInfo->Name, Hdl->FullPath, NameSize);
					return STATUS_SUCCESS;
				}
				return STATUS_BUFFER_OVERFLOW;
			}
			return STATUS_BUFFER_TOO_SMALL;

		// cached (actualy return full) information
		case KeyCachedInformation:
			*ResultSize = sizeof(KEY_CACHED_INFORMATION);
			if(BufSize >= sizeof(KEY_CACHED_INFORMATION)) {
				CachedInfo = Buffer;
				CachedInfo->LastWriteTime.QuadPart = IniGetLastSaved();
				CachedInfo->TitleIndex = 0;
				CachedInfo->SubKeys = SubKeyCount;
				CachedInfo->MaxNameLen = MaxSubKeyNameSize;
				CachedInfo->Values = ValueCount;
				CachedInfo->MaxValueNameLen = MaxValueNameSize;
				CachedInfo->MaxValueDataLen = MaxValueDataSize;
				CachedInfo->NameLength = wcslen(Node->Name) * sizeof(WCHAR);
				return STATUS_SUCCESS;
			}
			return STATUS_BUFFER_TOO_SMALL;
	}

	return STATUS_INVALID_INFO_CLASS;
}

// -------------------------------------------------------------------------------------------------

static ULONG IniRegKeyEnum(HANDLE KeyHandleId, ULONG Index, int InfoClass, void *Buffer, ULONG BufSize, ULONG *ResultSize)
{
	KEY_BASIC_INFORMATION *BasicInfo;
	KEY_NODE_INFORMATION *NodeInfo;
	INIREG_HANDLE *Hdl;
	INI_NODE *Node, *SubNode;
	INI_STATUS IniStatus;
	int NodeExists, NodeOpaque;
	ULONG SkipKeys, KeyIndex;
	ULONG NameSize, MinBufSize, RequiredBufSize;
	OBJECT_ATTRIBUTES Attributes;
	UNICODE_STRING *UnicodeString;
	ULONG status;
	HANDLE SubKeyHandle;
	TypeNtEnumerateKey EnumerateKey;
	TypeNtClose Close;
	int AuxInfoClass;
	ULONG InfoBufSize = 0, InfoBufReturnedSize = 0;
	void *InfoBuf = NULL, *TempMem;
	WCHAR *SubKeyName = NULL, *NamePtr;
	ULONG SubKeyNameSize = 0;

	Close = HookGetOEP(HOOK_PROC_NTCLOSE);
	EnumerateKey = HookGetOEP(HOOK_PROC_NTENUMERATEKEY);

	// find (or create) node handle for registry key
	if((Hdl = IniRegHandleLookupOrWrap(KeyHandleId)) == NULL) {
		return EnumerateKey(KeyHandleId, Index, InfoClass, Buffer, BufSize, ResultSize);
	}

	// get node info state
	if((IniStatus = IniNodeStateInfo(Hdl->NodePath, &NodeExists, &NodeOpaque)) != INI_OK)
		return IniStatusToNtStatus(IniStatus);

	// node not exists
	if(!NodeExists) {

		// if not using transparent registry access, return error
		if( NodeOpaque || Hdl->FakeHandle )
			return STATUS_KEY_DELETED;

		// use registry key
		return EnumerateKey(KeyHandleId, Index, InfoClass, Buffer, BufSize, ResultSize);
	}

	// lookup node
	if((IniStatus = IniNodeCreateByPath(&Node, Hdl->NodePath)) != INI_OK)
		return IniStatusToNtStatus(IniStatus);

	SkipKeys = 0;

	// if initiating enum (or enum sequence break), reset enum indices
	if( (Index == 0) || (Index != Hdl->EnumKeyIndex + 1) ) {
		Hdl->EnumKeyIndexNode = 0xFFFFFFFF;
		Hdl->EnumKeyIndexReg  = 0xFFFFFFFF;
		SkipKeys = Index;
	}

	// enumerating node data
	if(Hdl->EnumKeyIndexReg == 0xFFFFFFFF) {

		KeyIndex = Hdl->EnumKeyIndexNode + 1;

		// find subnode by enumeration index
		SubNode = NULL;
		while(KeyIndex < Node->Subnodes.Count) {
			if(Node->Subnodes.Node[KeyIndex]->State != INI_NODE_DELETED) {
				if(SkipKeys == 0) {
					SubNode = Node->Subnodes.Node[KeyIndex];
					break;
				}
				SkipKeys--;
			}
			KeyIndex++;
		}

		// subnode found, return info
		if(SubNode != NULL) {

			switch(InfoClass) {

				// basic information for subnode
				case KeyBasicInformation:
					NameSize = wcslen(SubNode->Name) * sizeof(WCHAR);
					RequiredBufSize = sizeof(KEY_BASIC_INFORMATION) + NameSize;
					*ResultSize = RequiredBufSize;
					if(BufSize >= sizeof(KEY_BASIC_INFORMATION)) {
						BasicInfo = Buffer;
						BasicInfo->LastWriteTime.QuadPart = IniGetLastSaved();
						BasicInfo->TitleIndex = 0;
						BasicInfo->NameLength = NameSize;
						if(BufSize >= RequiredBufSize) {
							memcpy(BasicInfo->Name, SubNode->Name, NameSize);
							break;
						}
						return STATUS_BUFFER_OVERFLOW;
					}
					return STATUS_BUFFER_TOO_SMALL;

				// "node" information for subnode
				case KeyNodeInformation:
					NameSize = wcslen(SubNode->Name) * sizeof(WCHAR);
					RequiredBufSize = sizeof(KEY_NODE_INFORMATION) + NameSize;
					*ResultSize = RequiredBufSize;
					if(BufSize >= sizeof(KEY_NODE_INFORMATION)) {
						NodeInfo = Buffer;
						NodeInfo->LastWriteTime.QuadPart = IniGetLastSaved();
						NodeInfo->TitleIndex = 0;
						NodeInfo->ClassOffset = 0xFFFFFFFF;
						NodeInfo->ClassLength = 0;
						NodeInfo->NameLength = NameSize;
						if(BufSize >= RequiredBufSize) {
							memcpy(NodeInfo->Name, SubNode->Name, NameSize);
							break;
						}
						return STATUS_BUFFER_OVERFLOW;
					}
					return STATUS_BUFFER_TOO_SMALL;

				// use IniRegKeyQuery to get all required info...
				default:

					if((UnicodeString = WcharBufToUnicodeString(SubNode->Name)) == NULL)
						return STATUS_NO_MEMORY;

					memset(&Attributes, 0, sizeof(Attributes));
					Attributes.Length = sizeof(Attributes);
					Attributes.RootDirectory = KeyHandleId;
					Attributes.ObjectName = UnicodeString;
					Attributes.Attributes = 0x40;

					status = IniRegKeyOpen(&SubKeyHandle, KEY_READ, &Attributes);

					free(UnicodeString);

					if(status != STATUS_SUCCESS)
						return status;

					status = IniRegKeyQuery(SubKeyHandle, InfoClass, Buffer, BufSize, ResultSize);

					IniRegHandleClose(SubKeyHandle);

					if(status != STATUS_SUCCESS)
						return status;

					break;
			}

			// return success
			Hdl->EnumKeyIndex     = Index;
			Hdl->EnumKeyIndexNode = KeyIndex;
			return STATUS_SUCCESS;
		}
	}

	// enumerating registry sub-keys

	// if not using transparent registry access, return end of enumeration
	if( NodeOpaque || Hdl->FakeHandle )
		return STATUS_NO_MORE_ENTRIES;

	// info class type for querying subkey name
	switch(InfoClass) {
		case KeyNodeInformation:
			AuxInfoClass = KeyNodeInformation;
			break;
		default:
			AuxInfoClass = KeyBasicInformation;
			break;
	}

	// allocate buffer for querying data
	InfoBufSize = 256;
	if((InfoBuf = malloc(InfoBufSize)) == NULL)
		return STATUS_NO_MEMORY;

	// enumeration index
	KeyIndex = Hdl->EnumKeyIndexReg + 1;

	while(1) {

		// query subkey info
		status = EnumerateKey(KeyHandleId, KeyIndex, AuxInfoClass, InfoBuf, InfoBufSize, &InfoBufReturnedSize);
		if( (status == STATUS_BUFFER_OVERFLOW) || (status == STATUS_BUFFER_TOO_SMALL) ) {
			InfoBufSize = (InfoBufReturnedSize + 63) & ~63;
			if((TempMem = realloc(InfoBuf, InfoBufSize)) == NULL) {
				free(SubKeyName);
				free(InfoBuf);
				return STATUS_NO_MEMORY;
			}
			InfoBuf = TempMem;
			status = EnumerateKey(KeyHandleId, KeyIndex, AuxInfoClass, InfoBuf, InfoBufSize, &InfoBufReturnedSize);
		}

		if(status != ERROR_SUCCESS) {

			free(SubKeyName);
			free(InfoBuf);

			if( (status == STATUS_KEY_DELETED) && (Index != 0) )
				return STATUS_NO_MORE_ENTRIES;

			return status;
		}

		// get name from subkey info
		switch(AuxInfoClass) {
			case KeyBasicInformation:
				BasicInfo = InfoBuf;
				NamePtr = BasicInfo->Name;
				NameSize = BasicInfo->NameLength;
				break;
			case KeyNodeInformation:
				NodeInfo = InfoBuf;
				NamePtr = NodeInfo->Name;
				NameSize = NodeInfo->NameLength;
				break;
		}

		// convert subkey name to zero-terminated string
		if(!MakeZeroString(&SubKeyName, &SubKeyNameSize, NamePtr, NameSize)) {
			free(SubKeyName);
			free(InfoBuf);
			return STATUS_NO_MEMORY;
		}

		// if subnode with this name is not exists, break loop
		if((SubNode = IniNodeLookup(Node, SubKeyName)) == NULL) {
			if(SkipKeys == 0)
				break;
			SkipKeys--;
		}

		KeyIndex++;
	}

	// if queried info is required info, return it
	if(InfoClass == AuxInfoClass) {

		*ResultSize = InfoBufReturnedSize;

		switch(InfoClass) {
			case KeyBasicInformation:
				MinBufSize = sizeof(KEY_BASIC_INFORMATION);
				break;
			case KeyNodeInformation:
				MinBufSize = sizeof(KEY_NODE_INFORMATION);
				break;
		}

		if(BufSize >= MinBufSize) {
			if(BufSize >= InfoBufReturnedSize) {
				memcpy(Buffer, InfoBuf, InfoBufReturnedSize);
				status = STATUS_SUCCESS;
			} else {
				memcpy(Buffer, InfoBuf, MinBufSize);
				status = STATUS_BUFFER_OVERFLOW;
			}
		}

		else {
			status = STATUS_BUFFER_TOO_SMALL;
		}

	}

	// query required info
	else {

		if((UnicodeString = WcharBufToUnicodeString(SubKeyName)) == NULL) {
			status = STATUS_NO_MEMORY;
		}

		else {

			memset(&Attributes, 0, sizeof(Attributes));
			Attributes.Length = sizeof(Attributes);
			Attributes.RootDirectory = KeyHandleId;
			Attributes.ObjectName =	UnicodeString;
			Attributes.Attributes = 0x40;

			status = IniRegKeyOpen(&SubKeyHandle, KEY_READ, &Attributes);

			free(UnicodeString);

			if(status == STATUS_SUCCESS) {

				status = IniRegKeyQuery(SubKeyHandle, InfoClass, Buffer, BufSize, ResultSize);

				IniRegHandleClose(SubKeyHandle);

			}
		}

	}

	free(InfoBuf);
	free(SubKeyName);

	// update enumeration indices
	if(status == STATUS_SUCCESS) {
		Hdl->EnumKeyIndex    = Index;
		Hdl->EnumKeyIndexReg = KeyIndex;
	}

	// return status
	return status;
}

// -------------------------------------------------------------------------------------------------

static ULONG IniRegKeyDelete(HANDLE HandleId)
{
	INI_NODE *Node;
	INIREG_HANDLE *Hdl;
	INI_STATUS status;

	// lookup (or create) node handle
	if((Hdl = IniRegHandleLookupOrWrap(HandleId)) == NULL)
		return STATUS_ACCESS_DENIED;

	// lookup (or create) node
	if((status = IniNodeCreateByPath(&Node, Hdl->NodePath)) != INI_OK)
		return IniStatusToNtStatus(status);

	// set node deleted
	if((status = IniNodeDelete(Node)) == INI_OK)
		IniRegSaveRequest();

	return status;
}

// -------------------------------------------------------------------------------------------------

static ULONG IniRegKeyRename(HANDLE HandleId, UNICODE_STRING * NewName)
{
	UNICODE_STRING us;
	OBJECT_ATTRIBUTES attr;
	INIREG_HANDLE *HdlSrc, **pHdlSrc, *HdlTarget;
	int SrcNodeExists, SrcNodeOpaque;
	int TargetNodeExists, TargetNodeOpaque;
	WCHAR * TargetFullPath;
	ULONG nt_status;
	INI_STATUS status;
	INI_NODE *TargetNode, *SrcNode;
	HANDLE SysHandle;
	TypeNtOpenKey OpenKey;
	TypeNtClose CloseKey;

	OpenKey = HookGetOEP(HOOK_PROC_NTOPENKEY);
	CloseKey = HookGetOEP(HOOK_PROC_NTCLOSE);

	if(NewName->Length == 0)
		return STATUS_SUCCESS;

	// find (or create) source node handle
	if((HdlSrc = IniRegHandleLookupOrWrap(HandleId)) == NULL)
		return STATUS_ACCESS_DENIED;

	// find source node handle pointer
	if((pHdlSrc = IniRegHandleLookupPtr(HandleId)) == NULL)
		return STATUS_ACCESS_DENIED;

	// get node state info
	if((status = IniNodeStateInfo(HdlSrc->NodePath, &SrcNodeExists, &SrcNodeOpaque)) != INI_OK)
		return IniStatusToNtStatus(status);

	// if node not exists and not using transparent registry access, return error
	if( (!SrcNodeExists) && (SrcNodeOpaque || HdlSrc->FakeHandle) )
		return STATUS_KEY_DELETED;

	// modify full key path with new key name
	if((TargetFullPath = IniRegChangeNamePart(HdlSrc->FullPath, NewName)) == NULL)
		return STATUS_NO_MEMORY;

	// allocate new node handle with modified name
	us.Buffer = TargetFullPath;
	us.Length = wcslen(us.Buffer) * sizeof(WCHAR);
	us.MaximumLength = us.Length;

	if((nt_status = IniRegHandleInit(&HdlTarget, NULL, &us)) != STATUS_SUCCESS) {
		free(TargetFullPath);
		return nt_status;
	}

	// get state info for modified node name
	if((status = IniNodeStateInfo(HdlTarget->NodePath, &TargetNodeExists, &TargetNodeOpaque)) != INI_OK) {
		free(HdlTarget);
		free(TargetFullPath);
		return IniStatusToNtStatus(status);
	}

	// make sure target node not exists
	if(TargetNodeExists) {
		free(HdlTarget);
		free(TargetFullPath);
		return STATUS_CANNOT_DELETE;
	}

	// if using transparent access for target node,
	// make sure registry key for target node not exists
	if(!TargetNodeOpaque) {
		memset(&attr, 0, sizeof(attr));
		attr.Length = sizeof(attr);
		attr.ObjectName = &us;
		attr.Attributes = 0x640;

		if(OpenKey(&SysHandle, KEY_READ, &attr) == STATUS_SUCCESS) {
			CloseKey(SysHandle);
			free(HdlTarget);
			free(TargetFullPath);
			return STATUS_CANNOT_DELETE;
		}
	}

	free(TargetFullPath);

	// create target node
	if((status = IniNodeCreateByPath(&TargetNode, HdlTarget->NodePath)) != INI_OK) {
		free(HdlTarget);
		return IniStatusToNtStatus(status);
	}

	// if source node is not marked as deleted,
	// copy data to target node
	// and delete (mark as deleted) source node
	if( (!SrcNodeOpaque) || SrcNodeExists ) {

		if((status = IniNodeCreateByPath(&SrcNode, HdlSrc->NodePath)) != INI_OK) {
			free(HdlTarget);
			return IniStatusToNtStatus(status);
		}

		IniNodeCopy(TargetNode, SrcNode);
		IniNodeDelete(SrcNode);

	}

	// if using transparent access to registry key
	// copy data from key to target node
	if( (!SrcNodeOpaque) && (!HdlSrc->FakeHandle) ) {
		IniRegKeyCopyToNode(TargetNode, HandleId);
	}

	// copy handle data to new handle
	HdlTarget->IdHandle   = HandleId;
	HdlTarget->SysHandle  = NULL;
	HdlTarget->FakeHandle = 1;

	// replace node handle
	HdlTarget->Next = HdlSrc->Next;
	*pHdlSrc = HdlTarget;
	free(HdlSrc);

	IniRegSaveRequest();

	return STATUS_SUCCESS;
}

// -------------------------------------------------------------------------------------------------

static ULONG IniRegValueQuery(HANDLE KeyHandleId, UNICODE_STRING *usValueName, int InfoClass, void *Buffer, ULONG BufLength, ULONG *ResultLength)
{
	INI_NODE *Node;
	INI_VALUE *Value;
	INIREG_HANDLE *Hdl;
	INI_STATUS status;
	WCHAR *ValueName;
	int NodeExists, NodeOpaque;
	TypeNtQueryValueKey QueryValueKey;

	QueryValueKey = HookGetOEP(HOOK_PROC_NTQUERYVALUEKEY);

	// find (or create) node handle
	if((Hdl = IniRegHandleLookupOrWrap(KeyHandleId)) == NULL) {
		return QueryValueKey(KeyHandleId, usValueName, InfoClass, Buffer, BufLength, ResultLength);
	}

	// get node state info
	if((status = IniNodeStateInfo(Hdl->NodePath, &NodeExists, &NodeOpaque)) != INI_OK)
		return IniStatusToNtStatus(status);

	// node exists
	if(NodeExists) {

		// lookup node
		if((status = IniNodeCreateByPath(&Node, Hdl->NodePath)) != INI_OK)
			return IniStatusToNtStatus(status);

		// lookup value
		if((ValueName = UnicodeStringToWcharBuf(usValueName)) == NULL)
			return STATUS_NO_MEMORY;
		Value = IniValueLookup(Node, ValueName);
		free(ValueName);

		// value found, return required data
		if(Value != NULL) {
			if(Value->Type == INI_VALUE_DELETED)
				return STATUS_OBJECT_NAME_NOT_FOUND;
			return IniRegFillValueInfo(Value, InfoClass, Buffer, BufLength, ResultLength);
		}

		// value not found and no transparent registry access used, return error
		if(NodeOpaque || Hdl->FakeHandle)
			return STATUS_OBJECT_NAME_NOT_FOUND;

	}

	// node not found
	else {

		// node not found and no transparent registry access used, return error
		if(NodeOpaque || Hdl->FakeHandle)
			return STATUS_KEY_DELETED;

	}

	// node or value not found, use transparent registry access
	return QueryValueKey(KeyHandleId, usValueName, InfoClass, Buffer, BufLength, ResultLength);
}

// -------------------------------------------------------------------------------------------------

static ULONG IniRegValueQueryMulti(HANDLE KeyHandleId, KEY_VALUE_ENTRY *Entries, ULONG Count, void *Buffer, ULONG *BufSize, ULONG *RequiredSize)
{
	INI_STATUS status;
	INI_NODE *Node;
	INI_VALUE *Value;
	INIREG_HANDLE *Hdl;
	WCHAR *ValueNameBuf = NULL;
	ULONG ValueNameBufSize = 0;
	int NodeExists, NodeOpaque;
	char *EntriesFound = NULL;
	UNICODE_STRING *us;
	TypeNtQueryMultipleValueKey QueryMultiValue;
	ULONG nt_status, index, CountFound = 0, CountLeft;
	ULONG BufSizeAvailable, BufWritten, BufRequired;
	ULONG Result, Required;
	char *BufCursor;
	KEY_VALUE_ENTRY *EntriesLeft, *entry;
	int BufferOverrun = 0;

	QueryMultiValue = HookGetOEP(HOOK_PROC_NTQUERYMULTIPLEVALUEKEY);

	// no values queried, nothing to do
	if(Count == 0) {

		*BufSize = 0;
		*RequiredSize = 0;

		return STATUS_SUCCESS;

	}

	// lookup or create node handle
	if((Hdl = IniRegHandleLookupOrWrap(KeyHandleId)) == NULL) {
		return QueryMultiValue(KeyHandleId, Entries, Count, Buffer, BufSize, RequiredSize);
	}

	// get node handle info
	if((status = IniNodeStateInfo(Hdl->NodePath, &NodeExists, &NodeOpaque)) != INI_OK)
		return IniStatusToNtStatus(status);

	// node not exists
	if(!NodeExists) {

		// if no transparent access is used, return error
		if( NodeOpaque || Hdl->FakeHandle )
			return STATUS_KEY_DELETED;

		// query data from registry
		return QueryMultiValue(KeyHandleId, Entries, Count, Buffer, BufSize, RequiredSize);

	}

	BufRequired = 0;
	BufWritten = 0;

	BufSizeAvailable = *BufSize;

	BufCursor = Buffer;

	// find node
	if((status = IniNodeCreateByPath(&Node, Hdl->NodePath)) != INI_OK)
		return IniStatusToNtStatus(status);

	// allocate table for marking found entries
	if((EntriesFound = calloc(1, Count)) == NULL)
		return STATUS_NO_MEMORY;

	// look entries in node
	for(index = 0; index < Count; ++index) {

		us = Entries[index].ValueName;

		// convert entry name to zero-terminated string
		if(!MakeZeroString(&ValueNameBuf, &ValueNameBufSize, us->Buffer, us->Length)) {
			free(EntriesFound);
			free(ValueNameBuf);
			return STATUS_NO_MEMORY;
		}

		// search value in node by name
		if((Value = IniValueLookup(Node, ValueNameBuf)) != NULL) {

			// value marked as deleted, return error
			if(Value->Type == INI_VALUE_DELETED) {
				free(EntriesFound);
				free(ValueNameBuf);
				return STATUS_OBJECT_NAME_NOT_FOUND;
			}

			// not enough buffer space, flag overrun
			if(Value->DataSize > BufSizeAvailable) {
				BufferOverrun = 1;
			}

			// fill value info
			Entries[index].DataLength = Value->DataSize;
			Entries[index].DataOffset = 0;
			Entries[index].Type = IniToRegValueType(Value->Type);

			// copy value data if have enough space
			if(!BufferOverrun) {

				Entries[index].DataOffset = BufWritten;

				memcpy(BufCursor, Value->Data, Value->DataSize);

				BufWritten += Value->DataSize;
				BufCursor += Value->DataSize;
				BufSizeAvailable -= Value->DataSize;

			}

			BufRequired += Value->DataSize;

			// mark entry found
			EntriesFound[index] = 1;
			CountFound++;
		}

	}

	free(ValueNameBuf);

	// found all items, return success
	if(CountFound == Count) {

		free(EntriesFound);

		*BufSize = BufWritten;
		*RequiredSize = BufRequired;

		if(BufferOverrun) {
			return STATUS_BUFFER_OVERFLOW;
		}

		return STATUS_SUCCESS;

	}

	// not all items found and no transparent access, return error
	if( NodeOpaque || Hdl->FakeHandle ) {

		free(EntriesFound);

		return STATUS_OBJECT_NAME_NOT_FOUND;

	}

	// no items found in node, query data from registry
	if(CountFound == 0) {

		free(EntriesFound);

		return QueryMultiValue(KeyHandleId, Entries, Count, Buffer, BufSize, RequiredSize);

	}

	// prepare table for querying items left
	CountLeft = Count - CountFound;
	if((EntriesLeft = calloc(CountLeft, sizeof(KEY_VALUE_ENTRY))) == NULL) {
		free(EntriesFound);
		return STATUS_NO_MEMORY;
	}

	entry = EntriesLeft;
	for(index = 0; index < Count; ++index) {
		if(!EntriesFound[index]) {
			entry->ValueName = Entries[index].ValueName;
			entry++;
		}
	}

	// query left items from registry
	if(!BufferOverrun) {
		Result = BufSizeAvailable;
		nt_status = QueryMultiValue(KeyHandleId, EntriesLeft, CountLeft, BufCursor, &Result, &Required);
	} else {
		Result = 0;
		nt_status = QueryMultiValue(KeyHandleId, EntriesLeft, CountLeft, NULL, &Result, &Required);
	}

	if(nt_status != STATUS_SUCCESS) {

		if(nt_status != STATUS_BUFFER_OVERFLOW) {
			free(EntriesLeft);
			free(EntriesFound);
			return nt_status;
		}

		BufferOverrun = 1;

	}

	// copy queried entries to main table
	entry = EntriesLeft;
	for(index = 0; index < Count; index++) {
		if(!EntriesFound[index]) {
			Entries[index].DataLength = entry->DataLength;
			Entries[index].DataOffset = entry->DataOffset + BufWritten;
			Entries[index].Type = entry->Type;
			entry++;
		}
	}

	// increment counters
	BufWritten += Result;
	BufRequired += Required;

	free(EntriesLeft);
	free(EntriesFound);

	*BufSize = BufWritten;
	*RequiredSize = BufRequired;

	return (BufferOverrun ? STATUS_BUFFER_OVERFLOW : STATUS_SUCCESS);
}

// -------------------------------------------------------------------------------------------------

static ULONG IniRegValueEnum(HANDLE KeyHandleId, ULONG Index, int InfoClass, void *Buffer, ULONG BufSize, ULONG *ResultSize)
{
	INI_NODE *Node;
	INI_VALUE *Value;
	INIREG_HANDLE *Hdl;
	TypeNtEnumerateValueKey EnumerateValueKey;
	int NodeExists, NodeOpaque;
	ULONG NtStatus, ValueIndex, SkipValues, InfoClassWithName;
	INI_STATUS status;
	void *InfoBuf, *temp, *ValueNamePtr;
	ULONG InfoBufSize, InfoBufReturnSize;
	ULONG ValueNameLen, ValueNameSize;
	ULONG RequiredBufSize;
	WCHAR *ValueName;
	KEY_VALUE_BASIC_INFORMATION *BasicInfo;
	KEY_VALUE_FULL_INFORMATION *FullInfo;
	KEY_VALUE_PARTIAL_INFORMATION *PartialInfo;

	EnumerateValueKey = HookGetOEP(HOOK_PROC_NTENUMERATEVALUEKEY);

	// find or create node handle
	if((Hdl = IniRegHandleLookupOrWrap(KeyHandleId)) == NULL) {
		return EnumerateValueKey(KeyHandleId, Index, InfoClass, Buffer, BufSize, ResultSize);
	}

	// get node state info
	if((status = IniNodeStateInfo(Hdl->NodePath, &NodeExists, &NodeOpaque)) != INI_OK)
		return IniStatusToNtStatus(status);

	// node not exists
	if(!NodeExists) {

		// if not using transparent registry access return error
		if(NodeOpaque || Hdl->FakeHandle)
			return STATUS_KEY_DELETED;

		// query data from registry
		return EnumerateValueKey(KeyHandleId, Index, InfoClass, Buffer, BufSize, ResultSize);
	}

	// find this node
	if((status = IniNodeCreateByPath(&Node, Hdl->NodePath)) != INI_OK)
		return IniStatusToNtStatus(status);

	SkipValues = 0;

	// if initiating enumeration or sequence break, reset enum indices
	// (on sequence break, set skip value count)
	if( (Index == 0) || (Index != Hdl->EnumValueIndex + 1) ) {
		Hdl->EnumValueIndexNode = 0xFFFFFFFF;
		Hdl->EnumValueIndexReg  = 0xFFFFFFFF;
		SkipValues = Index;
	}

	// enumerating values in node
	if(Hdl->EnumValueIndexReg == 0xFFFFFFFF) {

		ValueIndex = Hdl->EnumValueIndexNode + 1;

		// find value by enumeration index
		Value = NULL;
		while(ValueIndex < Node->Values.Count) {
			if(Node->Values.Value[ValueIndex]->Type != INI_VALUE_DELETED) {
				if(SkipValues == 0) {
					Value = Node->Values.Value[ValueIndex];
					break;
				}
				SkipValues--;
			}
			ValueIndex++;
		}

		// if value found, return its info
		if(Value != NULL) {
			if((NtStatus = IniRegFillValueInfo(Value, InfoClass, Buffer, BufSize, ResultSize)) == STATUS_SUCCESS) {
				Hdl->EnumValueIndex     = Index;
				Hdl->EnumValueIndexNode = ValueIndex;
			}
			return NtStatus;
		}
	}

	// if not used transparent registry access enumeration done
	if(NodeOpaque || Hdl->FakeHandle)
		return STATUS_NO_MORE_ENTRIES;

	// select info class for querying value name
	switch(InfoClass) {
		case KeyValueBasicInformation:
		case KeyValueFullInformation:
			InfoClassWithName = InfoClass;
			break;
		case KeyValuePartialInformation:
			InfoClassWithName = KeyValueFullInformation;
			break;
		default:
			return STATUS_INVALID_INFO_CLASS;
	}

	// allocate buffer for querying info
	InfoBufSize = (BufSize > 64) ? BufSize : 64;
	if((InfoBuf = malloc(InfoBufSize)) == NULL)
		return STATUS_NO_MEMORY;

	ValueIndex = Hdl->EnumValueIndexReg + 1;

	while(1) {

		// query value info and name
		NtStatus = EnumerateValueKey(KeyHandleId, ValueIndex, InfoClassWithName, InfoBuf, InfoBufSize, &InfoBufReturnSize);
		if( (NtStatus == STATUS_BUFFER_OVERFLOW) || (NtStatus == STATUS_BUFFER_TOO_SMALL) ) {
			InfoBufSize = (InfoBufReturnSize + 63) & ~63;
			if((temp = realloc(InfoBuf, InfoBufSize)) == NULL) {
				free(InfoBuf);
				return STATUS_NO_MEMORY;
			}
			InfoBuf = temp;
			NtStatus = EnumerateValueKey(KeyHandleId, ValueIndex, InfoClassWithName, InfoBuf, InfoBufSize, &InfoBufReturnSize);
		}

		if(NtStatus != STATUS_SUCCESS) {
			free(InfoBuf);

			if( (NtStatus == STATUS_KEY_DELETED) && (Index != 0) )
				return STATUS_NO_MORE_ENTRIES;

			return NtStatus;
		}

		// get name from queried info
		switch(InfoClassWithName) {

			case KeyValueBasicInformation:
				BasicInfo = InfoBuf;
				ValueNameLen = BasicInfo->NameLength / sizeof(WCHAR);
				ValueNamePtr = BasicInfo->Name;
				break;

			case KeyValueFullInformation:
				FullInfo = InfoBuf;
				ValueNameLen = FullInfo->NameLength / sizeof(WCHAR);
				ValueNamePtr = FullInfo->Name;
				break;

			default:
				free(InfoBuf);
				return STATUS_UNSUCCESSFUL;
		}

		// convert value name to zero-terminated string
		ValueNameSize = (ValueNameLen + 1) * sizeof(WCHAR);
		if((ValueName = malloc(ValueNameSize)) == NULL) {
			free(InfoBuf);
			return STATUS_NO_MEMORY;
		}

		memcpy(ValueName, ValueNamePtr, ValueNameLen * sizeof(WCHAR));
		ValueName[ValueNameLen] = 0;

		// lookup value from node by this name
		Value = IniValueLookup(Node, ValueName);

		free(ValueName);

		// if no such value in node, break loop
		if(Value == NULL) {
			if(SkipValues == 0)
				break;
			SkipValues--;
		}

		ValueIndex++;
	}

	// queried info type is requested type, return info
	if(InfoClassWithName == InfoClass) {

		*ResultSize = InfoBufReturnSize;

		if(BufSize < InfoBufReturnSize) {

			switch(InfoClass) {

				case KeyValueBasicInformation:
					if(BufSize >= sizeof(KEY_VALUE_BASIC_INFORMATION)) {
						memcpy(Buffer, InfoBuf, sizeof(KEY_VALUE_BASIC_INFORMATION));
						free(InfoBuf);
						return STATUS_BUFFER_OVERFLOW;
					}
					break;

				case KeyValueFullInformation:
					if(BufSize >= sizeof(KEY_VALUE_FULL_INFORMATION)) {
						memcpy(Buffer, InfoBuf, sizeof(KEY_VALUE_FULL_INFORMATION));
						free(InfoBuf);
						return STATUS_BUFFER_OVERFLOW;
					}
					break;

			}

			return STATUS_BUFFER_TOO_SMALL;
		}

		memcpy(Buffer, InfoBuf, InfoBufReturnSize);

		free(InfoBuf);

		Hdl->EnumValueIndex    = Index;
		Hdl->EnumValueIndexReg = ValueIndex;
		return STATUS_SUCCESS;
	}

	// convert queried info to required type and return
	if( (InfoClass == KeyValuePartialInformation) && (InfoClassWithName == KeyValueFullInformation) ) {

		FullInfo = InfoBuf;

		PartialInfo = Buffer;
		RequiredBufSize = sizeof(KEY_VALUE_PARTIAL_INFORMATION) + FullInfo->DataLength;

		if(BufSize >= sizeof(KEY_VALUE_PARTIAL_INFORMATION)) {

			PartialInfo->TitleIndex = FullInfo->TitleIndex;
			PartialInfo->Type = FullInfo->Type;
			PartialInfo->DataLength = FullInfo->DataLength;

			if(BufSize >= RequiredBufSize) {

				memcpy(PartialInfo->Data, (char*)FullInfo + FullInfo->DataOffset, FullInfo->DataLength);
				free(InfoBuf);

				Hdl->EnumValueIndex    = Index;
				Hdl->EnumValueIndexReg = ValueIndex;
				return STATUS_SUCCESS;
			}

			free(InfoBuf);
			return STATUS_BUFFER_OVERFLOW;
		}

		free(InfoBuf);
		return STATUS_BUFFER_TOO_SMALL;
	}

	free(InfoBuf);
	return STATUS_UNSUCCESSFUL;
}

// -------------------------------------------------------------------------------------------------

static ULONG IniRegValueSet(HANDLE KeyHandleId, UNICODE_STRING *usValueName, ULONG Type, void *Data, ULONG DataSize)
{
	INI_NODE *Node;
	INIREG_HANDLE *Hdl;
	INI_STATUS status;
	WCHAR *ValueName;
	INI_VALUE_TYPE ValueType;

	// find or create node handle
	if((Hdl = IniRegHandleLookupOrWrap(KeyHandleId)) == NULL)
		return STATUS_ACCESS_DENIED;

	// find or create node
	if((status = IniNodeCreateByPath(&Node, Hdl->NodePath)) != INI_OK)
		return IniStatusToNtStatus(status);

	// convert value name to zero-terminated string
	if((ValueName = UnicodeStringToWcharBuf(usValueName)) == NULL)
		return STATUS_NO_MEMORY;

	// set value
	ValueType = RegToIniValueType(Type);

	if((status = IniValueSet(Node, ValueName, ValueType, Data, DataSize)) == INI_OK)
		IniRegSaveRequest();

	free(ValueName);

	return IniStatusToNtStatus(status);
}

// -------------------------------------------------------------------------------------------------

static ULONG IniRegValueDelete(HANDLE KeyHandleId, UNICODE_STRING *usValueName)
{
	INI_NODE *Node;
	INIREG_HANDLE *Hdl;
	INI_STATUS status;
	WCHAR *ValueName;

	// find or create node handle
	if((Hdl = IniRegHandleLookupOrWrap(KeyHandleId)) == NULL)
		return STATUS_ACCESS_DENIED;

	// find or create node
	if((status = IniNodeCreateByPath(&Node, Hdl->NodePath)) != INI_OK)
		return IniStatusToNtStatus(status);

	// convert value name to zero-terminated string
	if((ValueName = UnicodeStringToWcharBuf(usValueName)) == NULL)
		return STATUS_NO_MEMORY;

	// try to delete value
	status = IniValueDelete(Node, ValueName);

	// if value not found, create value and mark as deleted
	if( (status == INI_NOTFOUND) && (Node->State == INI_NODE_TRANSPARENT) ) {
		status = IniValueSetRaw(Node, ValueName, INI_VALUE_DELETED, NULL, 0);
	}

	// request to save data
	if(status == INI_OK) {
		IniRegSaveRequest();
	}

	free(ValueName);

	return IniStatusToNtStatus(status);
}

// -------------------------------------------------------------------------------------------------

static ULONG IniRegKeyCopyToNode(INI_NODE *Node, HANDLE KeyHandle)
{
	KEY_BASIC_INFORMATION *BasicInfo;
	KEY_VALUE_FULL_INFORMATION *ValueInfo;
	ULONG index, status, InfoBufSize, InfoResultSize;
	WCHAR *NameBuf = NULL;
	ULONG NameBufSize = 0;
	UNICODE_STRING SubKeyName;
	OBJECT_ATTRIBUTES SubKeyAttr;
	HANDLE SubKeyHandle;
	INI_NODE *Subnode;
	INI_STATUS ini_status;
	void *InfoBuf, *InfoBufTemp;
	TypeNtOpenKey OpenKey;
	TypeNtClose CloseKey;
	TypeNtEnumerateKey EnumKey;
	TypeNtEnumerateValueKey EnumValue;
	INI_VALUE_TYPE IniType;
	void *ValueData;

	OpenKey = HookGetOEP(HOOK_PROC_NTOPENKEY);
	CloseKey = HookGetOEP(HOOK_PROC_NTCLOSE);
	EnumKey = HookGetOEP(HOOK_PROC_NTENUMERATEKEY);
	EnumValue = HookGetOEP(HOOK_PROC_NTENUMERATEVALUEKEY);

	// allocate buffer for querying data
	InfoBufSize = 256;
	if((InfoBuf = malloc(InfoBufSize)) == NULL)
		return STATUS_NO_MEMORY;

	// enumerate subkeys
	BasicInfo = InfoBuf;

	for(index = 0; ; index++) {

		// query subkey info
		status = EnumKey(KeyHandle, index, KeyBasicInformation, BasicInfo, InfoBufSize, &InfoResultSize);
		if((status == STATUS_BUFFER_TOO_SMALL) || (status == STATUS_BUFFER_OVERFLOW)) {
			InfoBufSize = (InfoResultSize + 63) & ~63;
			if((InfoBufTemp = realloc(InfoBuf, InfoBufSize)) == NULL) {
				free(NameBuf);
				free(InfoBuf);
				return STATUS_NO_MEMORY;
			}
			BasicInfo = InfoBuf = InfoBufTemp;
			status = EnumKey(KeyHandle, index, KeyBasicInformation, BasicInfo, InfoBufSize, &InfoResultSize);
		}

		if(status != STATUS_SUCCESS) {
			if(status == STATUS_NO_MORE_ENTRIES)
				break;
			free(NameBuf);
			free(InfoBuf);
			return status;
		}

		// convert subkey name to zero-terminated string
		if(!MakeZeroString(&NameBuf, &NameBufSize, BasicInfo->Name, BasicInfo->NameLength)) {
			free(NameBuf);
			free(InfoBuf);
			return STATUS_NO_MEMORY;
		}

		// if target subnode already exists, ignore subkey
		if(IniNodeLookup(Node, NameBuf) != NULL)
			continue;

		// create subnode and copy subkey data
		SubKeyName.Length = (USHORT)(BasicInfo->NameLength);
		SubKeyName.MaximumLength = (USHORT)(BasicInfo->NameLength);
		SubKeyName.Buffer = BasicInfo->Name;

		memset(&SubKeyAttr, 0, sizeof(SubKeyAttr));
		SubKeyAttr.Length = sizeof(SubKeyAttr);
		SubKeyAttr.RootDirectory = KeyHandle;
		SubKeyAttr.ObjectName = &SubKeyName;
		SubKeyAttr.Attributes = 0x40; // case insensitive

		if((ini_status = IniNodeCreate(&Subnode, Node, NameBuf)) != INI_OK) {
			free(NameBuf);
			free(InfoBuf);
			return IniStatusToNtStatus(ini_status);
		}

		if((status = OpenKey(&SubKeyHandle, KEY_READ, &SubKeyAttr)) == STATUS_SUCCESS) {
			IniRegKeyCopyToNode(Subnode, SubKeyHandle);
			CloseKey(SubKeyHandle);
		}
	}

	// enumerate values
	ValueInfo = InfoBuf;

	for(index = 0; ; index++) {

		// query value information
		status = EnumValue(KeyHandle, index, KeyValueFullInformation, ValueInfo, InfoBufSize, &InfoResultSize);
		if((status == STATUS_BUFFER_TOO_SMALL) || (status == STATUS_BUFFER_OVERFLOW)) {
			InfoBufSize = (InfoResultSize + 63) & ~63;
			if((InfoBufTemp = realloc(InfoBuf, InfoBufSize)) == NULL) {
				free(NameBuf);
				free(InfoBuf);
				return STATUS_NO_MEMORY;
			}
			ValueInfo = InfoBuf = InfoBufTemp;
			status = EnumValue(KeyHandle, index, KeyValueFullInformation, ValueInfo, InfoBufSize, &InfoResultSize);
		}

		if(status != STATUS_SUCCESS) {
			if(status == STATUS_NO_MORE_ENTRIES)
				break;
			free(NameBuf);
			free(InfoBuf);
			return status;
		}

		// convert value name to zero-terminated string
		if(!MakeZeroString(&NameBuf, &NameBufSize, ValueInfo->Name, ValueInfo->NameLength)) {
			free(NameBuf);
			free(InfoBuf);
			return STATUS_NO_MEMORY;
		}

		// if target value exists, ignore value from regkey
		if(IniValueLookup(Node, NameBuf) != NULL)
			continue;

		// set value
		IniType = RegToIniValueType(ValueInfo->Type);
		ValueData = (char*)ValueInfo + ValueInfo->DataOffset;

		IniValueSet(Node, NameBuf, IniType, ValueData, ValueInfo->DataLength);
	}

	free(NameBuf);
	free(InfoBuf);

	return STATUS_SUCCESS;
}

// -------------------------------------------------------------------------------------------------

static ULONG IniRegFillValueInfo(INI_VALUE *Value, int InfoClass, void *Buf, ULONG BufSize, ULONG *ResultSize)
{
	KEY_VALUE_BASIC_INFORMATION *BasicInfo;
	KEY_VALUE_PARTIAL_INFORMATION *PartialInfo;
	KEY_VALUE_FULL_INFORMATION *FullInfo;
	ULONG NameSize, Size, SizeWithoutData;

	// fill information for value
	switch(InfoClass) {

		// name only
		case KeyValueBasicInformation:

			NameSize = (Value->Name != NULL) ? (wcslen(Value->Name) * sizeof(WCHAR)) : 0;
			Size = sizeof(KEY_VALUE_BASIC_INFORMATION) + NameSize;

			BasicInfo = Buf;
			*ResultSize = Size;

			if(BufSize >= sizeof(KEY_VALUE_BASIC_INFORMATION)) {

				BasicInfo->TitleIndex = 0;
				BasicInfo->Type = IniToRegValueType(Value->Type);
				BasicInfo->NameLength = NameSize;

				if(BufSize >= Size) {
					BasicInfo->TitleIndex = 0;
					BasicInfo->Type = IniToRegValueType(Value->Type);
					BasicInfo->NameLength = NameSize;
					memcpy(BasicInfo->Name, Value->Name, NameSize);
					return STATUS_SUCCESS;
				}

				return STATUS_BUFFER_OVERFLOW;
			}

			return STATUS_BUFFER_TOO_SMALL;

		// data and name
		case KeyValueFullInformation:

			NameSize = (Value->Name != NULL) ? (wcslen(Value->Name) * sizeof(WCHAR)) : 0;
			SizeWithoutData = sizeof(KEY_VALUE_FULL_INFORMATION) + NameSize;
			Size = SizeWithoutData + Value->DataSize;

			FullInfo = Buf;
			*ResultSize = Size;

			if(BufSize >= sizeof(KEY_VALUE_FULL_INFORMATION)) {

				FullInfo->TitleIndex = 0;
				FullInfo->Type = IniToRegValueType(Value->Type);
				FullInfo->DataOffset = 0;
				FullInfo->DataLength = Value->DataSize;
				FullInfo->NameLength = NameSize;

				if(BufSize >= SizeWithoutData) {

					memcpy(FullInfo->Name, Value->Name, NameSize);

					if(BufSize >= Size) {
						FullInfo->DataOffset = SizeWithoutData;
						memcpy((char*)FullInfo + SizeWithoutData, Value->Data, Value->DataSize);

						return STATUS_SUCCESS;
					}
				}

				return STATUS_BUFFER_OVERFLOW;
			}

			return STATUS_BUFFER_TOO_SMALL;

		// data only
		case KeyValuePartialInformation:

			Size = sizeof(KEY_VALUE_PARTIAL_INFORMATION) + Value->DataSize;

			PartialInfo = Buf;
			*ResultSize = Size;

			if(BufSize >= sizeof(KEY_VALUE_PARTIAL_INFORMATION)) {

				PartialInfo->TitleIndex = 0;
				PartialInfo->Type = IniToRegValueType(Value->Type);
				PartialInfo->DataLength = Value->DataSize;

				if(BufSize >= Size) {
					memcpy(PartialInfo->Data, Value->Data, Value->DataSize);
					return STATUS_SUCCESS;
				}

				return STATUS_BUFFER_OVERFLOW;
			}

			return STATUS_BUFFER_TOO_SMALL;
	}

	return STATUS_INVALID_INFO_CLASS;
}

// -------------------------------------------------------------------------------------------------

static int IniRegHandleClose(HANDLE IdHandle)
{
	int success = 0;
	INIREG_HANDLE **pHdl, *Hdl;
	TypeNtClose Close;

	Close = HookGetOEP(HOOK_PROC_NTCLOSE);

	// find node handle
	if((pHdl = IniRegHandleLookupPtr(IdHandle)) != NULL) {

		// remove node handle
		Hdl = *pHdl;
		*pHdl = Hdl->Next;

		// close handle or fake handle
		Close(Hdl->IdHandle);

		// free node handle
		free(Hdl);

		return 1;
	}

	return 0;
}

// -------------------------------------------------------------------------------------------------

static HANDLE IniRegHandleAdd(INIREG_HANDLE *Hdl, HANDLE SysHandle)
{
	ULONG HandleListIndex;
	INIREG_HANDLE **pHandleList;
	HANDLE IdHandle;

	// use registry key handle
	if(SysHandle != NULL) {
		IdHandle = SysHandle;
		Hdl->FakeHandle = 0;
		Hdl->CloseIdHandle = 0;
	}
	// use fake handle (event)
	else {
		IdHandle = CreateEvent(NULL, TRUE, TRUE, NULL);
		Hdl->FakeHandle = 1;
		Hdl->CloseIdHandle = 1;
	}

	// set handle values
	Hdl->IdHandle = IdHandle;
	Hdl->SysHandle = SysHandle;

	// select node handle list
	HandleListIndex = ((ULONG)(Hdl->IdHandle) >> 2) % INIREG_HANDLE_LISTS;
	pHandleList = IniRegData.HandleList + HandleListIndex;

	// add node handle to list start
	Hdl->Next = *pHandleList;
	*pHandleList = Hdl;

	return IdHandle;
}

// -------------------------------------------------------------------------------------------------

static INIREG_HANDLE *IniRegHandleLookup(HANDLE IdHandle)
{
	INIREG_HANDLE **pHdl;

	if((pHdl = IniRegHandleLookupPtr(IdHandle)) == NULL)
		return NULL;
	return *pHdl;
}

// -------------------------------------------------------------------------------------------------

static INIREG_HANDLE **IniRegHandleLookupPtr(HANDLE IdHandle)
{
	int HandleListIndex;
	INIREG_HANDLE **pHandleList, **pHdl;
	ULONG Hdl1, Hdl2;

	// select node handle list to lookup
	HandleListIndex = ((ULONG)(IdHandle) >> 2) % INIREG_HANDLE_LISTS;
	pHandleList = IniRegData.HandleList + HandleListIndex;

	// compare handles ignoring 2 lsb
	Hdl2 = (ULONG)IdHandle & ~3;

	// lookup list
	for(pHdl = pHandleList; *pHdl != NULL; pHdl = &((*pHdl)->Next)) {

		Hdl1 = (ULONG)((*pHdl)->IdHandle) & ~3;

		if(Hdl1 == Hdl2) {
			return pHdl;
		}

	}

	return NULL;
}

// -------------------------------------------------------------------------------------------------

static ULONG IniRegHandleWrap(INIREG_HANDLE **pHdl, HANDLE SysHandle)
{
	INIREG_HANDLE *Hdl;
	KEY_NAME_INFORMATION *NameInfo, *TempNameInfo;
	ULONG NameInfoSize, NameInfoReturnedSize;
	ULONG status;
	TypeNtQueryKey QueryKey;
	WCHAR *KeyPath = NULL;
	ULONG KeyPathSize = 0;

	QueryKey = HookGetOEP(HOOK_PROC_NTQUERYKEY);

	// allocate buffer for querying registry key name
	NameInfoSize = 1024;
	if((NameInfo = malloc(NameInfoSize)) == NULL)
		return STATUS_NO_MEMORY;

	// query registry key absolute name
	status = QueryKey(SysHandle, KeyNameInformation, NameInfo, NameInfoSize, &NameInfoReturnedSize);
	if((status == STATUS_BUFFER_OVERFLOW) || (status == STATUS_BUFFER_TOO_SMALL)) {
		if((TempNameInfo = realloc(NameInfo, NameInfoReturnedSize)) == NULL) {
			free(NameInfo);
			return STATUS_NO_MEMORY;
		}
		NameInfoSize = NameInfoReturnedSize;
		NameInfo = TempNameInfo;
		status = QueryKey(SysHandle, KeyNameInformation, NameInfo, NameInfoSize, &NameInfoReturnedSize);
	}

	if(status != STATUS_SUCCESS) {
		free(NameInfo);
		return status;
	}

	// convert registry key absolute name to zero-terminated string
	if(!MakeZeroString(&KeyPath, &KeyPathSize, NameInfo->Name, NameInfo->NameLength)) {
		free(NameInfo);
		return STATUS_NO_MEMORY;
	}

	free(NameInfo);

	// allocate new node handle
	Hdl = IniRegHandleAlloc(KeyPath);

	free(KeyPath);

	if(Hdl == NULL) {
		return STATUS_NO_MEMORY;
	}

	// add node handle to list
	IniRegHandleAdd(Hdl, SysHandle);
	return STATUS_SUCCESS;
}

// -------------------------------------------------------------------------------------------------

static ULONG IniRegHandleInit(INIREG_HANDLE **pHdl, INIREG_HANDLE *ParentHdl, UNICODE_STRING *usSubNodePath)
{
	INIREG_HANDLE *Hdl;
	WCHAR *SubNodePath, *FullPath;
	ULONG SubNodePathLen, FullPathLen;

	// convert sub-node path to zero-terminated string
	if((SubNodePath = UnicodeStringToWcharBuf(usSubNodePath)) == NULL)
		return STATUS_NO_MEMORY;

	SubNodePathLen = wcslen(SubNodePath);

	// remove trailing backslashes
	while( (SubNodePathLen > 0) && (SubNodePath[SubNodePathLen - 1] == L'\\') )
		SubNodePathLen--;
	SubNodePath[SubNodePathLen] = 0;

	// if used parent handle, make full node path
	if(ParentHdl != NULL) {

		// allocate buffer for full path
		FullPathLen = wcslen(ParentHdl->FullPath) + 1 + SubNodePathLen;
		if((FullPath = malloc((FullPathLen + 1) * sizeof(WCHAR))) == NULL) {
			free(SubNodePath);
			return STATUS_NO_MEMORY;
		}

		// make full path
		wcscpy(FullPath, ParentHdl->FullPath);
		if(SubNodePathLen > 0) {
			wcscat(FullPath, L"\\");
			wcscat(FullPath, SubNodePath);
		}

		// allocate new handle
		Hdl = IniRegHandleAlloc(FullPath);

		free(FullPath);

	}

	// use absolute path
	else {

		// allocate new handle
		Hdl = IniRegHandleAlloc(SubNodePath);

	}

	free(SubNodePath);


	if(Hdl == NULL)
		return STATUS_NO_MEMORY;

	*pHdl = Hdl;

	return STATUS_SUCCESS;
}

// -------------------------------------------------------------------------------------------------

static INIREG_HANDLE * IniRegHandleAlloc(WCHAR *FullPath)
{
	INIREG_HANDLE *Hdl;
	ULONG HdlSize, FullPathSize, NodePathSize;
	WCHAR *NodePath;
	char *cursor;

	// make node path from registry key path
	if((NodePath = IniRegMakeNodePath(FullPath)) == NULL)
		return NULL;

	// calculate size for node handle buffer
	FullPathSize = (wcslen(FullPath) + 1) * sizeof(WCHAR);
	NodePathSize = (wcslen(NodePath) + 1) * sizeof(WCHAR);
	HdlSize = sizeof(INIREG_HANDLE) + FullPathSize + NodePathSize;

	// allocate node handle buffer
	if((Hdl = malloc(HdlSize)) == NULL) {
		free(NodePath);
		return NULL;
	}

	cursor = (void*)Hdl;
	cursor += sizeof(INIREG_HANDLE);

	// fill node handle info
	Hdl->Next					= NULL;

	Hdl->IdHandle				= NULL;
	Hdl->SysHandle				= NULL;

	Hdl->NodePath				= (void*)cursor;
	cursor += NodePathSize;
	Hdl->FullPath				= (void*)cursor;
	cursor += FullPathSize;

	Hdl->FakeHandle				= 0;

	Hdl->EnumValueIndex			= 0xFFFFFFFF;
	Hdl->EnumValueIndexNode		= 0xFFFFFFFF;
	Hdl->EnumValueIndexReg		= 0xFFFFFFFF;

	Hdl->EnumKeyIndex			= 0xFFFFFFFF;
	Hdl->EnumKeyIndexNode		= 0xFFFFFFFF;
	Hdl->EnumKeyIndexReg		= 0xFFFFFFFF;

	memcpy(Hdl->NodePath, NodePath, NodePathSize);
	memcpy(Hdl->FullPath, FullPath, FullPathSize);

	free(NodePath);

	return Hdl;
}

// -------------------------------------------------------------------------------------------------

static WCHAR *IniRegChangeNamePart(WCHAR *FullPathSrc, UNICODE_STRING *usNewName)
{
	WCHAR *FullPath, *namepart, *temp;
	ULONG FullPathSize;
	WCHAR *NewNameBuf = NULL;
	ULONG NewNameBufSize = 0;

	// duplicate node path
	if((FullPath = wcsdup(FullPathSrc)) == NULL)
		return NULL;

	// find name part in path
	if((namepart = wcsrchr(FullPath, L'\\')) == NULL) {
		free(FullPath);
		return NULL;
	}
	// remove name part (keep backslash)
	*(namepart + 1) = 0;

	// convert new part to zero-terminated string
	if(!MakeZeroString(&NewNameBuf, &NewNameBufSize, usNewName->Buffer, usNewName->Length)) {
		free(FullPath);
		return NULL;
	}

	// expand or shrink buffer for node path
	FullPathSize = (wcslen(FullPath) + wcslen(NewNameBuf) + 1) * sizeof(WCHAR);
	if((temp = realloc(FullPath, FullPathSize)) == NULL) {
		free(NewNameBuf);
		free(FullPath);
		return NULL;
	}
	FullPath = temp;

	// append new name part
	wcscat(FullPath, NewNameBuf);

	free(NewNameBuf);

	return FullPath;
}

// -------------------------------------------------------------------------------------------------

static WCHAR * IniRegMakeNodePath(WCHAR *FullPath)
{
	WCHAR *PathStart, *Path;
	WCHAR *Directory = NULL;
	ULONG PathLen;

	PathStart = FullPath;

	// \REGISTRY\USERS tree
	if(wcsnicmp(PathStart, IniRegData.UsersPath, IniRegData.UsersPathLen) == 0) {

		// \REGISTRY\USERS\(current user SID) replaced by HKCU
		if( (IniRegData.CurrentUserPath != NULL) &&
			(wcsnicmp(PathStart, IniRegData.CurrentUserPath, IniRegData.CurrentUserPathLen) == 0) )
		{
			PathStart += IniRegData.CurrentUserPathLen;
			Directory = L"HKEY_CURRENT_USER";
		}

		// replace by HKU
		else {
			PathStart += IniRegData.UsersPathLen;
			Directory = L"HKEY_USERS";
		}
	}

	// \REGISTRY\MACHINE
	else if(wcsnicmp(PathStart, IniRegData.MachinePath, IniRegData.MachinePathLen) == 0) {

		// \REGISTRY\MACHINE\Software\Classes replaced by HKCR
		if(wcsnicmp(PathStart, IniRegData.ClassesRootPath, IniRegData.ClassesRootPathLen) == 0) {
			PathStart += IniRegData.ClassesRootPathLen;
			Directory = L"HKEY_CLASSES_ROOT";
		}

		// oter replaced by HKLM
		else {
			PathStart += IniRegData.MachinePathLen;
			Directory = L"HKEY_LOCAL_MACHINE";
		}
	}

	// remove leading backslashes
	while(*PathStart == L'\\')
		PathStart++;

	// return path
	if(Directory == NULL) {
		if((Path = wcsdup(PathStart)) == NULL)
			return NULL;
		PathLen = wcslen(Path);
	}
	// append directory and return path
	else {
		PathLen = wcslen(Directory) + 1 + wcslen(PathStart);
		Path = malloc((PathLen + 1) * sizeof(WCHAR));
		swprintf(Path, L"%s\\%s", Directory, PathStart);
	}

	// remove trailing backslashes
	while( (PathLen > 0) && (Path[PathLen - 1] == L'\\') )
		PathLen--;
	Path[PathLen] = 0;

	return Path;
}

// -------------------------------------------------------------------------------------------------

static INIREG_HANDLE *IniRegHandleLookupOrWrap(HANDLE SysHandle)
{
	INIREG_HANDLE *Hdl;

	// lookup existing node handle
	if((Hdl = IniRegHandleLookup(SysHandle)) != NULL)
		return Hdl;

	// create new node handle for registry key handle
	if(IniRegHandleWrap(&Hdl, SysHandle) == STATUS_SUCCESS)
		return Hdl;

	return NULL;
}

// -------------------------------------------------------------------------------------------------

static UNICODE_STRING *WcharBufToUnicodeString(WCHAR *Buf)
{
	ULONG Size, StringLen, StringSize;
	UNICODE_STRING *UnicodeString;

	StringLen = wcslen(Buf);
	StringSize = (StringLen + 1) * sizeof(WCHAR);
	Size = sizeof(UNICODE_STRING) + StringSize;
	if((UnicodeString = malloc(Size)) == NULL)
		return NULL;
	UnicodeString->Length = (USHORT)(StringLen * sizeof(WCHAR));
	UnicodeString->MaximumLength = (USHORT)(StringSize);
	UnicodeString->Buffer = (void*)((char*)UnicodeString + sizeof(UNICODE_STRING));
	memcpy(UnicodeString->Buffer, Buf, StringSize);
	return UnicodeString;
}

// -------------------------------------------------------------------------------------------------

static WCHAR *UnicodeStringToWcharBuf(UNICODE_STRING *UnicodeString)
{
	WCHAR *Buf;
	ULONG NumChars, StringSize;

	NumChars = UnicodeString->Length / sizeof(WCHAR);
	StringSize = (NumChars + 1) * sizeof(WCHAR);

	if((Buf = malloc(StringSize)) == NULL)
		return NULL;
	memcpy(Buf, UnicodeString->Buffer, NumChars * sizeof(WCHAR));
	Buf[NumChars] = 0;

	return Buf;
}

// -------------------------------------------------------------------------------------------------

static int MakeZeroString(WCHAR **pBuf, ULONG *pBufSize, WCHAR *Data, ULONG DataSize)
{
	ULONG BufSize = *pBufSize;
	WCHAR *Buf = *pBuf;
	ULONG Len, RequiredSize;

	Len = DataSize / sizeof(WCHAR);
	RequiredSize = (Len + 1) * sizeof(WCHAR);

	if(BufSize < RequiredSize) {
		RequiredSize = (RequiredSize + 15) & ~15;
		if((Buf = realloc(Buf, RequiredSize)) == NULL)
			return 0;
		*pBuf = Buf;
		*pBufSize = RequiredSize;
	}

	memcpy(Buf, Data, Len * sizeof(WCHAR));
	Buf[Len] = 0;

	return 1;
}


// -------------------------------------------------------------------------------------------------

static ULONG IniStatusToNtStatus(INI_STATUS IniStatus)
{
	switch(IniStatus) {
		case INI_OK:			return STATUS_SUCCESS;
		case INI_NOMEM:			return STATUS_NO_MEMORY;
		case INI_NOTFOUND:		return STATUS_OBJECT_NAME_NOT_FOUND;
		default:				return STATUS_UNSUCCESSFUL;
	}
}

// -------------------------------------------------------------------------------------------------

static INI_VALUE_TYPE RegToIniValueType(ULONG RegType)
{
	switch(RegType) {
		case REG_NONE:			return INI_VALUE_NOTYPE;
		case REG_BINARY:		return INI_VALUE_BINARY;
		case REG_DWORD:			return INI_VALUE_DWORD;
		case REG_QWORD:			return INI_VALUE_QWORD;
		case REG_SZ:			return INI_VALUE_STRING;
		case REG_EXPAND_SZ:		return INI_VALUE_STRING_ENV;
		case REG_MULTI_SZ:		return INI_VALUE_STRING_MULTI;
		default:				return INI_VALUE_NOTYPE;
	}
}

// -------------------------------------------------------------------------------------------------

static ULONG IniToRegValueType(INI_VALUE_TYPE ValueType)
{
	switch(ValueType) {
		case INI_VALUE_NOTYPE:			return REG_NONE;
		case INI_VALUE_BINARY:			return REG_BINARY;
		case INI_VALUE_DWORD:			return REG_DWORD;
		case INI_VALUE_QWORD:			return REG_QWORD;
		case INI_VALUE_STRING:			return REG_SZ;
		case INI_VALUE_STRING_ENV:		return REG_EXPAND_SZ;
		case INI_VALUE_STRING_MULTI:	return REG_MULTI_SZ;
		default:						return REG_NONE;
	}
}

// -------------------------------------------------------------------------------------------------

static void IniRegGetCurrentUserPath()
{
	UNICODE_STRING usUserPath;
	TypeRtlFormatCurrentUserKeyPath FormatUserPath;
	TypeRtlFreeUnicodeString FreeString;
	HANDLE NtDll;

	// find procedures
	if((NtDll = GetModuleHandle(L"ntdll.dll")) == NULL)
		return;

	FormatUserPath = (void*)GetProcAddress(NtDll, "RtlFormatCurrentUserKeyPath");
	FreeString = (void*)GetProcAddress(NtDll, "RtlFreeUnicodeString");
	if( (!FormatUserPath) || (!FreeString) )
		return;

	// get registry key path for current user
	if(FormatUserPath(&usUserPath) != STATUS_SUCCESS)
		return;

	// convert key path to zero-terminated string and calculate its length
	if((IniRegData.CurrentUserPath = UnicodeStringToWcharBuf(&usUserPath)) != NULL) {
		IniRegData.CurrentUserPathLen = wcslen(IniRegData.CurrentUserPath);
	}

	FreeString(&usUserPath);
}

// -------------------------------------------------------------------------------------------------

static void IniRegInitPathes()
{
	IniRegGetCurrentUserPath();

	IniRegData.ClassesRootPath		= L"\\REGISTRY\\MACHINE\\Software\\Classes";
	IniRegData.MachinePath			= L"\\REGISTRY\\MACHINE";
	IniRegData.UsersPath			= L"\\REGISTRY\\USER";

	IniRegData.ClassesRootPathLen	= wcslen(IniRegData.ClassesRootPath);
	IniRegData.MachinePathLen		= wcslen(IniRegData.MachinePath);
	IniRegData.UsersPathLen			= wcslen(IniRegData.UsersPath);
}

// -------------------------------------------------------------------------------------------------

static void IniRegCleanupPathes()
{
	free(IniRegData.CurrentUserPath);
}

// -------------------------------------------------------------------------------------------------

void IniRegSaveRequest()
{
	IniRegData.SaveRequestTick = GetTickCount();
	SetEvent(IniRegData.SaveRequest);
}

// -------------------------------------------------------------------------------------------------

enum {
	SAVE_THREAD_EVENT_STOP,
	SAVE_THREAD_EVENT_SAVE,
	SAVE_THREAD_EVENT_COUNT,
};

DWORD WINAPI IniRegSaveThread(void *Param)
{
	DWORD WaitMs = INFINITE, Event, Tick, Elapsed;
	HANDLE Events[SAVE_THREAD_EVENT_COUNT];

	// watch event list
	Events[SAVE_THREAD_EVENT_STOP] = IniRegData.StopRequest;
	Events[SAVE_THREAD_EVENT_SAVE] = IniRegData.SaveRequest;

	while(1) {

		// watch events or timer
		Event = WaitForMultipleObjects(SAVE_THREAD_EVENT_COUNT, Events, FALSE, WaitMs);

		switch(Event) {

			// stop event, return
			case SAVE_THREAD_EVENT_STOP:
				return 0;

			case SAVE_THREAD_EVENT_SAVE:
			case WAIT_TIMEOUT:

				// get elapsed time from save request
				Tick = GetTickCount();
				Elapsed = Tick - IniRegData.SaveRequestTick;
				// timeout not expired, calculate time to wait
				if(Elapsed < (IniRegData.SaveTimeout - 25)) {
					WaitMs = IniRegData.SaveTimeout - Elapsed;
				}
				// timeout expired, save data
				else {
					EnterCriticalSection(&(IniRegData.Lock));
					IniSave();
					LeaveCriticalSection(&(IniRegData.Lock));
					WaitMs = INFINITE;
				}
				break;
		}

	}

	return 0;
}

// -------------------------------------------------------------------------------------------------

static int IniRegInitSaveThread()
{
	// create events
	IniRegData.SaveRequest = CreateEvent(NULL, FALSE, FALSE, NULL);
	IniRegData.StopRequest = CreateEvent(NULL, FALSE, FALSE, NULL);
	if( (IniRegData.StopRequest) && (IniRegData.SaveRequest) )
	{
		// spawn thread
		if((IniRegData.SaveThread = CreateThread(NULL, 0, IniRegSaveThread, NULL, 0, NULL)) != NULL)
			return 1;

		if(IniRegData.StopRequest) CloseHandle(IniRegData.StopRequest);
		if(IniRegData.SaveRequest) CloseHandle(IniRegData.SaveRequest);
	}
	return 0;
}

// -------------------------------------------------------------------------------------------------

static void IniRegStopSaveThread()
{
	// stop thread
	SetEvent(IniRegData.StopRequest);
	WaitForSingleObject(IniRegData.SaveThread, INFINITE);

	// close handles
	CloseHandle(IniRegData.SaveThread);
	CloseHandle(IniRegData.StopRequest);
	CloseHandle(IniRegData.SaveRequest);
}

// -------------------------------------------------------------------------------------------------

INI_STATUS IniRegInitialize(WCHAR *Filename, ULONG SaveTimeout)
{
	INI_STATUS status;

	// initialize ini storage
	if((status = IniInitialize(Filename)) == INI_OK)
	{
		// initialize data
		memset(&IniRegData, 0, sizeof(IniRegData));

		IniRegData.SaveTimeout = SaveTimeout;

		IniRegInitPathes();

		// create critical section
		InitializeCriticalSection(&(IniRegData.Lock));

		// spawn data save thread
		if(IniRegInitSaveThread())
		{

			IniRegData.HooksEnabled = 1;

			return INI_OK;
		}

		IniCleanup();
	}

	return status;
}

// -------------------------------------------------------------------------------------------------

void IniRegCleanup()
{
	int index;
	INIREG_HANDLE *Hdl, *HdlNext;

	if(IniRegData.HooksEnabled) {

		IniRegData.HooksEnabled = 0;

		// stop save thread
		IniRegStopSaveThread();

		// delete critical section
		DeleteCriticalSection(&(IniRegData.Lock));

		// free handle lists
		for(index = 0; index < INIREG_HANDLE_LISTS; index++) {
			for(Hdl = IniRegData.HandleList[index]; Hdl != NULL; Hdl = HdlNext) {
				HdlNext = Hdl->Next;
				if(Hdl->CloseIdHandle) {
					CloseHandle(Hdl->IdHandle);
				}
				free(Hdl);
			}
			IniRegData.HandleList[index] = NULL;
		}

		// free data
		IniRegCleanupPathes();

		// cleanup ini storage
		IniCleanup();

	}
}

// -------------------------------------------------------------------------------------------------
