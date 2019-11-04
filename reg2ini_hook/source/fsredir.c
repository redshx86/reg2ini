// -------------------------------------------------------------------------------------------------

#include "fsredir.h"

// -------------------------------------------------------------------------------------------------

static TypeNtOpenProcessToken OpenToken;
static TypeNtQueryInformationToken QueryToken;
static TypeRtlConvertSidToUnicodeString FormatSid;
static TypeRtlFreeUnicodeString FreeString;

static TypeNtOpenKey OpenKey;
static TypeNtClose CloseHdl;
static TypeNtQueryValueKey QueryValue;
static TypeNtEnumerateValueKey EnumValue;

// -------------------------------------------------------------------------------------------------

static FSR_DATA FsrData;

// -------------------------------------------------------------------------------------------------

static DWORD FsrStatusToWinError(FSR_STATUS status)
{
	switch(status) {
		case FSR_OK:			return ERROR_SUCCESS;
		case FSR_BADPATH:
		case FSR_DEVICEPATH:	return ERROR_INVALID_NAME;
		default:				return ERROR_NOT_ENOUGH_MEMORY;
	}
}

// -------------------------------------------------------------------------------------------------

static void FsrFreeKeepError(void *block)
{
	DWORD error;

	error = GetLastError();
	free(block);
	SetLastError(error);
}

// -------------------------------------------------------------------------------------------------

static int FsrIsValidPathChar(WCHAR c)
{
	switch(c) {
		case 0: case L'\"': case L'\\':
		case L'|': case L'/': case L'?':
		case L'<': case L'>': case L'*':
			return 0;
		default:
			return 1;
	}
}

// -------------------------------------------------------------------------------------------------

static int FsrIsPathSeparator(WCHAR c)
{
	return ((c == L'\\') || (c == '/'));
}

// -------------------------------------------------------------------------------------------------

static int FsrIsDriveLetter(WCHAR c)
{
	return ( ((c >= 'A') && (c <= 'Z')) ||
		((c >= 'a') && (c <= 'z')) );
}

// -------------------------------------------------------------------------------------------------

static void FsrReplaceSlashes(R2I_WSTR *Wstr)
{
	WCHAR *p = Wstr->Buf;

	while((p = wcschr(p, L'/')) != NULL)
		*(p++) = L'\\';
}

// -------------------------------------------------------------------------------------------------

static int FsrAppendPath(R2I_WSTR **pPath, WCHAR *AppendBuf, DWORD AppendLength)
{
	R2I_WSTR *Path = *pPath;
	WCHAR c1, c2;

	if(AppendLength == WSTR_LENGTH_UNKNOWN)
		AppendLength = wcslen(AppendBuf);

	if( (Path->Length != 0) && (AppendLength != 0) ) {
		c1 = Path->Buf[Path->Length - 1];
		c2 = AppendBuf[0];
		if( (!FsrIsPathSeparator(c1)) && (!FsrIsPathSeparator(c2)) ) {
			if(!WstrAppend(pPath, L"\\", 1))
				return 0;
		}
	}

	return WstrAppend(pPath, AppendBuf, AppendLength);
}

// -------------------------------------------------------------------------------------------------

static int FsrGetRootPath(R2I_WSTR *Path)
{
	DWORD i;

	// for absolute path like "X:\..." keep 3 chars
	if( FsrIsDriveLetter(Path->Buf[0]) && (Path->Buf[1] == L':') && (Path->Buf[2] == L'\\') ) {
		WstrDelete(Path, 3, WSTR_END);
		return 1;
	}

	// for network path like "\\server\...", keep server name (no trailing BS)
	if( (Path->Buf[0] == L'\\') && (Path->Buf[1] == L'\\') && FsrIsValidPathChar(Path->Buf[2]) ) {
		for(i = 3; (Path->Buf[i] != L'\\') && (Path->Buf[i] != 0); ++i);
		WstrDelete(Path, i, WSTR_END);
		return 1;
	}

	return 0;
}

// -------------------------------------------------------------------------------------------------

static int FsrIsDosDeviceName(WCHAR *Name)
{
	static WCHAR *DosDeviceNames[] = {
		L"LPT", L"COM", L"PRN", L"AUX",
		L"NUL", L"CON"
	};

	DWORD len;
	int i;

	len = wcslen(Name);
	if((len < 3) || (len > 4))
		return 0;

	for(i = 0; i < 6; ++i) {
		if(wcsnicmp(Name, DosDeviceNames[i], 3) == 0)
			break;
	}

	if((i <= 1) && (len == 4) && (isdigit(Name[3])))
		return 1;

	if((i >= 2) && (i <= 5) && (len == 3))
		return 1;

	return 0;
}

// -------------------------------------------------------------------------------------------------

static FSR_PATH_TYPE FsrGetPathType(WCHAR *Path, WCHAR **pNonRoot)
{
	DWORD i;

	*pNonRoot = Path;

	// UNC path beginning with "\\"
	if( (Path[0] == L'\\') && (Path[1] == L'\\') ) {
		// special path beginning with "\\?\" or "\\.\"
		if( ((Path[2] == L'?') || (Path[2] == L'.')) && (Path[3] == L'\\') ) {
			// dos device name "\\?\X:\" or "\\.\X:\"
			if( FsrIsDriveLetter(Path[4]) && (Path[5] == L':') && (Path[6] == L'\\') ) {
				*pNonRoot += 7;
				return FSR_PATH_ABSOLUTE_LONG;
			}
			// network path "\\?\UNC\server"
			if( (Path[2] == L'?') && (wcsnicmp(Path + 4, L"UNC\\", 4) == 0) && FsrIsValidPathChar(Path[8]) ) {
				for(i = 9; (Path[i - 1] != L'\\') && (Path[i] != 0); Path++)
					;
				*pNonRoot += i;
				return FSR_PATH_NETWORK_LONG;
			}
			// device or special path
			return FSR_PATH_DEVICE;
		}
		// network path "\\server"
		if(FsrIsValidPathChar(Path[2])) {
			for(i = 3; (Path[i - 1] != L'\\') && (Path[i] != 0); i++)
				;
			*pNonRoot += i;
			return FSR_PATH_NETWORK;
		}
		// Unknown "\\" path
		return FSR_PATH_UNKNOWN;
	}

	// drive letter "X:..."
	if( FsrIsDriveLetter(Path[0]) && (Path[1] == L':') ) {
		// absolute path "X:\..."
		if(Path[2] == L'\\') {
			*pNonRoot += 3;
			return FSR_PATH_ABSOLUTE;
		}
		// drive-relative path "X:..."
		*pNonRoot += 2;
		return FSR_PATH_DRIVE_RELATIVE;
	}

	// current drive relative "\..." path
	if(Path[0] == L'\\') {
		*pNonRoot += 1;
		return FSR_PATH_CURDRIVE_RELATIVE;
	}

	// dos device
	if(FsrIsDosDeviceName(Path)) {
		return FSR_PATH_DEVICE;
	}

	// current directory relative path
	return FSR_PATH_CURDIR_RELATIVE;
}

// -------------------------------------------------------------------------------------------------

static FSR_STATUS FsrNormalizePath(R2I_WSTR **pNormalizedPath, WCHAR **pNonRoot, 
								   WCHAR *SourcePath, DWORD SourcePathLen, int AddLongPrefix)
{
	FSR_STATUS status = FSR_ERROR;
	FSR_PATH_TYPE PathType;
	R2I_WSTR *Path = NULL, *FullPath = NULL;
	WCHAR *NonRoot, *Cursor;
	WCHAR *PartName, *NextPartName;
	DWORD FullPathLen, CurDirLen;
	DWORD RootPartLen, PartNameLen, i;

	// copy source path to new buffer
	if((Path = WstrDup(SourcePath, SourcePathLen, MAX_PATH)) == NULL)
		goto __no_mem;

	// replace slashes to backslashes
	FsrReplaceSlashes(Path);

	// get path type
	if((PathType = FsrGetPathType(Path->Buf, &NonRoot)) == FSR_PATH_UNKNOWN) {
		status = FSR_BADPATH;
		goto __error_exit;
	}

	if(PathType == FSR_PATH_DEVICE) {
		status = FSR_DEVICEPATH;
		goto __error_exit;
	}

	// "\\?\X:\" long absolute path -> "X:\" absolute path
	if(PathType == FSR_PATH_ABSOLUTE_LONG) {
		WstrDelete(Path, 0, 4);
	}

	// "\\?\UNC\server" long network path -> "\\server" network path
	if(PathType == FSR_PATH_NETWORK_LONG) {
		WstrDelete(Path, 2, 6);
	}

	// expand current drive or current directory relative path
	if( (PathType == FSR_PATH_CURDIR_RELATIVE) || (PathType == FSR_PATH_CURDRIVE_RELATIVE) ) {
		// allocate buffer for full path
		if((FullPath = WstrAlloc(MAX_PATH)) == NULL)
			goto __no_mem;
		// get current directory and save to full path buffer
		CurDirLen = GetCurrentDirectory(FullPath->BufLength, FullPath->Buf);
		if(CurDirLen > FullPath->BufLength) {
			if(!WstrExpand(&FullPath, CurDirLen + Path->Length + 1))
				goto __no_mem;
			CurDirLen = GetCurrentDirectory(FullPath->BufLength, FullPath->Buf);
		}
		if((CurDirLen == 0) || (CurDirLen >= FullPath->BufLength))
			goto __error_exit;
		FullPath->Length = CurDirLen;
		// make sure current directory have no forward slashes
		FsrReplaceSlashes(FullPath);
		// get root from current directory for curdrive-relative path
		if(PathType == FSR_PATH_CURDRIVE_RELATIVE) {
			if(!FsrGetRootPath(FullPath))
				goto __error_exit;
		}
		// append relative path to curdir/curdrive
		if(!FsrAppendPath(&FullPath, Path->Buf, Path->Length))
			goto __no_mem;
		// replace source path to full path
		free(Path);
		Path = FullPath;
		FullPath = NULL;
	}

	// expand drive-relative path
	if(PathType == FSR_PATH_DRIVE_RELATIVE) {
		// allocate buffer for full path
		if((FullPath = WstrAlloc(MAX_PATH)) == NULL)
			goto __no_mem;
		// get full path name
		FullPathLen = GetFullPathName(Path->Buf, FullPath->BufLength, FullPath->Buf, NULL);
		if(FullPathLen > FullPath->BufLength) {
			if(!WstrExpand(&FullPath, FullPathLen))
				goto __no_mem;
			FullPathLen = GetFullPathName(Path->Buf, FullPath->BufLength, FullPath->Buf, NULL);
		}
		if((FullPathLen == 0) || (FullPathLen >= FullPath->BufLength)) {
			status = FSR_BADPATH;
			goto __error_exit;
		}
		FullPath->Length = FullPathLen;
		// make sure returned path have no forward slashes
		FsrReplaceSlashes(FullPath);
		// set full path buffer
		free(Path);
		Path = FullPath;
		FullPath = NULL;
	}

	// check path type again, it should be normal network path "\\server..." ...
	if((Path->Buf[0] == L'\\') && (Path->Buf[1] == L'\\') && (FsrIsValidPathChar(Path->Buf[2]))) {
		for(i = 3; (Path->Buf[i] != 0) && (Path->Buf[i] != L'\\'); i++) {
			if(!FsrIsValidPathChar(Path->Buf[i])) {
				status = FSR_BADPATH;
				goto __error_exit;
			}
		}
		// append trailing backslash to server name
		if(Path->Buf[i] == 0) {
			if(!WstrAppend(&Path, L"\\", 1))
				goto __no_mem;
		}
		RootPartLen = i + 1;
		PathType = FSR_PATH_NETWORK;
	}
	// ... or absolute local path "X:\..."
	else if(FsrIsDriveLetter(Path->Buf[0]) && (Path->Buf[1] == L':') && (Path->Buf[2] == L'\\')) {
		Path->Buf[0] &= ~(L'a' - L'A');
		RootPartLen = 3;
		PathType = FSR_PATH_ABSOLUTE;
	}
	// invalid path
	else {
		status = FSR_BADPATH;
		goto __error_exit;
	}

	// copy object names
	Cursor = NonRoot = Path->Buf + RootPartLen;

	for(PartName = NonRoot; PartName != NULL; PartName = NextPartName) {

		// cut at path separator
		if((NextPartName = wcschr(PartName, L'\\')) != NULL)
			*(NextPartName++) = 0;

		PartNameLen = wcslen(PartName);

		// remove trailing dots and spaces from last path part
		if(NextPartName == NULL) {
			while((PartNameLen > 0) && ((PartName[PartNameLen-1] == L' ') || (PartName[PartNameLen-1] == L'.')))
				PartNameLen--;
			PartName[PartNameLen] = 0;
		}

		// validate part name
		if(PartNameLen > 256) {
			status = FSR_BADPATH;
			goto __error_exit;
		}
		
		for(i = 0; i < PartNameLen; ++i) {
			if(!FsrIsValidPathChar(PartName[i])) {
				status = FSR_BADPATH;
				goto __error_exit;
			}
		}

		// skip single dots and empties
		if((PartName[0] == 0) || ((PartName[0] == L'.') && (PartName[1] == 0)))
			continue;

		// one step back on double dot
		if((PartName[0] == L'.') && (PartName[1] == L'.') && (PartName[2] == 0)) {
			if(Cursor > NonRoot) {
				Cursor--;
				while((Cursor > NonRoot) && (*Cursor != L'\\'))
					Cursor--;
			}
			continue;
		}

		// add backslash from previous pass
		if(Cursor > NonRoot)
			Cursor++;

		// copy part name
		if(PartName != Cursor) {
			wcscpy(Cursor, PartName);
		}
		
		Cursor += PartNameLen;

		// put backslash
		*Cursor = L'\\';
	}

	// remove backslash and terminate buffer
	*Cursor = 0;
	Path->Length = Cursor - Path->Buf;

	// append long prefix
	if(AddLongPrefix) {
		if(Path->Length > 256) {
			if(PathType == FSR_PATH_ABSOLUTE) {
				if(!WstrInsert(&Path, 0, L"\\\\?\\", 4))
					goto __no_mem;
				RootPartLen += 4;
			}
			if(PathType == FSR_PATH_NETWORK) {
				if(!WstrInsert(&Path, 2, L"?\\UNC\\", 6))
					goto __no_mem;
				RootPartLen += 6;
			}
		}
	}

	// success
	if(pNonRoot != NULL) {
		*pNonRoot = NonRoot;
	}

	*pNormalizedPath = Path;
	return FSR_OK;

__no_mem:
	status = FSR_NOMEM;
__error_exit:
	free(FullPath);
	free(Path);
	return status;
}

// -------------------------------------------------------------------------------------------------

static int FsrNodeCompare(const WCHAR *KeyName, const FSR_NODE **Node)
{
	return wcsicmp(KeyName, (*Node)->Name);
}

// -------------------------------------------------------------------------------------------------

static FSR_STATUS FsrNodeAdd(FSR_NODE **pNode, FSR_NODE *ParentNode, WCHAR *NodeName)
{
	DWORD NameSize, NodeSize, NewSubnodeMaxCount, InsertPos;
	FSR_NODE *Node, **NewSubnodes;
	char *cursor;
	int CompareResult;

	// expand parent's subnode array
	if(ParentNode->SubnodeCount == ParentNode->SubnodeMaxCount) {
		NewSubnodeMaxCount = (ParentNode->SubnodeMaxCount + 2) * 3 / 2;
		if((NewSubnodes = realloc(ParentNode->Subnode, NewSubnodeMaxCount * sizeof(FSR_NODE*))) == NULL)
			return FSR_NOMEM;
		ParentNode->SubnodeMaxCount = NewSubnodeMaxCount;
		ParentNode->Subnode = NewSubnodes;
	}

	// calculate node buffer size
	NameSize = (wcslen(NodeName) + 1) * sizeof(WCHAR);
	NodeSize = sizeof(FSR_NODE) + NameSize;

	// allocate node buffer
	if((Node = malloc(NodeSize)) == NULL)
		return FSR_NOMEM;

	cursor = (char*)Node + sizeof(FSR_NODE);

	// initialize new node
	memset(Node, 0, sizeof(FSR_NODE));
	Node->ParentNode = ParentNode;
	Node->Type = FSR_NODE_INSIGNIFICANT;

	Node->Name = (void*)cursor;
	cursor += NameSize;
	memcpy(Node->Name, NodeName, NameSize);

	// find position to insert node to parent's subnode buffer
	for(InsertPos = 0; InsertPos < ParentNode->SubnodeCount; InsertPos++) {
		CompareResult = FsrNodeCompare(NodeName, ParentNode->Subnode + InsertPos);
		if(CompareResult == 0) {
			free(Node);
			return FSR_EXISTS;
		}
		if(CompareResult < 0) {
			break;
		}
	}

	// insert node
	memmove(ParentNode->Subnode + InsertPos + 1, ParentNode->Subnode + InsertPos,
		(ParentNode->SubnodeCount - InsertPos) * sizeof(FSR_NODE*));
	ParentNode->SubnodeCount++;

	ParentNode->Subnode[InsertPos] = Node;

	// return node
	*pNode = Node;
	return FSR_OK;
}

// -------------------------------------------------------------------------------------------------

static FSR_STATUS FsrNodeCreate(FSR_NODE **pNode, WCHAR *NodePath)
{
	FSR_STATUS status;
	R2I_WSTR *PathBuffer;
	FSR_NODE *ParentNode, **pCurrentNode, *CurrentNode = NULL;
	WCHAR *NonRoot;
	WCHAR *NodeName, *NextNodeName, BackupChar;

	// make absolute path
	if((status = FsrNormalizePath(&PathBuffer, &NonRoot, NodePath, WSTR_LENGTH_UNKNOWN, 0)) != FSR_OK)
		return status;

	// lookup/create nodes
	for(NodeName = PathBuffer->Buf; (NodeName != NULL) && (*NodeName != 0); NodeName = NextNodeName) {

		// cut at next name
		if(NodeName == PathBuffer->Buf) {
			NextNodeName = NonRoot;
			BackupChar = *NextNodeName;
			*NextNodeName = 0;
			ParentNode = FsrData.RootNode;
		} else {
			if((NextNodeName = wcschr(NodeName, L'\\')) != NULL)
				*(NextNodeName++) = 0;
			ParentNode = CurrentNode;
		}

		// lookup/create new node
		pCurrentNode = bsearch(NodeName, ParentNode->Subnode,
			ParentNode->SubnodeCount, sizeof(FSR_NODE*), FsrNodeCompare);
		if(pCurrentNode != NULL) {
			CurrentNode = *pCurrentNode;
		} else {
			if((status = FsrNodeAdd(&CurrentNode, ParentNode, NodeName)) != FSR_OK) {
				free(PathBuffer);
				return status;
			}
			if(NodeName == PathBuffer->Buf) {
				CurrentNode->IsRootNode = 1;
			}
		}

		// restore cutted char
		if(NodeName == PathBuffer->Buf) {
			*NextNodeName = BackupChar;
		}

	}

	free(PathBuffer);

	*pNode = CurrentNode;
	return FSR_OK;
}

// -------------------------------------------------------------------------------------------------

static void FsrNodeFree(FSR_NODE *Node)
{
	DWORD i;

	for(i = 0; i < Node->SubnodeCount; ++i) {
		FsrNodeFree(Node->Subnode[i]);
	}
	free(Node->TargetPath);
	free(Node->Subnode);
	free(Node);
}

// -------------------------------------------------------------------------------------------------

FSR_STATUS FsrRedirectAdd(WCHAR *SourcePath, WCHAR *TargetSubdir, int IsException)
{
	FSR_STATUS status;
	FSR_NODE *Node;
	R2I_WSTR *TargetPath = NULL;

	// make target full path
	if(TargetSubdir != NULL) {
		TargetPath = WstrDup(FsrData.DataDir->Buf, FsrData.DataDir->Length, 
			FsrData.DataDir->Length + wcslen(TargetSubdir) + 1);
		if(TargetPath == NULL)
			return FSR_NOMEM;
		if(!FsrAppendPath(&TargetPath, TargetSubdir, WSTR_LENGTH_UNKNOWN)) {
			free(TargetPath);
			return FSR_NOMEM;
		}
	}

	// create or new node
	if((status = FsrNodeCreate(&Node, SourcePath)) != FSR_OK) {
		free(TargetPath);
		return status;
	}

	// free old node data
	free(Node->TargetPath);

	// write type and target directory to node
	Node->Type = (!IsException) ? FSR_NODE_REDIRECT : FSR_NODE_NO_REDIRECT;
	Node->TargetPath = TargetPath;

	return FSR_OK;
}

// -------------------------------------------------------------------------------------------------

static FSR_STATUS FsrRedirectPath(R2I_WSTR **pTargetPath, WCHAR *SourcePath, 
								  DWORD SourcePathLength, int AddLongPrefix)
{
	FSR_STATUS status;
	FSR_PATH_TYPE PathType;
	FSR_NODE **pCurrentNode, *CurrentNode, *RedirectNode = NULL;
	R2I_WSTR *Path;
	WCHAR *PartName, *NextPartName, *SubpathName = NULL;
	WCHAR *NonRoot, *CutPos, BackupChar;
	DWORD SubpathNameOffset, SubpathNameLen;

	// normalize path
	if((status = FsrNormalizePath(&Path, &NonRoot, SourcePath, SourcePathLength, 0)) != FSR_OK) {
		if(status == FSR_DEVICEPATH) {
			*pTargetPath = NULL;
			return FSR_OK;
		}
		return status;
	}

	// lookup redirect node
	for(PartName = Path->Buf; (PartName != NULL) && (*PartName != 0); PartName = NextPartName) {

		// find next dir name or filename
		if(PartName == Path->Buf) {
			CutPos = NextPartName = NonRoot;
			CurrentNode = FsrData.RootNode;
		} else {
			CutPos = NULL;
			if((NextPartName = wcschr(PartName, L'\\')) != NULL)
				CutPos = NextPartName++;
		}

		// cut object name
		if(CutPos != NULL) {
			BackupChar = *CutPos;
			*CutPos = 0;
		}

		// lookup node
		pCurrentNode = bsearch(PartName, CurrentNode->Subnode,
			CurrentNode->SubnodeCount, sizeof(FSR_DATA*), FsrNodeCompare);
		CurrentNode = (pCurrentNode != NULL) ? (*pCurrentNode) : NULL;

		// restore next object name
		if(CutPos != NULL) {
			*CutPos = BackupChar;
		}

		// node not found
		if(CurrentNode == NULL)
			break;

		// redirect exclusion node
		if(CurrentNode->Type == FSR_NODE_NO_REDIRECT) {
			RedirectNode = NULL;
			break;
		}

		// redirect node (use "longest" redirect node)
		if(CurrentNode->Type == FSR_NODE_REDIRECT) {
			RedirectNode = CurrentNode;
			SubpathName = NextPartName;
		}
	}

	// no redirect node found
	if(RedirectNode == NULL) {
		free(Path);
		*pTargetPath = NULL;
		return FSR_OK;
	}

	// change redirected part in path
	if(SubpathName != NULL) {
		SubpathNameOffset = SubpathName - Path->Buf;
		SubpathNameLen = wcslen(SubpathName);
		// expand path buffer if needed
		if(!WstrExpand(&Path, RedirectNode->TargetPath->Length + SubpathNameLen + 1)) {
			free(Path);
			return FSR_NOMEM;
		}
		SubpathName = Path->Buf + SubpathNameOffset;
		// move subpath name to change base directory
		if(RedirectNode->TargetPath->Buf[RedirectNode->TargetPath->Length - 1] == L'\\') {
			memmove(Path->Buf + RedirectNode->TargetPath->Length, 
				SubpathName, (wcslen(SubpathName) + 1) * sizeof(WCHAR));
			Path->Length = RedirectNode->TargetPath->Length + SubpathNameLen;
		} 
		// move subpath name to change base directory and add backslash
		else {
			memmove(Path->Buf + RedirectNode->TargetPath->Length + 1, 
				SubpathName, (wcslen(SubpathName) + 1) * sizeof(WCHAR));
			Path->Buf[RedirectNode->TargetPath->Length] = L'\\';
			Path->Length = RedirectNode->TargetPath->Length + SubpathNameLen + 1;
		}
		// set redirected directory
		memcpy(Path->Buf, RedirectNode->TargetPath->Buf, 
			RedirectNode->TargetPath->Length * sizeof(WCHAR));
	} 
	// set path to redirect subdir
	else {
		if(!WstrSet(&Path, RedirectNode->TargetPath->Buf, RedirectNode->TargetPath->Length)) {
			free(Path);
			return FSR_NOMEM;
		}
	}

	// append long path prefix
	if( (AddLongPrefix) && (Path->Length > 256) ) {
		PathType = FsrGetPathType(Path->Buf, &NonRoot);
		if(PathType == FSR_PATH_ABSOLUTE) {
			if(!WstrInsert(&Path, 0, L"\\\\?\\", 4)) {
				free(Path);
				return FSR_NOMEM;
			}
		}
		if(PathType == FSR_PATH_NETWORK) {
			if(!WstrInsert(&Path, 2, L"?\\UNC\\", 6)) {
				free(Path);
				return FSR_NOMEM;
			}
		}
	}

	*pTargetPath = Path;
	return FSR_OK;
}

// -------------------------------------------------------------------------------------------------

static void FsrRedirectPrint(FSR_NODE *Node, DWORD offset)
{
#ifdef _DEBUG
	DWORD i;

	for(i = 0; i < offset; ++i)
		printf("  ");

	switch(Node->Type) {
		case FSR_NODE_INSIGNIFICANT:
			printf("FSR_NODE_INSIGNIFICANT");
			break;
		case FSR_NODE_REDIRECT:
			printf("FSR_NODE_REDIRECT");
			break;
		case FSR_NODE_NO_REDIRECT:
			printf("FSR_NODE_NO_REDIRECT");
			break;
	}

	printf(" %S (%S) ", Node->Name, Node->TargetPath ? Node->TargetPath->Buf : L"NULL");

	puts("");

	for(i = 0; i < Node->SubnodeCount; ++i)
		FsrRedirectPrint(Node->Subnode[i], offset + 1);
#endif
}

// -------------------------------------------------------------------------------------------------

static DWORD FsrCreateDirs(WCHAR *Path)
{
	DWORD Attributes, Error;
	WCHAR *CutPos;
	TypeGetFileAttributesW GetAttr;
	TypeCreateDirectoryW MkDir;

	GetAttr = HookGetOEP(HOOK_PROC_GETFILEATTRIBUTESW);
	MkDir = HookGetOEP(HOOK_PROC_CREATEDIRECTORYW);

	// get attributes for path
	Attributes = GetAttr(Path);

	// error getting attributes
	if(Attributes == INVALID_FILE_ATTRIBUTES) {

		// error other than directory not exists, return
		Error = GetLastError();
		if( (Error != ERROR_PATH_NOT_FOUND) && (Error != ERROR_FILE_NOT_FOUND) )
			return Error;

		// path not found, create it
		if(Error == ERROR_PATH_NOT_FOUND) {

			if((CutPos = wcsrchr(Path, L'\\')) == NULL)
				return ERROR_INVALID_NAME;

			*CutPos = 0;
			Error = FsrCreateDirs(Path);
			*CutPos = L'\\';

			if(Error != ERROR_SUCCESS)
				return Error;

		}

		// create directory itself
		if(!MkDir(Path, NULL))
			return GetLastError();

		return ERROR_SUCCESS;
	}

	// directory found
	if(Attributes & FILE_ATTRIBUTE_DIRECTORY)
		return ERROR_SUCCESS;

	// file found instead of directory
	return ERROR_ALREADY_EXISTS;
}

// -------------------------------------------------------------------------------------------------

static DWORD FsrCreateParentDirs(WCHAR *Target)
{
	WCHAR *CutPos;
	DWORD Error;

	if((CutPos = wcsrchr(Target, L'\\')) == NULL)
		return ERROR_INVALID_NAME;

	*CutPos = 0;
	Error = FsrCreateDirs(Target);
	*CutPos = L'\\';

	return Error;
}

// -------------------------------------------------------------------------------------------------

static DWORD FsrOpenFile(HANDLE *pFileHandle, WCHAR *FileName)
{
	HANDLE FileHandle;
	DWORD Attributes;
	TypeCreateFileW CrtFile;
	TypeGetFileAttributesW GetAttr;

	CrtFile = HookGetOEP(HOOK_PROC_CREATEFILEW);
	GetAttr = HookGetOEP(HOOK_PROC_GETFILEATTRIBUTESW);

	if((Attributes = GetAttr(FileName)) == INVALID_FILE_ATTRIBUTES)
		return GetLastError();

	// open directory
	if(Attributes & FILE_ATTRIBUTE_DIRECTORY) {
		FileHandle = CrtFile(FileName, FILE_READ_ATTRIBUTES,
			FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
			NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	}
	// open file
	else {
		FileHandle = CrtFile(FileName, GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE,
			NULL, OPEN_EXISTING, 0, NULL);
	}

	if(FileHandle == NULL)
		return GetLastError();

	*pFileHandle = FileHandle;
	return ERROR_SUCCESS;
}

// -------------------------------------------------------------------------------------------------

static int FsrCopyFile(WCHAR *DestFileName, HANDLE SrcFileHandle)
{
	HANDLE DestFileHandle;
	DWORD BlockSize, BytesRead;
	BY_HANDLE_FILE_INFORMATION FileInfo;
	char *Buffer;
	TypeCreateFileW CrtFile;
	TypeSetFileAttributesW SetAttr;
	TypeCreateDirectoryW MkDir;

	CrtFile = HookGetOEP(HOOK_PROC_CREATEFILEW);
	SetAttr = HookGetOEP(HOOK_PROC_SETFILEATTRIBUTESW);
	MkDir = HookGetOEP(HOOK_PROC_CREATEDIRECTORYW);

	// get file information
	if(!GetFileInformationByHandle(SrcFileHandle, &FileInfo))
		return 0;

	// create and open directory
	if(FileInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
		MkDir(DestFileName, NULL);
		DestFileHandle = CrtFile(DestFileName, FILE_WRITE_ATTRIBUTES,
			FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
			NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	}
	// create file
	else {
		DestFileHandle = CrtFile(DestFileName, GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE,
			NULL, CREATE_ALWAYS, 0, NULL);
	}
	if(DestFileHandle == INVALID_HANDLE_VALUE)
		return 0;

	// copy file contents (ignore ntfs streams)
	if( ! (FileInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ) {

		// choose block size to copy regarding of file size
		if( (FileInfo.nFileSizeHigh != 0) || (FileInfo.nFileSizeLow > 10*1024*1024) ) {
			BlockSize = 64 * 1024;
		} else if(FileInfo.nFileSizeLow > 64 * 1024) {
			BlockSize = 16 * 1024;
		} else {
			BlockSize = 4 * 1024;
		}

		// allocate block buffer
		if((Buffer = malloc(BlockSize)) == NULL) {
			CloseHandle(SrcFileHandle);
			return 0;
		}

		// copy data
		while(1) {
			ReadFile(SrcFileHandle, Buffer, BlockSize, &BytesRead, NULL);
			if(BytesRead == 0)
				break;
			WriteFile(DestFileHandle, Buffer, BytesRead, &BytesRead, NULL);
		}

		free(Buffer);

	}

	// copy attributes
	SetFileTime(DestFileHandle, &(FileInfo.ftCreationTime), &(FileInfo.ftLastAccessTime), &(FileInfo.ftLastWriteTime));
	SetAttr(DestFileName, FileInfo.dwFileAttributes);

	CloseHandle(DestFileHandle);
	return 1;
}

// -------------------------------------------------------------------------------------------------

HANDLE __stdcall HookCreateFileW(WCHAR *source_name, DWORD access, DWORD share_mode, void *security,
								 DWORD source_disposition, DWORD create_attributes, void *hftemplate)
{
	FSR_STATUS status;
	R2I_WSTR *TargetName;
	HANDLE hfsource = INVALID_HANDLE_VALUE;
	HANDLE hftarget = INVALID_HANDLE_VALUE;
	DWORD error, attributes;
	DWORD target_disposition;
	int create_target_path;
	int open_target_file;
	int check_no_source_file;
	int check_source_file;
	int copy_source_file;
	int write_access;
	DWORD wmask, reserror;
	TypeCreateFileW CrtFile;
	TypeGetFileAttributesW GetAttr;

	CrtFile = HookGetOEP(HOOK_PROC_CREATEFILEW);
	GetAttr = HookGetOEP(HOOK_PROC_GETFILEATTRIBUTESW);

	if(!FsrData.HooksEnabled) {
		hfsource = CrtFile(source_name, access, share_mode, security, source_disposition, create_attributes, hftemplate);
		return hfsource;
	}

	// get redirected file name
	if((status = FsrRedirectPath(&TargetName, source_name, WSTR_LENGTH_UNKNOWN, 1)) != FSR_OK) {
		SetLastError(FsrStatusToWinError(status));
		return INVALID_HANDLE_VALUE;
	}

	// no redirection - use original filename
	if(TargetName == NULL) {
		hfsource = CrtFile(source_name, access, share_mode, security, source_disposition, create_attributes, hftemplate);
		return hfsource;
	}

	// is traget file and path exists?
	create_target_path = 1;
	open_target_file = 1;

	attributes = GetAttr(TargetName->Buf);
	if(attributes == INVALID_FILE_ATTRIBUTES) {
		error = GetLastError();
		if(error == ERROR_FILE_NOT_FOUND) {
			create_target_path = 0;
			open_target_file = 0;
		} else if(error == ERROR_PATH_NOT_FOUND) {
			open_target_file = 0;
		}
	}

	// use target file unless it not exists
	if(open_target_file) {
		hftarget = CrtFile(TargetName->Buf, access, share_mode, security, source_disposition, create_attributes, hftemplate);
		FsrFreeKeepError(TargetName);
		return hftarget;
	}

	// use original file if target file not exists and no write access required
	wmask = MAXIMUM_ALLOWED | GENERIC_WRITE |
		FILE_WRITE_DATA | FILE_APPEND_DATA |
		FILE_WRITE_EA | /*FILE_WRITE_ATTRIBUTES |*/
		FILE_DELETE_CHILD;

	write_access = ( access & wmask );

	if( (!write_access) && (source_disposition == OPEN_EXISTING) ) {
		free(TargetName);
		hfsource = CrtFile(source_name, access, share_mode, security, source_disposition, create_attributes, hftemplate);
		return hfsource;
	}

	// no target file, write access, check disposition
	check_no_source_file = 0;
	check_source_file = 0;
	copy_source_file = 0;
	target_disposition = source_disposition;

	switch(source_disposition) {
		case CREATE_ALWAYS:
			break;
		case CREATE_NEW:
			check_no_source_file = 1;
			break;
		case OPEN_ALWAYS:
			copy_source_file = 1;
			break;
		case OPEN_EXISTING:
			check_source_file = 1;
			copy_source_file = 1;
			break;
		case TRUNCATE_EXISTING:
			check_source_file = 1;
			target_disposition = CREATE_NEW;
			break;
	}

	// try open original file
	if( check_no_source_file || check_source_file || copy_source_file ) {
		error = FsrOpenFile(&hfsource, source_name);
		if( (error != ERROR_SUCCESS) && (error != ERROR_FILE_NOT_FOUND) && (error != ERROR_PATH_NOT_FOUND) ) {
			reserror = error;
			goto __error_exit;
		}
	}

	// original file should not exists (CREATE_NEW)
	if( check_no_source_file && (hfsource != INVALID_HANDLE_VALUE) ) {
		reserror = ERROR_FILE_EXISTS;
		goto __error_exit;
	}

	// original file should exists (OPEN_EXISTING, TRUNCATE_EXISTING)
	if( check_source_file && (hfsource == INVALID_HANDLE_VALUE) ) {
		reserror = error; //ERROR_FILE_NOT_FOUND;
		goto __error_exit;
	}

	// create path for target file
	if(create_target_path) {
		if((error = FsrCreateParentDirs(TargetName->Buf)) != ERROR_SUCCESS) {
			reserror = error;
			goto __error_exit;
		}
	}

	// copy original file content (OPEN_ALWAYS, OPEN_EXISTING)
	if( copy_source_file && (hfsource != INVALID_HANDLE_VALUE) ) {
		FsrCopyFile(TargetName->Buf, hfsource);
	}

	// create target file
	hftarget = CrtFile(TargetName->Buf, access, share_mode, security, target_disposition, create_attributes, hftemplate);
	if(hftarget == INVALID_HANDLE_VALUE) {
		reserror = GetLastError();
		goto __error_exit;
	}

	// close source file
	if(hfsource != INVALID_HANDLE_VALUE)
		CloseHandle(hfsource);

	free(TargetName);
	return hftarget;

__error_exit:

	if(hfsource != INVALID_HANDLE_VALUE)
		CloseHandle(hfsource);

	if(hftarget != INVALID_HANDLE_VALUE)
		CloseHandle(hftarget);

	free(TargetName);

	SetLastError(reserror);
	return INVALID_HANDLE_VALUE;
}

// -------------------------------------------------------------------------------------------------

DWORD __stdcall HookGetFileAttributesW(WCHAR *source_name)
{
	FSR_STATUS status;
	DWORD attributes, error;
	TypeGetFileAttributesW GetAttr;
	R2I_WSTR *TargetName;

	GetAttr = HookGetOEP(HOOK_PROC_GETFILEATTRIBUTESW);

	if(!FsrData.HooksEnabled) {
		return GetAttr(source_name);
	}

	// get redirected filename
	if((status = FsrRedirectPath(&TargetName, source_name, WSTR_LENGTH_UNKNOWN, 1)) != FSR_OK) {
		SetLastError(FsrStatusToWinError(status));
		return INVALID_FILE_ATTRIBUTES;
	}

	// no redirection - use original filename
	if(TargetName == NULL)
		return GetAttr(source_name);

	// get target file attributes
	attributes = GetAttr(TargetName->Buf);
	free(TargetName);

	// target file not found - use original file
	if(attributes == INVALID_FILE_ATTRIBUTES) {
		error = GetLastError();
		if( (error == ERROR_FILE_NOT_FOUND) || (error == ERROR_PATH_NOT_FOUND) )
			return GetAttr(source_name);
	}

	return attributes;
}

// -------------------------------------------------------------------------------------------------

BOOL __stdcall HookGetFileAttributesExW(WCHAR *source_name, int level, void *buf)
{
	FSR_STATUS status;
	BOOL ok;
	DWORD error;
	R2I_WSTR *TargetName;
	TypeGetFileAttributesExW GetAttrEx;

	GetAttrEx = HookGetOEP(HOOK_PROC_GETFILEATTRIBUTESEXW);

	if(!FsrData.HooksEnabled) {
		return GetAttrEx(source_name, level, buf);
	}

	// get redirected filename
	if((status = FsrRedirectPath(&TargetName, source_name, WSTR_LENGTH_UNKNOWN, 1)) != FSR_OK)
		return FsrStatusToWinError(status);

	// no redirection - use original filename
	if(TargetName == NULL)
		return GetAttrEx(source_name, level, buf);

	// get attributes for redirected filename
	ok = GetAttrEx(TargetName->Buf, level, buf);
	free(TargetName);

	// redirected file not exists - use original filename
	if(!ok) {
		error = GetLastError();
		if( (error == ERROR_FILE_NOT_FOUND) || (error == ERROR_PATH_NOT_FOUND) )
			return GetAttrEx(source_name, level, buf);
	}

	return ok;
}

// -------------------------------------------------------------------------------------------------

BOOL __stdcall HookSetFileAttributesW(WCHAR *source_name, DWORD attributes)
{
	FSR_STATUS status;
	DWORD error, openerror, crterror;
	R2I_WSTR *TargetName = NULL;
	HANDLE hfsource = NULL;
	TypeSetFileAttributesW SetAttr;

	SetAttr = HookGetOEP(HOOK_PROC_SETFILEATTRIBUTESW);

	if(!FsrData.HooksEnabled) {
		return SetAttr(source_name, attributes);
	}

	// get redirected filename
	if((status = FsrRedirectPath(&TargetName, source_name, WSTR_LENGTH_UNKNOWN, 1)) != FSR_OK)
		return FsrStatusToWinError(status);

	// redirected filename is not exists, use original file
	if(TargetName == NULL)
		return SetAttr(source_name, attributes);

	// set attributes for redirected file
	if(SetAttr(TargetName->Buf, attributes)) {
		free(TargetName);
		return TRUE;
	}

	// redirected file not found,
	error = GetLastError();
	if( (error == ERROR_FILE_NOT_FOUND) || (error == ERROR_PATH_NOT_FOUND) ) {

		// open original file
		openerror = FsrOpenFile(&hfsource, source_name);
		if(openerror != ERROR_SUCCESS) {
			error = openerror;
			goto __error_exit;
		}

		// create directories for redirected file
		if(error == ERROR_PATH_NOT_FOUND) {
			if((crterror = FsrCreateParentDirs(TargetName->Buf)) != ERROR_SUCCESS) {
				error = crterror;
				goto __error_exit;
			}
		}

		// copy original file to redirected
		FsrCopyFile(TargetName->Buf, hfsource);

		// set attributes to redirected file again
		if(!SetAttr(TargetName->Buf, attributes)) {
			error = GetLastError();
			goto __error_exit;
		}

		CloseHandle(hfsource);
		free(TargetName);
		return TRUE;

	}

__error_exit:
	if(hfsource != NULL)
		CloseHandle(hfsource);
	free(TargetName);
	SetLastError(error);
	return FALSE;
}

// -------------------------------------------------------------------------------------------------

BOOL __stdcall HookCreateDirectoryW(WCHAR *source_name, void *security)
{
	DWORD error;
	FSR_STATUS status;
	R2I_WSTR *TargetName;
	TypeCreateDirectoryW MkDir;

	MkDir = HookGetOEP(HOOK_PROC_CREATEDIRECTORYW);

	if(!FsrData.HooksEnabled) {
		return MkDir(source_name, NULL);
	}

	// get redirected directory name
	if((status = FsrRedirectPath(&TargetName, source_name, WSTR_LENGTH_UNKNOWN, 1)) != FSR_OK)
		return FsrStatusToWinError(status);

	// no redirection - use original name
	if(TargetName == NULL)
		return MkDir(source_name, security);

	// create parent directories
	if((error = FsrCreateParentDirs(TargetName->Buf)) != ERROR_SUCCESS)
		goto __error_exit;

	// create directory itself
	if(!MkDir(TargetName->Buf, security)) {
		error = GetLastError();
		goto __error_exit;
	}

	free(TargetName);
	return TRUE;

__error_exit:
	free(TargetName);
	SetLastError(error);
	return FALSE;
}

// -------------------------------------------------------------------------------------------------

BOOL __stdcall HookRemoveDirectoryW(WCHAR *source_name)
{
	FSR_STATUS status;
	R2I_WSTR *TargetName;
	TypeRemoveDirectoryW RmDir;

	RmDir = HookGetOEP(HOOK_PROC_REMOVEDIRECTORYW);

	if(!FsrData.HooksEnabled) {
		return RmDir(source_name);
	}

	// get redirected directory name
	if((status = FsrRedirectPath(&TargetName, source_name, WSTR_LENGTH_UNKNOWN, 1)) != FSR_OK)
		return status;

	// no redirection - use original directory name
	if(TargetName == NULL)
		return RmDir(source_name);

	// remove redirected directory
	if(!RmDir(TargetName->Buf)) {
		FsrFreeKeepError(TargetName);
		return FALSE;
	}

	free(TargetName);
	return TRUE;
}

// -------------------------------------------------------------------------------------------------

BOOL __stdcall HookDeleteFileW(WCHAR *source_name)
{
	FSR_STATUS status;
	R2I_WSTR *TargetName;
	TypeDeleteFileW DelFile;

	DelFile = HookGetOEP(HOOK_PROC_DELETEFILEW);

	if(!FsrData.HooksEnabled) {
		return DelFile(source_name);
	}

	// get redirected file name
	if((status = FsrRedirectPath(&TargetName, source_name, WSTR_LENGTH_UNKNOWN, 1)) != FSR_OK)
		return status;

	// no redirection - use original filename
	if(TargetName == NULL)
		return DelFile(source_name);

	// delete redirected file
	if(!DelFile(TargetName->Buf)) {
		FsrFreeKeepError(TargetName->Buf);
		return FALSE;
	}

	free(TargetName);
	return TRUE;
}

// -------------------------------------------------------------------------------------------------

static DWORD FsrCheckFile(R2I_WSTR **pFileName, WCHAR *Extension)
{
	R2I_WSTR *FileName = *pFileName;
	WCHAR *FullNameBuf = NULL, *FileNamePart, *Temp;
	int HaveExtension;
	DWORD Error, Attributes, LenNoExt;
	TypeGetFileAttributesW GetAttr;
	DWORD ExtensionLen;

	GetAttr = HookGetOEP(HOOK_PROC_GETFILEATTRIBUTESW);

	// check filename have extension
	FileNamePart = FileName->Buf;
	if((Temp = wcsrchr(FileName->Buf, L'\\')) != NULL)
		FileNamePart = Temp + 1;
	HaveExtension = (wcschr(FileNamePart, L'.') != NULL);

	// check attributes
	Attributes = GetAttr(FileName->Buf);

	// can't get attributes or found directory
	if( (Attributes == INVALID_FILE_ATTRIBUTES) || (Attributes & FILE_ATTRIBUTE_DIRECTORY) ) {

		// if other error than file not found, return error
		if(Attributes == INVALID_FILE_ATTRIBUTES) {
			Error = GetLastError();
			if(Error != ERROR_FILE_NOT_FOUND)
				return Error;
		}

		ExtensionLen = (Extension != NULL) ? wcslen(Extension) : 0;

		// if filename have extension, append not required
		if( HaveExtension || (ExtensionLen == 0) )
			return ERROR_FILE_NOT_FOUND;

		// append extension
		LenNoExt = FileName->Length;
		if(!WstrAppend(pFileName, Extension, ExtensionLen))
			return ERROR_NOT_ENOUGH_MEMORY;
		FileName = *pFileName;

		// get attributes again
		Attributes = GetAttr(FileName->Buf);

		// if can't get attributes again, return error
		if(Attributes == INVALID_FILE_ATTRIBUTES) {
			WstrDelete(FileName, LenNoExt, WSTR_END);
			return GetLastError();
		}

		// looking up for files, not directories
		if(Attributes & FILE_ATTRIBUTE_DIRECTORY) {
			WstrDelete(FileName, LenNoExt, WSTR_END);
			return GetLastError();
		}

	}

	return ERROR_SUCCESS;
}

// -------------------------------------------------------------------------------------------------

static DWORD FsrSearchFile(WCHAR *BasePath, WCHAR *FileName, WCHAR *Extension, R2I_WSTR **pResultFilename)
{
	FSR_STATUS Status;
	R2I_WSTR *SourceFullName = NULL;
	R2I_WSTR *TargetFullName = NULL;
	DWORD Error;
	DWORD FileNameLength;

	FileNameLength = wcslen(FileName);

	// concat basepath and filename
	if((BasePath != NULL) && (*BasePath != 0)) {
		if((SourceFullName = WstrDup(BasePath, WSTR_LENGTH_UNKNOWN, MAX_PATH)) == NULL) {
			Error = ERROR_NOT_ENOUGH_MEMORY;
			goto __exit;
		}
		if(!FsrAppendPath(&SourceFullName, FileName, FileNameLength)) {
			Error = ERROR_NOT_ENOUGH_MEMORY;
			goto __exit;
		}
	} else {
		if((SourceFullName = WstrDup(FileName, WSTR_LENGTH_UNKNOWN, MAX_PATH)) == NULL) {
			Error = ERROR_NOT_ENOUGH_MEMORY;
			goto __exit;
		}
	}

	// redirect path
	if((Status = FsrRedirectPath(&TargetFullName, SourceFullName->Buf, SourceFullName->Length, 1)) != FSR_OK) {
		Error = FsrStatusToWinError(Status);
		goto __exit;
	}

	// check for redirected file
	if(TargetFullName != NULL) {
		Error = FsrCheckFile(&TargetFullName, Extension);
		if(Error == ERROR_SUCCESS) {
			*pResultFilename = TargetFullName;
			free(SourceFullName);
			return ERROR_SUCCESS;
		}
		if((Error != ERROR_FILE_NOT_FOUND) && (Error != ERROR_PATH_NOT_FOUND) && (Error != ERROR_ACCESS_DENIED))
			goto __exit;
	}

	// check for original file
	Error = FsrCheckFile(&SourceFullName, Extension);
	if(Error == ERROR_SUCCESS) {
		Status = FsrNormalizePath(pResultFilename, NULL, SourceFullName->Buf, SourceFullName->Length, 1);
		Error = FsrStatusToWinError(Status);
	}

__exit:
	free(TargetFullName);
	free(SourceFullName);
	return Error;
}

// -------------------------------------------------------------------------------------------------

static DWORD FsrSearchPath(WCHAR *BasePath, WCHAR *FileName, WCHAR *Extension, R2I_WSTR **pResultFilename)
{
	R2I_WSTR *FileNameCopy;
	FSR_PATH_TYPE FilenamePathType;
	FSR_SEARCH_PATH_LIST *SearchEntry;
	WCHAR *SourceFullName = NULL;
	WCHAR *NonRootPart;
	DWORD Error;

	// get filename path type
	FilenamePathType = FsrGetPathType(FileName, &NonRootPart);

	if(FilenamePathType == FSR_PATH_UNKNOWN) {
		return ERROR_FILE_NOT_FOUND;
	}

	if(FilenamePathType == FSR_PATH_DEVICE) {
		if((FileNameCopy = WstrDup(FileName, WSTR_LENGTH_UNKNOWN, 0)) == NULL)
			return ERROR_NOT_ENOUGH_MEMORY;
		*pResultFilename = FileNameCopy;
		return ERROR_SUCCESS;
	}

	// search file in different places
	if( (FilenamePathType == FSR_PATH_CURDIR_RELATIVE) && (BasePath == NULL) ) {
		// check program start directory
		Error = FsrSearchFile(FsrData.ProgramStartPath->Buf, FileName, Extension, pResultFilename);
		if( (Error == ERROR_FILE_NOT_FOUND) || (Error == ERROR_PATH_NOT_FOUND) || (Error == ERROR_ACCESS_DENIED) ) {
			// check current directory
			Error = FsrSearchFile(L".", FileName, Extension, pResultFilename);
			if( (Error == ERROR_FILE_NOT_FOUND) || (Error == ERROR_PATH_NOT_FOUND) || (Error == ERROR_ACCESS_DENIED) ) {
				// check search directories
				for(SearchEntry = FsrData.SrchPthItems; SearchEntry != NULL; SearchEntry = SearchEntry->next) {
					Error = FsrSearchFile(SearchEntry->source_name, FileName, Extension, pResultFilename);
					if((Error != ERROR_FILE_NOT_FOUND) && (Error != ERROR_PATH_NOT_FOUND) && (Error != ERROR_ACCESS_DENIED))
						break;
				}
			}
		}
		if((Error == ERROR_PATH_NOT_FOUND) || (Error == ERROR_ACCESS_DENIED))
			Error = ERROR_FILE_NOT_FOUND;
	}
	// search file with given BasePath
	else {
		if((FilenamePathType != FSR_PATH_CURDIR_RELATIVE) || (BasePath == NULL)) {
			BasePath = L"";
		}
		Error = FsrSearchFile(BasePath, FileName, Extension, pResultFilename);
	}

	return Error;
}

// -------------------------------------------------------------------------------------------------

DWORD __stdcall HookSearchPathW(WCHAR *path, WCHAR *filename, WCHAR *extension,
								DWORD buflen, WCHAR *buf, WCHAR **pfnpart)
{
	DWORD error;
	WCHAR *temp;
	R2I_WSTR *Result;
	TypeSearchPathW SearchPth;

	SearchPth = HookGetOEP(HOOK_PROC_SEARCHPATHW);

	if(FsrData.HooksEnabled) {

		// search path
		error = FsrSearchPath(path, filename, extension, &Result);

		// success, fill buffer
		if(error == ERROR_SUCCESS) {

			if(buflen < Result->Length + 1) {
				free(Result);
				return Result->Length + 1;
			}

			wcscpy(buf, Result->Buf);

			if(pfnpart != NULL) {
				*pfnpart = buf;
				if((temp = wcsrchr(buf, L'\\')) != NULL)
					*pfnpart = temp + 1;
			}

			free(Result);
			return Result->Length;

		}

		// return error
		SetLastError(error);
		return 0;
	}

	return SearchPth(path, filename, extension, buflen, buf, pfnpart);
}

// -------------------------------------------------------------------------------------------------

static int FsrAddSearchResult(FSR_SEARCH *SearchData, WIN32_FIND_DATA *FileInfo)
{
	DWORD InsertIndex;
	WCHAR *FileName1, *FileName2;
	int DotsType1, DotsType2;
	void *Temp;
	int res;
	FSR_SEARCH_ENTRY *SearchEntry;
	DWORD FilenameSize, ShortNameSize, EntrySize;
	char *Cursor;

	FileName1 = FileInfo->cFileName;

	DotsType1 = 0x7fffffff;
	if(FileName1[0] == L'.') {
		if(FileName1[1] == 0)
			DotsType1 = 1;
		else if( (FileName1[1] == L'.') && (FileName1[2] == 0) )
			DotsType1 = 2;
	}

	for(InsertIndex = 0; InsertIndex < SearchData->entrycnt; InsertIndex++) {

		FileName2 = SearchData->entry[InsertIndex]->name;

		DotsType2 = 0x7fffffff;
		if(FileName2[0] == L'.') {
			if(FileName2[1] == 0)
				DotsType2 = 1;
			else if( (FileName2[1] == L'.') && (FileName2[2] == 0) )
				DotsType2 = 2;
		}

		if(DotsType1 < DotsType2)
			break;

		if(DotsType2 != 0x7fffffff) {
			if(DotsType2 == DotsType1) return 1;
		} else {
			res = wcsicmp(FileName1, FileName2);
			if(res == 0) return 1;
			if(res < 0) break;
		}
	}

	FilenameSize = ((wcslen(FileInfo->cFileName) + 4) & ~3) * sizeof(WCHAR);
	ShortNameSize = ((wcslen(FileInfo->cAlternateFileName) + 4) & ~3) * sizeof(WCHAR);
	EntrySize = sizeof(FSR_SEARCH_ENTRY) + FilenameSize + ShortNameSize;
	if((SearchEntry = malloc(EntrySize)) == NULL)
		return 0;

	Cursor = (char*)SearchEntry + sizeof(FSR_SEARCH_ENTRY);

	SearchEntry->attributes = FileInfo->dwFileAttributes;
	SearchEntry->ctime = FileInfo->ftCreationTime;
	SearchEntry->atime = FileInfo->ftLastAccessTime;
	SearchEntry->mtime = FileInfo->ftLastWriteTime;
	SearchEntry->fsizelow = FileInfo->nFileSizeLow;
	SearchEntry->fsizehigh = FileInfo->nFileSizeHigh;

	SearchEntry->name = (void*)Cursor;
	Cursor += FilenameSize;
	wcscpy(SearchEntry->name, FileInfo->cFileName);

	SearchEntry->shortname = (void*)Cursor;
	Cursor += ShortNameSize;
	wcscpy(SearchEntry->shortname, FileInfo->cAlternateFileName);

	if(SearchData->entrycnt == SearchData->entrymaxcnt) {
		SearchData->entrymaxcnt = (SearchData->entrymaxcnt * 3 / 2) + 8;
		if((Temp = realloc(SearchData->entry, SearchData->entrymaxcnt * sizeof(FSR_SEARCH_ENTRY*))) == NULL) {
			free(SearchEntry);
			return 0;
		}
		SearchData->entry = Temp;
	}

	memmove(SearchData->entry + InsertIndex + 1, SearchData->entry + InsertIndex,
		(SearchData->entrycnt - InsertIndex) * sizeof(FSR_SEARCH_ENTRY*));
	SearchData->entrycnt++;

	SearchData->entry[InsertIndex] = SearchEntry;
	return 1;
}

// -------------------------------------------------------------------------------------------------

static int FsrGetSearchResult(FSR_SEARCH *SearchData, WIN32_FIND_DATA *FileInfo)
{
	FSR_SEARCH_ENTRY *SearchEntry;

	if(SearchData->entrypos == SearchData->entrycnt)
		return 0;

	SearchEntry = SearchData->entry[SearchData->entrypos++];
	FileInfo->dwFileAttributes = SearchEntry->attributes;
	FileInfo->ftCreationTime = SearchEntry->ctime;
	FileInfo->ftLastAccessTime = SearchEntry->atime;
	FileInfo->ftLastWriteTime = SearchEntry->mtime;
	FileInfo->nFileSizeHigh = SearchEntry->fsizehigh;
	FileInfo->nFileSizeLow = SearchEntry->fsizelow;
	FileInfo->dwReserved0 = 0;
	FileInfo->dwReserved1 = 0;
	wcscpy(FileInfo->cFileName, SearchEntry->name);
	wcscpy(FileInfo->cAlternateFileName, SearchEntry->shortname);

	return 1;
}

// -------------------------------------------------------------------------------------------------

HANDLE __stdcall HookFindFirstFileExW(WCHAR *SourceName, int level, WIN32_FIND_DATA *filedata, int searchop, void *filter, DWORD options)
{
	FSR_STATUS status;
	FSR_PATH_TYPE path_type;
	DWORD error;
	WCHAR backup_fnamepart_char;
	R2I_WSTR *SrcNameCopy = NULL;
	R2I_WSTR *TargetName = NULL;
	WCHAR *tmp, *fnamepart, *path_to_displace, *non_root_part;
	int used_pattern;
	HANDLE search_src = INVALID_HANDLE_VALUE;
	HANDLE search_tgt = INVALID_HANDLE_VALUE;
	WIN32_FIND_DATA wfd_src, wfd_tgt;
	FSR_SEARCH *srchdata = NULL;
	TypeFindFirstFileExW FndFirst;
	TypeFindNextFileW FndNext;
	TypeFindClose FndClose;

	FndFirst = HookGetOEP(HOOK_PROC_FINDFIRSTFILEEXW);
	FndNext = HookGetOEP(HOOK_PROC_FINDNEXTFILEW);
	FndClose = HookGetOEP(HOOK_PROC_FINDCLOSE);

	if(!FsrData.HooksEnabled) {
		return FndFirst(SourceName, level, filedata, searchop, filter, options);
	}

	// only level 0 (WIN32_FIND_DATA) is supported by api
	if(level != 0) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return INVALID_HANDLE_VALUE;
	}

	if((SrcNameCopy = WstrDup(SourceName, WSTR_LENGTH_UNKNOWN, MAX_PATH)) == NULL) {
		error = ERROR_NOT_ENOUGH_MEMORY;
		goto __error_exit;
	}

	// replace forward slashes to backslashes
	FsrReplaceSlashes(SrcNameCopy);

	// check path, find non-root part offset
	path_type = FsrGetPathType(SrcNameCopy->Buf, &non_root_part);

	if( (path_type == FSR_PATH_UNKNOWN) || (path_type == FSR_PATH_DEVICE) ) {
		error = ERROR_INVALID_NAME;
		goto __error_exit;
	}

	// remove trailing spaces then slashes
	tmp = non_root_part + wcslen(non_root_part);
	while( (tmp > non_root_part) && (tmp[-1] == L' ') ) tmp--;
	while( (tmp > non_root_part) && (tmp[-1] == L'\\') ) tmp--;
	*tmp = 0;

	// get last path component (filename or pattern part)
	fnamepart = non_root_part;
	if((tmp = wcsrchr(fnamepart, L'\\')) != NULL)
		fnamepart = tmp + 1;

	// no pattern - use whole path
	path_to_displace = SrcNameCopy->Buf;
	used_pattern = 0;

	backup_fnamepart_char = *fnamepart;

	// with pattern - use path without pattern
	// or "." if only pattern passed
	if(wcspbrk(fnamepart, L"*?") != NULL) {
		used_pattern = 1;
		if(fnamepart == SrcNameCopy->Buf) {
			path_to_displace = L".";
		}
		*fnamepart = 0;
	}

	// target path to lookup...
	status = FsrRedirectPath(&TargetName, path_to_displace, WSTR_LENGTH_UNKNOWN, 1);
	if(status != FSR_OK) {
		error = FsrStatusToWinError(status);
		goto __error_exit;
	}

	// no target path or no pattern (only one name will be returned)
	if( (TargetName == NULL) || (!used_pattern) ) {

		// no pattern - lookup target path
		if(TargetName != NULL) {
			search_tgt = FndFirst(TargetName->Buf, 0, filedata, searchop, filter, options);
			if(search_tgt != INVALID_HANDLE_VALUE) {
				// but return original passed filename
				*fnamepart = backup_fnamepart_char;
				wcscpy(filedata->cFileName, fnamepart);
				filedata->cAlternateFileName[0] = 0;

				free(TargetName);
				free(SrcNameCopy);
				return search_tgt;
			}
		}

		// no target path - just look original path
		free(TargetName);
		free(SrcNameCopy);
		return FndFirst(SourceName, 0, filedata, searchop, filter, options);

	}

	// combine target path with pattern
	*fnamepart = backup_fnamepart_char;
	if(!FsrAppendPath(&TargetName, fnamepart, WSTR_LENGTH_UNKNOWN)) {
		error = ERROR_NOT_ENOUGH_MEMORY;
		goto __error_exit;
	}

	// lookup original and target path with pattern
	search_src = FndFirst(SourceName, 0, &wfd_src, searchop, filter, options);
	search_tgt = FndFirst(TargetName->Buf, 0, &wfd_tgt, searchop, filter, options);

	// one lookup failed, just return another (or return error if both failed)
	if( (search_src == INVALID_HANDLE_VALUE) || (search_tgt == INVALID_HANDLE_VALUE) ) {

		// both lookup failed, return error
		if((search_src == INVALID_HANDLE_VALUE) && (search_tgt == INVALID_HANDLE_VALUE)) {
			error = GetLastError();
			goto __error_exit;
		}

		free(TargetName);
		free(SrcNameCopy);

		if(search_src != INVALID_HANDLE_VALUE) {
			memcpy(filedata, &wfd_src, sizeof(wfd_src));
			return search_src;
		} else {
			memcpy(filedata, &wfd_tgt, sizeof(wfd_tgt));
			return search_tgt;
		}

	}

	// both lookups succeeded, merge them...
	if((srchdata = calloc(1, sizeof(FSR_SEARCH))) == NULL) {
		error = ERROR_NOT_ENOUGH_MEMORY;
		goto __error_exit;
	}

	srchdata->hsearch = search_src;

	// target path lookup have priority
	do {
		if(!FsrAddSearchResult(srchdata, &wfd_tgt)) {
			error = ERROR_NOT_ENOUGH_MEMORY;
			goto __error_exit;
		}
	} while(FndNext(search_tgt, &wfd_tgt));

	// source path lookup next
	do {
		if(!FsrAddSearchResult(srchdata, &wfd_src)) {
			error = ERROR_NOT_ENOUGH_MEMORY;
			goto __error_exit;
		}
	} while(FndNext(search_src, &wfd_src));

	// add search to list
	EnterCriticalSection(&(FsrData.searches_lock));
	srchdata->next = FsrData.searches;
	FsrData.searches = srchdata;
	LeaveCriticalSection(&(FsrData.searches_lock));

	// close target search, and use source search as id
	FndClose(search_tgt);

	free(TargetName);
	free(SrcNameCopy);

	// return first found element
	FsrGetSearchResult(srchdata, filedata);
	return search_src;


__error_exit:

	if(search_tgt != INVALID_HANDLE_VALUE)
		FndClose(search_tgt);
	if(search_src != INVALID_HANDLE_VALUE)
		FndClose(search_src);

	if(srchdata != NULL) {
		while(srchdata->entrycnt > 0) {
			free(srchdata->entry[--(srchdata->entrycnt)]);
		}
		free(srchdata->entry);
		free(srchdata);
	}

	free(TargetName);
	free(SrcNameCopy);

	SetLastError(error);
	return INVALID_HANDLE_VALUE;
}

// -------------------------------------------------------------------------------------------------

BOOL __stdcall HookFindNextFileW(HANDLE search, WIN32_FIND_DATA *filedata)
{
	FSR_SEARCH *srchdata;
	TypeFindNextFileW FndNext;

	if(FsrData.HooksEnabled) {
		EnterCriticalSection(&(FsrData.searches_lock));
		// lookup this search handle
		for(srchdata = FsrData.searches; srchdata != NULL; srchdata = srchdata->next) {
			if(srchdata->hsearch == search)
				break;
		}
		// return next item
		if(srchdata != NULL) {
			if(!FsrGetSearchResult(srchdata, filedata)) {
				LeaveCriticalSection(&(FsrData.searches_lock));
				SetLastError(ERROR_NO_MORE_FILES);
				return FALSE;
			}
			LeaveCriticalSection(&(FsrData.searches_lock));
			return TRUE;
		}
		LeaveCriticalSection(&(FsrData.searches_lock));
	}

	FndNext = HookGetOEP(HOOK_PROC_FINDNEXTFILEW);
	return FndNext(search, filedata);
}

// -------------------------------------------------------------------------------------------------

BOOL __stdcall HookFindClose(HANDLE search)
{
	FSR_SEARCH **psrchdata, *srchdata;
	TypeFindClose FndClose;

	if(FsrData.HooksEnabled) {
		EnterCriticalSection(&(FsrData.searches_lock));
		// lookup this search handle
		for(psrchdata = &(FsrData.searches); *psrchdata != NULL; psrchdata = &((*psrchdata)->next)) {
			if((*psrchdata)->hsearch == search)
				break;
		}
		srchdata = *psrchdata;
		// free search memory
		if(srchdata != NULL) {
			*psrchdata = srchdata->next;
			while(srchdata->entrycnt > 0)
				free(srchdata->entry[--(srchdata->entrycnt)]);
			free(srchdata->entry);
			free(srchdata);
		}
		LeaveCriticalSection(&(FsrData.searches_lock));
	}

	FndClose = HookGetOEP(HOOK_PROC_FINDCLOSE);
	return FndClose(search);
}

// -------------------------------------------------------------------------------------------------

BOOL __stdcall HookCopyFileExW(WCHAR *src_name, WCHAR *dst_name, void *progress,
							   void *param, BOOL *cancel, DWORD flags)
{
	DWORD error;
	FSR_STATUS status;
	R2I_WSTR *src_new_name = NULL;
	R2I_WSTR *dst_new_name = NULL;
	DWORD attributes;
	TypeCopyFileExW CpyFile;
	TypeGetFileAttributesW GetAttr;

	CpyFile = HookGetOEP(HOOK_PROC_COPYFILEEXW);
	GetAttr = HookGetOEP(HOOK_PROC_GETFILEATTRIBUTESW);

	if(!FsrData.HooksEnabled) {
		return CpyFile(src_name, dst_name, progress, param, cancel, flags);
	}

	// get redirected source filename
	if((status = FsrRedirectPath(&src_new_name, src_name, WSTR_LENGTH_UNKNOWN, 1)) != FSR_OK) {
		error = FsrStatusToWinError(status);
		goto __error_exit;
	}

	// get redirected destination filename
	if((status = FsrRedirectPath(&dst_new_name, dst_name, WSTR_LENGTH_UNKNOWN, 1)) != FSR_OK) {
		error = FsrStatusToWinError(status);
		goto __error_exit;
	}

	// if source file redirected and redirected source file exists,
	// use redirected name otherwise original name
	if(src_new_name != NULL) {
		attributes = GetAttr(src_new_name->Buf);
		if(attributes == INVALID_FILE_ATTRIBUTES) {
			error = GetLastError();
			if((error != ERROR_FILE_NOT_FOUND) && (error != ERROR_PATH_NOT_FOUND))
				goto __error_exit;
		} else {
			src_name = src_new_name->Buf;
		}
	}

	// if destination name redirected, use it and create parent directories
	if(dst_new_name != NULL) {
		dst_name = dst_new_name->Buf;
		if((error = FsrCreateParentDirs(dst_name)) != ERROR_SUCCESS)
			goto __error_exit;
	}

	// copy file
	if(!CpyFile(src_name, dst_name, progress, param, cancel, flags)) {
		error = GetLastError();
		goto __error_exit;
	}

	free(src_new_name);
	free(dst_new_name);
	return TRUE;

__error_exit:
	free(src_new_name);
	free(dst_new_name);
	SetLastError(error);
	return FALSE;
}

// -------------------------------------------------------------------------------------------------

static BOOL FsrCopyTree(WCHAR *SourcePath, WCHAR *DestPath, int ReplaceExisting)
{
	HANDLE SrcFileHandle, SearchHandle;
	DWORD Error, Attributes, SourcePathLen, DestPathLen;
	WIN32_FIND_DATA FoundItem;
	WCHAR *SourcePathBuf, *DestPathBuf, *SrcPathCursor, *DestPathCursor, *FileName;
	TypeGetFileAttributesW GetAttr;
	TypeFindFirstFileExW FndFirst;
	TypeFindNextFileW FndNext;
	TypeFindClose FndClose;

	GetAttr = HookGetOEP(HOOK_PROC_GETFILEATTRIBUTESW);
	FndFirst = HookGetOEP(HOOK_PROC_FINDFIRSTFILEEXW);
	FndNext = HookGetOEP(HOOK_PROC_FINDNEXTFILEW);
	FndClose = HookGetOEP(HOOK_PROC_FINDCLOSE);

	// get attribute for source path
	if((Attributes = GetAttr(SourcePath)) == INVALID_FILE_ATTRIBUTES)
		return FALSE;

	// copy directory
	if(Attributes & FILE_ATTRIBUTE_DIRECTORY) {

		// create directory itself
		if(!CreateDirectory(DestPath, NULL)) {
			if(GetLastError() != ERROR_ALREADY_EXISTS)
				return FALSE;
		}

		// allocate buffers for source and destination file
		SourcePathLen = wcslen(SourcePath);
		DestPathLen = wcslen(DestPath);

		SourcePathBuf = malloc((SourcePathLen + MAX_PATH + 2) * sizeof(WCHAR));
		DestPathBuf = malloc((DestPathLen + MAX_PATH + 2) * sizeof(WCHAR));
		if((SourcePathBuf == NULL) || (DestPathBuf == NULL)) {
			free(SourcePathBuf);
			free(DestPathBuf);
			SetLastError(ERROR_NOT_ENOUGH_MEMORY);
			return FALSE;
		}

		// copy source and destination pathes to buffers
		wcscpy(SourcePathBuf, SourcePath);
		SrcPathCursor = SourcePathBuf + SourcePathLen;
		wcscpy(DestPathBuf, DestPath);
		DestPathCursor = DestPathBuf + DestPathLen;

		// append backslashes to buffers if needed
		if((SourcePathLen > 0) && FsrIsPathSeparator(SrcPathCursor[-1]))
			*(SrcPathCursor++) = L'\\';
		if((DestPathLen > 0) && FsrIsPathSeparator(DestPathCursor[-1]))
			*(DestPathCursor++) = L'\\';


		// append * mask to source path
		wcscpy(SrcPathCursor, L"*");

		Error = 0;

		// search items in source path
		SearchHandle = FndFirst(SourcePathBuf, FindExInfoStandard, &FoundItem, FindExSearchNameMatch, NULL, 0);
		if(SearchHandle == INVALID_HANDLE_VALUE) {
			Error = GetLastError();
		} else {
			// items search
			do {
				// ignore "." and ".." 's
				FileName = FoundItem.cFileName;
				if( (FileName[0] != L'.') || ((FileName[1] != 0) && ((FileName[1] != L'.') || (FileName[2] != 0))) ) {
					// copy item name to source and destination path buffer
					wcscpy(SrcPathCursor, FileName);
					wcscpy(DestPathCursor, FileName);
					// copy item
					if(!FsrCopyTree(SourcePathBuf, DestPathBuf, ReplaceExisting)) {
						Error = GetLastError();
						break;
					}
				}
			} while(FndNext(SearchHandle, &FoundItem));
			FndClose(SearchHandle);
		}

		free(SourcePathBuf);
		free(DestPathBuf);

		if(Error != 0) {
			SetLastError(Error);
			return FALSE;
		}

		return TRUE;
	}

	// copy file
	SrcFileHandle = CreateFile(SourcePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if(SrcFileHandle == INVALID_HANDLE_VALUE)
		return FALSE;
	FsrCopyFile(DestPath, SrcFileHandle);
	CloseHandle(SrcFileHandle);
	return TRUE;
}

// -------------------------------------------------------------------------------------------------

BOOL __stdcall HookMoveFileWithProgressW(WCHAR *src_name, WCHAR *dst_name,
										 void *progress, void *param, DWORD flags)
{
	BOOL success;
	DWORD error, replace;
	FSR_STATUS status;
	R2I_WSTR *src_new_name = NULL;
	R2I_WSTR *dst_new_name = NULL;
	DWORD attributes;
	TypeMoveFileWithProgressW MovFile;
	TypeCopyFileExW CpyFile;
	TypeGetFileAttributesW GetAttr;

	MovFile = HookGetOEP(HOOK_PROC_MOVEFILEWITHPROGRESSW);
	CpyFile = HookGetOEP(HOOK_PROC_COPYFILEEXW);
	GetAttr = HookGetOEP(HOOK_PROC_GETFILEATTRIBUTESW);

	if(!FsrData.HooksEnabled) {
		return MovFile(src_name, dst_name, progress, param, flags);
	}

	// get redirected source path
	if((status = FsrRedirectPath(&src_new_name, src_name, WSTR_LENGTH_UNKNOWN, 1)) != FSR_OK) {
		error = FsrStatusToWinError(status);
		goto __error_exit;
	}

	// get redirected destination path (if destination path given)
	if(dst_name != NULL) {
		if((status = FsrRedirectPath(&dst_new_name, dst_name, WSTR_LENGTH_UNKNOWN, 1)) != FSR_OK) {
			error = FsrStatusToWinError(status);
			goto __error_exit;
		}
	}

	// if source file redirected and exists, use it else use original source file
	if(src_new_name != NULL) {
		attributes = GetAttr(src_new_name->Buf);
		if(attributes == INVALID_FILE_ATTRIBUTES) {
			error = GetLastError();
			if((error != ERROR_FILE_NOT_FOUND) && (error != ERROR_PATH_NOT_FOUND))
				goto __error_exit;
		} else {
			src_name = src_new_name->Buf;
		}
	}

	// if destination filename redirected, use it and create parent dirs
	if(dst_new_name != NULL) {
		dst_name = dst_new_name->Buf;
		if((error = FsrCreateParentDirs(dst_name)) != ERROR_SUCCESS)
			goto __error_exit;
	}

	// source filename redirected, but redirected file not exists,
	// use original filename, but copy instead of move
	if( (src_new_name != NULL) && (src_name != src_new_name->Buf) ) {
		// destination file specified, copy original source file to redirected destination
		if(dst_name != NULL) {
			replace = (flags & MOVEFILE_REPLACE_EXISTING);
			success = FsrCopyTree(src_name, dst_name, replace);
		}
		// destination file not specified,
		// copy original source file to redirected source file location,
		// then call move file with specified arguments....
		else {
			success = FsrCopyTree(src_name, src_new_name->Buf, 0);
			if(success) {
				success = MovFile(src_new_name->Buf, dst_name, NULL, NULL, flags);
			}
		}
	}
	// move source file to destination
	else {
		success = MovFile(src_name, dst_name, progress, param, flags);
	}

	if(!success) {
		error = GetLastError();
		goto __error_exit;
	}

	free(dst_new_name);
	free(src_new_name);
	return TRUE;

__error_exit:
	free(dst_new_name);
	free(src_new_name);
	SetLastError(error);
	return FALSE;
}

// -------------------------------------------------------------------------------------------------

BOOL __stdcall HookGetDiskFreeSpaceW(WCHAR *source_name, DWORD *spc, DWORD *bps, DWORD *fc, DWORD *tc)
{
	FSR_STATUS status;
	R2I_WSTR *target_name;
	TypeGetDiskFreeSpaceW GetDiskFree;

	GetDiskFree = HookGetOEP(HOOK_PROC_GETDISKFREESPACEW);

	if(FsrData.HooksEnabled) {

		// get redirected path name
		if((status = FsrRedirectPath(&target_name, source_name, WSTR_LENGTH_UNKNOWN, 0)) != FSR_OK) {
			SetLastError(FsrStatusToWinError(status));
			return FALSE;
		}

		// redirection used, use root of redirected path
		if(target_name != NULL) {
			FsrGetRootPath(target_name);
			if(!GetDiskFree(target_name->Buf, spc, bps, fc, tc)) {
				FsrFreeKeepError(target_name);
				return FALSE;
			}
			free(target_name);
			return TRUE;
		}

	}

	return GetDiskFree(source_name, spc, bps, fc, tc);
}

// -------------------------------------------------------------------------------------------------

BOOL __stdcall HookGetDiskFreeSpaceExW(WCHAR *source_name, void *ava, void *tot, void *fre)
{
	FSR_STATUS status;
	R2I_WSTR *target_name;
	TypeGetDiskFreeSpaceExW GetDiskFreeEx;

	GetDiskFreeEx = HookGetOEP(HOOK_PROC_GETDISKFREESPACEEXW);

	if(FsrData.HooksEnabled) {

		// get redirected path name
		if((status = FsrRedirectPath(&target_name, source_name, WSTR_LENGTH_UNKNOWN, 0)) != FSR_OK) {
			SetLastError(FsrStatusToWinError(status));
			return FALSE;
		}

		// redirection used, use root of redirected path
		if(target_name != NULL) {
			FsrGetRootPath(target_name);
			if(!GetDiskFreeEx(target_name->Buf, ava, tot, fre)) {
				FsrFreeKeepError(target_name);
				return FALSE;
			}
			free(target_name);
			return TRUE;
		}

	}

	return GetDiskFreeEx(source_name, ava, tot, fre);
}

// -------------------------------------------------------------------------------------------------

static DWORD FsrSearchCreateProcess(WCHAR *CommandLine, R2I_WSTR **pResultCommandLine)
{
	DWORD Error, ArgumentsLen;
	WCHAR *CommandLineBuf = NULL;
	R2I_WSTR *FullFileName = NULL;
	R2I_WSTR *ResultCommandLine;
	WCHAR *FileName, *Temp, *Arguments = L"";
	WCHAR *Cursor, *NextBlock;
	WCHAR BackupChar;

	// copy command line to writeable buffer
	if((CommandLineBuf = wcsdup(CommandLine)) == NULL) {
		Error = ERROR_NOT_ENOUGH_MEMORY;
		goto __error_exit;
	}

	// quoted program filename
	if(*CommandLine == L'\"') {

		FileName = CommandLineBuf + 1;

		// find closing quote
		if((Temp = wcschr(FileName, L'\"')) != NULL) {
			*(Temp++) = 0;
			// skip whitespace before arguments
			while((*Temp == L' ') || (*Temp == L'\t'))
				Temp++;
			Arguments = Temp;
		}

		// find full filename
		if((Error = FsrSearchPath(NULL, FileName, L".exe", &FullFileName)) != ERROR_SUCCESS)
			goto __error_exit;

		// allocate buffer for command line with full filename
		ArgumentsLen = wcslen(Arguments);
		if((ResultCommandLine = WstrAlloc(FullFileName->Length + ArgumentsLen + 4)) == NULL) {
			Error = ERROR_NOT_ENOUGH_MEMORY;
			goto __error_exit;
		}
		Cursor = ResultCommandLine->Buf;

		// copy filename to command line
		*(Cursor++) = L'\"';
		wcscpy(Cursor, FullFileName->Buf);
		Cursor += FullFileName->Length;
		*(Cursor++) = L'\"';

		// add arguments to command line
		if(ArgumentsLen != 0) {
			*(Cursor++) = L' ';
			wcscpy(Cursor, Arguments);
			Cursor += ArgumentsLen;
		}

		*Cursor = 0;

		// return success
		*pResultCommandLine = ResultCommandLine;
		free(FullFileName);
		free(CommandLineBuf);
		return ERROR_SUCCESS;
	}

	// try to determine program name...
	NextBlock = CommandLineBuf;

	while(NextBlock != NULL) {

		// get next block (separated by whitespace), and cut path
		if((NextBlock = wcspbrk(NextBlock, L" \t")) != NULL) {
			BackupChar = *NextBlock;
			*NextBlock = 0;
		}

		// try to find full program filename
		Error = FsrSearchPath(NULL, CommandLineBuf, L".exe", &FullFileName);

		// if filename found,
		if(Error == ERROR_SUCCESS) {

			// get arguments
			if(NextBlock != NULL) {
				Arguments = NextBlock + 1;
				while((*Arguments == L' ') || (*Arguments == L'\t'))
					Arguments++;
			}

			// allocate full command line buffer
			ArgumentsLen = wcslen(Arguments);
			if((ResultCommandLine = WstrAlloc(FullFileName->Length + ArgumentsLen + 4)) == NULL) {
				Error = ERROR_NOT_ENOUGH_MEMORY;
				goto __error_exit;
			}
			Cursor = ResultCommandLine->Buf;

			// copy full program filename to buffer
			*(Cursor++) = L'\"';
			wcscpy(Cursor, FullFileName->Buf);
			Cursor += FullFileName->Length;
			*(Cursor++) = L'\"';

			// append arguments
			if(ArgumentsLen != 0) {
				*(Cursor++) = L' ';
				wcscpy(Cursor, Arguments);
				Cursor += ArgumentsLen;
			}

			*Cursor = 0;

			free(FullFileName);
			free(CommandLineBuf);

			// return success
			*pResultCommandLine = ResultCommandLine;
			return ERROR_SUCCESS;

		}

		// on error other than file/path not found, return it
		if((Error != ERROR_FILE_NOT_FOUND) && (Error != ERROR_PATH_NOT_FOUND))
			goto __error_exit;

		// restore characted cutted before next block
		if(NextBlock != NULL) {
			*NextBlock = BackupChar;
			while((*NextBlock == L' ') || (*NextBlock == L'\t'))
				NextBlock++;
		}

	}

__error_exit:
	free(FullFileName);
	free(CommandLineBuf);
	return Error;
}

// -------------------------------------------------------------------------------------------------

static DWORD FsrGetSetCurDir(WCHAR *dirname, R2I_WSTR **target_dirname, int create_dir)
{
	DWORD error;
	FSR_STATUS status;
	R2I_WSTR *target_path = NULL;
	TypeGetFileAttributesW GetAttr;

	GetAttr = HookGetOEP(HOOK_PROC_GETFILEATTRIBUTESW);

	// if original directory exists, use original directory
	if(GetAttr(dirname) != INVALID_FILE_ATTRIBUTES) {
		*target_dirname = NULL;
		return ERROR_SUCCESS;
	}

	error = GetLastError();
	if((error != ERROR_FILE_NOT_FOUND) && (error != ERROR_PATH_NOT_FOUND))
		goto __error_exit;

	// redirect directory
	if((status = FsrRedirectPath(&target_path, dirname, WSTR_LENGTH_UNKNOWN, 1)) != FSR_OK) {
		error = FsrStatusToWinError(status);
		goto __error_exit;
	}

	// if directory is not redirected, use original directory
	if(target_path == NULL) {
		*target_dirname = NULL;
		return ERROR_SUCCESS;
	}

	// create redirected directory
	if(create_dir) {
		if((error = FsrCreateDirs(target_path->Buf)) != ERROR_SUCCESS)
			goto __error_exit;
	}

	// use redirected directory
	*target_dirname = target_path;
	return ERROR_SUCCESS;

__error_exit:
	free(target_path);
	return error;
}

// -------------------------------------------------------------------------------------------------

BOOL __stdcall HookCreateProcessW(WCHAR *filename, WCHAR *command_line, void *process_security, void *thread_security,
								  BOOL inherit, DWORD options, void *environment, WCHAR *current_dir,
								  STARTUPINFOW *startup_info, PROCESS_INFORMATION *process_info)
{
	DWORD error;
	BOOL result;
	R2I_WSTR *filename_buf = NULL;
	R2I_WSTR *command_line_buf = NULL;
	R2I_WSTR *current_dir_buf = NULL;
	TypeCreateProcessW CrtProcess;

	CrtProcess = HookGetOEP(HOOK_PROC_CREATEPROCESSW);

	if(!FsrData.HooksEnabled) {
		result = CrtProcess(filename, command_line, process_security, thread_security, inherit,
			options, environment, current_dir, startup_info, process_info);
		return result;
	}

	// redirect filename or command line
	if(filename != NULL) {
		if((error = FsrSearchPath(NULL, filename, L".exe", &filename_buf)) != ERROR_SUCCESS)
			goto __error_exit;
		filename = filename_buf->Buf;
	} else {
		if((error = FsrSearchCreateProcess(command_line, &command_line_buf)) != ERROR_SUCCESS)
			goto __error_exit;
		command_line = command_line_buf->Buf;
	}

	// redirect current directory
	if(current_dir != NULL) {
		if((error = FsrGetSetCurDir(current_dir, &current_dir_buf, 1)) != ERROR_SUCCESS)
			goto __error_exit;
		if(current_dir_buf != NULL) {
			current_dir = current_dir_buf->Buf;
		}
	}

	// create process
	result = CrtProcess(filename, command_line, process_security, thread_security, inherit,
		options, environment, current_dir, startup_info, process_info);

	if(!result) {
		error = GetLastError();
		goto __error_exit;
	}

	free(current_dir_buf);
	free(command_line_buf);
	free(filename_buf);
	return TRUE;

__error_exit:
	free(current_dir_buf);
	free(command_line_buf);
	free(filename_buf);
	SetLastError(error);
	return FALSE;
}

// -------------------------------------------------------------------------------------------------

static DWORD FsrCharToUnicode(int CharSet, char *SrcString, WCHAR **pDestString)
{
	DWORD Error, BufferLength, ResultLength;
	WCHAR *Buffer, *Temp;

	BufferLength = strlen(SrcString) + 8;

	if((Buffer = malloc(BufferLength * sizeof(WCHAR))) == NULL)
		return ERROR_NOT_ENOUGH_MEMORY;

	ResultLength = MultiByteToWideChar(CharSet, 0, SrcString, -1, Buffer, BufferLength);
	if(ResultLength == 0) {

		Error = GetLastError();
		if(Error != ERROR_INSUFFICIENT_BUFFER) {
			free(Buffer);
			return Error;
		}

		if((BufferLength = MultiByteToWideChar(CharSet, 0, SrcString, -1, NULL, 0)) == 0) {
			Error = GetLastError();
			free(Buffer);
			return Error;
		}

		if((Temp = realloc(Buffer, BufferLength)) == NULL) {
			free(Buffer);
			return ERROR_NOT_ENOUGH_MEMORY;
		}
		Buffer = Temp;

		ResultLength = MultiByteToWideChar(CharSet, 0, SrcString, -1, Buffer, BufferLength);

		if(ResultLength == 0) {
			Error = GetLastError();
			free(Buffer);
			return Error;
		}

	}

	*pDestString = Buffer;
	return ERROR_SUCCESS;
}

// -------------------------------------------------------------------------------------------------

static DWORD FsrUnicodeToChar(int CharSet, WCHAR *SrcString, char **pDestString)
{
	DWORD Error, BufferLength, ResultLength;
	char *Buffer, *Temp;

	BufferLength = wcslen(SrcString) + 8;

	if((Buffer = malloc(BufferLength * sizeof(char))) == NULL)
		return ERROR_NOT_ENOUGH_MEMORY;

	ResultLength = WideCharToMultiByte(CharSet, 0, SrcString, -1, Buffer, BufferLength, NULL, NULL);

	if(ResultLength == 0) {

		Error = GetLastError();
		if(Error != ERROR_INSUFFICIENT_BUFFER) {
			free(Buffer);
			return Error;
		}

		if((BufferLength = WideCharToMultiByte(CharSet, 0, SrcString, -1, NULL, 0, NULL, NULL)) == 0) {
			Error = GetLastError();
			return Error;
		}

		if((Temp = realloc(Buffer, BufferLength * sizeof(char))) == NULL) {
			free(Buffer);
			return ERROR_NOT_ENOUGH_MEMORY;
		}

		Buffer = Temp;

		ResultLength = WideCharToMultiByte(CharSet, 0, SrcString, -1, Buffer, BufferLength, NULL, NULL);

		if(ResultLength == 0) {
			Error = GetLastError();
			free(Buffer);
			return Error;
		}

	}

	*pDestString = Buffer;
	return ERROR_SUCCESS;
}

// -------------------------------------------------------------------------------------------------

BOOL __stdcall HookCreateProcessA(char *filename, char *command_line, void *process_security, void *thread_security,
								  BOOL inherit, DWORD options, void *environment, char *current_dir,
								  STARTUPINFOA *startup_info, PROCESS_INFORMATION *process_info)
{
	BOOL success;
	WCHAR *buf;
	DWORD error;
	int encoding;
	char *filename_buf = NULL;
	char *command_line_buf = NULL;
	char *current_dir_buf = NULL;
	R2I_WSTR *reslt;
	TypeCreateProcessA CrtProcess;

	CrtProcess = HookGetOEP(HOOK_PROC_CREATEPROCESSA);

	if(!FsrData.HooksEnabled) {
		success = CrtProcess(filename, command_line, process_security, thread_security, inherit,
			options, environment, current_dir, startup_info, process_info);
		return success;
	}

	encoding = AreFileApisANSI() ? CP_ACP : CP_OEMCP;

	// redirect filename if given
	if(filename != NULL) {

		if((error = FsrCharToUnicode(encoding, filename, &buf)) != ERROR_SUCCESS)
			goto __error_exit;

		error = FsrSearchPath(NULL, buf, L".exe", &reslt);
		free(buf);
		if(error != ERROR_SUCCESS)
			goto __error_exit;

		error = FsrUnicodeToChar(encoding, reslt->Buf, &filename_buf);
		free(reslt);
		if(error != ERROR_SUCCESS)
			goto __error_exit;

		filename = filename_buf;

	}

	// else redirect command line
	else {

		if((error = FsrCharToUnicode(encoding, command_line, &buf)) != ERROR_SUCCESS)
			goto __error_exit;

		error = FsrSearchCreateProcess(buf, &reslt);
		free(buf);
		if(error != ERROR_SUCCESS)
			goto __error_exit;

		error = FsrUnicodeToChar(encoding, reslt->Buf, &command_line_buf);
		free(reslt);
		if(error != ERROR_SUCCESS)
			goto __error_exit;

		command_line = command_line_buf;

	}

	// redirect current directory
	if(current_dir != NULL) {

		if((error = FsrCharToUnicode(encoding, current_dir, &buf)) != ERROR_SUCCESS)
			goto __error_exit;

		error = FsrGetSetCurDir(buf, &reslt, 1);
		free(buf);

		if(error != ERROR_SUCCESS)
			goto __error_exit;

		if(reslt != NULL) {

			error = FsrUnicodeToChar(encoding, reslt->Buf, &current_dir_buf);
			free(reslt);
			if(error != ERROR_SUCCESS)
				goto __error_exit;

			current_dir = current_dir_buf;

		}

	}

	// create process
	success = CrtProcess(filename, command_line, process_security, thread_security, inherit,
		options, environment, current_dir, startup_info, process_info);
	if(!success) {
		error = GetLastError();
		goto __error_exit;
	}

	free(current_dir_buf);
	free(command_line_buf);
	free(filename_buf);
	return TRUE;

__error_exit:
	free(current_dir_buf);
	free(command_line_buf);
	free(filename_buf);
	SetLastError(error);
	return FALSE;
}

// -------------------------------------------------------------------------------------------------

HMODULE __stdcall HookLoadLibraryExW(WCHAR *filename, HANDLE hfile, DWORD flags)
{
	DWORD error;
	HMODULE hmodule;
	R2I_WSTR *target_name = NULL;
	TypeLoadLibraryExW LoadLib;

	LoadLib = HookGetOEP(HOOK_PROC_LOADLIBRARYEXW);

	if(!FsrData.HooksEnabled) {
		return LoadLib(filename, hfile, flags);
	}

	// library can be referenced by filename without path if already loaded
	if( (wcspbrk(filename, L":\\/") == NULL) && (GetModuleHandle(filename) != NULL) ) {
		return LoadLib(filename, hfile, flags);
	}

	// search path considering redirection
	if((error = FsrSearchPath(NULL, filename, L".dll", &target_name)) != ERROR_SUCCESS)
		goto __error_exit;

	// load library
	if((hmodule = LoadLib(target_name->Buf, hfile, flags)) == NULL) {
		error = GetLastError();
		goto __error_exit;
	}

	free(target_name);
	return hmodule;

__error_exit:
	free(target_name);
	SetLastError(error);
	return NULL;
}

// -------------------------------------------------------------------------------------------------

static DWORD FsrSetCurDir(WCHAR *dirname)
{
	DWORD error;
	R2I_WSTR *target_dirname = NULL;
	TypeSetCurrentDirectoryW SetCurDir;

	SetCurDir = HookGetOEP(HOOK_PROC_SETCURRENTDIRECTORYW);

	if((error = FsrGetSetCurDir(dirname, &target_dirname, 0)) != ERROR_SUCCESS)
		return error;

	if(target_dirname != NULL)
		dirname = target_dirname->Buf;

	if(!SetCurDir(dirname)) {
		free(target_dirname);
		return GetLastError();
	}

	free(target_dirname);
	return ERROR_SUCCESS;
}

// -------------------------------------------------------------------------------------------------

BOOL __stdcall HookSetCurrentDirectoryW(WCHAR *dirname)
{
	DWORD error;
	TypeSetCurrentDirectoryW SetCurDir;

	if(!FsrData.HooksEnabled) {
		SetCurDir = HookGetOEP(HOOK_PROC_SETCURRENTDIRECTORYW);
		return SetCurDir(dirname);
	}

	if((error = FsrSetCurDir(dirname)) != ERROR_SUCCESS) {
		SetLastError(error);
		return FALSE;
	}

	return TRUE;
}

// -------------------------------------------------------------------------------------------------

BOOL __stdcall HookSetCurrentDirectoryA(char *dirname)
{
	WCHAR *dirname_buf;
	int charset;
	DWORD error;
	TypeSetCurrentDirectoryA SetCurDir;

	if(!FsrData.HooksEnabled) {
		SetCurDir = HookGetOEP(HOOK_PROC_SETCURRENTDIRECTORYA);
		return SetCurDir(dirname);
	}

	charset = AreFileApisANSI() ? CP_ACP : CP_OEMCP;
	if((error = FsrCharToUnicode(charset, dirname, &dirname_buf)) != ERROR_SUCCESS) {
		SetLastError(error);
		return FALSE;
	}

	error = FsrSetCurDir(dirname_buf);

	free(dirname_buf);

	if(error != ERROR_SUCCESS) {
		SetLastError(error);
		return FALSE;
	}

	return TRUE;
}

// -------------------------------------------------------------------------------------------------

static DWORD FsrGetIniFileName(R2I_WSTR **pTargetFilename, WCHAR *SourceFilename, int ReadOnly)
{
	FSR_STATUS status;
	FSR_PATH_TYPE PathType;
	R2I_WSTR *SourcePathBuf = NULL;
	R2I_WSTR *TargetFilename = NULL;
	R2I_WSTR *Temp;
	WCHAR *NonRoot;
	DWORD Error, Attributes;
	HANDLE SourceFileHandle;
	TypeGetFileAttributesW GetAttr;
	DWORD SourceFilenameLength;

	GetAttr = HookGetOEP(HOOK_PROC_GETFILEATTRIBUTESW);

	// default filename is win.ini in windir
	if(SourceFilename == NULL) {
		SourceFilename = L"win.ini";
	}

	SourceFilenameLength = wcslen(SourceFilename);

	// get path type
	if((PathType = FsrGetPathType(SourceFilename, &NonRoot)) == FSR_PATH_UNKNOWN) {
		Error = ERROR_INVALID_NAME;
		goto __error_exit;
	}

	// default directory is windir
	if(PathType == FSR_PATH_CURDIR_RELATIVE) {
		if((SourcePathBuf = WstrDup(FsrData.WinDir->Buf, WSTR_LENGTH_UNKNOWN, MAX_PATH)) == NULL) {
			Error = ERROR_NOT_ENOUGH_MEMORY;
			goto __error_exit;
		}
		if(!FsrAppendPath(&SourcePathBuf, SourceFilename, SourceFilenameLength)) {
			Error = ERROR_NOT_ENOUGH_MEMORY;
			goto __error_exit;
		}
		SourceFilename = SourcePathBuf->Buf;
		SourceFilenameLength = SourcePathBuf->Length;
	}

	// get redirected path
	if((status = FsrRedirectPath(&TargetFilename, SourceFilename, SourceFilenameLength, 1)) != FSR_OK) {
		Error = FsrStatusToWinError(status);
		goto __error_exit;
	}

	// no redirection - normalize original path and use
	if(TargetFilename == NULL) {
		status = FsrNormalizePath(pTargetFilename, &NonRoot, SourceFilename, SourceFilenameLength, 1);
		free(SourcePathBuf);
		return FsrStatusToWinError(status);
	}

	// if target file found, use it
	Attributes = GetAttr(TargetFilename->Buf);
	if(Attributes != INVALID_FILE_ATTRIBUTES) {
		*pTargetFilename = TargetFilename;
		free(SourcePathBuf);
		return ERROR_SUCCESS;
	}

	// on error checking target file presence, return
	Error = GetLastError();
	if((Error != ERROR_FILE_NOT_FOUND) && (Error != ERROR_PATH_NOT_FOUND))
		goto __error_exit;

	// only read operation, normalize original path and use
	if(ReadOnly) {
		if((status = FsrNormalizePath(&Temp, &NonRoot, SourceFilename, SourceFilenameLength, 1)) != FSR_OK) {
			Error = FsrStatusToWinError(status);
			goto __error_exit;
		}
		free(TargetFilename);
		free(SourcePathBuf);
		*pTargetFilename = Temp;
		return ERROR_SUCCESS;
	}

	// if target path not found, create it
	if(Error == ERROR_PATH_NOT_FOUND) {
		if((Error = FsrCreateParentDirs(TargetFilename->Buf)) != ERROR_SUCCESS)
			goto __error_exit;
	}

	// copy source file to target location
	SourceFileHandle = CreateFile(SourceFilename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if(SourceFileHandle == INVALID_HANDLE_VALUE) {
		Error = GetLastError();
		if((Error != ERROR_FILE_NOT_FOUND) && (Error != ERROR_PATH_NOT_FOUND))
			goto __error_exit;
	} else {
		FsrCopyFile(TargetFilename->Buf, SourceFileHandle);
		CloseHandle(SourceFileHandle);
	}

	// return success
	free(SourcePathBuf);
	*pTargetFilename = TargetFilename;
	return ERROR_SUCCESS;

__error_exit:
	free(TargetFilename);
	free(SourcePathBuf);
	return Error;
}

// -------------------------------------------------------------------------------------------------

DWORD __stdcall HookGetPrivateProfileStringW(WCHAR *GroupName, WCHAR *KeyName, WCHAR *DefValue,
											 WCHAR *Buf, DWORD BufLen, WCHAR *FileName)
{
	DWORD Error, Result;
	R2I_WSTR *TargetFilename;
	TypeGetPrivateProfileStringW GetPrivProfileString;

	GetPrivProfileString = HookGetOEP(HOOK_PROC_GETPRIVATEPROFILESTRINGW);
	if(!FsrData.HooksEnabled) {
		return GetPrivProfileString(GroupName, KeyName, DefValue, Buf, BufLen, FileName);
	}

	if((Error = FsrGetIniFileName(&TargetFilename, FileName, 1)) != ERROR_SUCCESS) {
		SetLastError(Error);
		return 0;
	}

	if((Result = GetPrivProfileString(GroupName, KeyName, DefValue, Buf, BufLen, TargetFilename->Buf)) == 0) {
		FsrFreeKeepError(TargetFilename);
		return 0;
	}

	free(TargetFilename);
	return Result;
}

// -------------------------------------------------------------------------------------------------

DWORD __stdcall HookGetPrivateProfileSectionW(WCHAR *GroupName, WCHAR *Buf, DWORD BufLen, WCHAR *FileName)
{
	DWORD Error, Result;
	R2I_WSTR *TargetFilename;
	TypeGetPrivateProfileSectionW GetPrivProfileSection;

	GetPrivProfileSection = HookGetOEP(HOOK_PROC_GETPRIVATEPROFILESECTIONW);
	if(!FsrData.HooksEnabled) {
		return GetPrivProfileSection(GroupName, Buf, BufLen, FileName);
	}

	if((Error = FsrGetIniFileName(&TargetFilename, FileName, 1)) != ERROR_SUCCESS) {
		SetLastError(Error);
		return 0;
	}

	if((Result = GetPrivProfileSection(GroupName, Buf, BufLen, TargetFilename->Buf)) == 0) {
		FsrFreeKeepError(TargetFilename);
		return 0;
	}

	free(TargetFilename);
	return Result;
}

// -------------------------------------------------------------------------------------------------

BOOL __stdcall HookWritePrivateProfileStringW(WCHAR *GroupName, WCHAR *KeyName, WCHAR *Value, WCHAR *FileName)
{
	BOOL Result;
	DWORD Error;
	R2I_WSTR *TargetFilename;
	TypeWritePrivateProfileStringW WritePrivProfileString;

	WritePrivProfileString = HookGetOEP(HOOK_PROC_WRITEPRIVATEPROFILESTRINGW);
	if(!FsrData.HooksEnabled) {
		return WritePrivProfileString(GroupName, KeyName, Value, FileName);
	}

	if((Error = FsrGetIniFileName(&TargetFilename, FileName, 0)) != ERROR_SUCCESS) {
		SetLastError(Error);
		return 0;
	}

	if((Result = WritePrivProfileString(GroupName, KeyName, Value, TargetFilename->Buf)) == FALSE) {
		FsrFreeKeepError(TargetFilename);
		return FALSE;
	}

	free(TargetFilename);
	return Result;
}

// -------------------------------------------------------------------------------------------------

BOOL __stdcall HookWritePrivateProfileSectionW(WCHAR *GroupName, WCHAR *Data, WCHAR *FileName)
{
	BOOL Result;
	DWORD Error;
	R2I_WSTR *TargetFilename;
	TypeWritePrivateProfileSectionW WritePrivProfileSection;

	WritePrivProfileSection = HookGetOEP(HOOK_PROC_WRITEPRIVATEPROFILESECTIONW);
	if(!FsrData.HooksEnabled) {
		return WritePrivProfileSection(GroupName, Data, FileName);
	}

	if((Error = FsrGetIniFileName(&TargetFilename, FileName, 0)) != ERROR_SUCCESS) {
		SetLastError(Error);
		return 0;
	}

	if((Result = WritePrivProfileSection(GroupName, Data, TargetFilename->Buf)) == FALSE) {
		FsrFreeKeepError(TargetFilename);
		return FALSE;
	}

	free(TargetFilename);
	return Result;
}

// -------------------------------------------------------------------------------------------------

static DWORD FsrGetIniFilenameA(char **pTargetFilename, char *SourceFilename, int ReadOnly)
{
	WCHAR *SourceFilenameUnicode;
	R2I_WSTR *TargetFilename;
	int CharSet;
	DWORD Error;

	CharSet = AreFileApisANSI() ? CP_ACP : CP_OEMCP;

	if(SourceFilename == NULL) {
		SourceFilenameUnicode = NULL;
	} else {
		if((Error = FsrCharToUnicode(CharSet, SourceFilename, &SourceFilenameUnicode)) != ERROR_SUCCESS)
			return Error;
	}

	Error = FsrGetIniFileName(&TargetFilename, SourceFilenameUnicode, ReadOnly);
	free(SourceFilenameUnicode);
	if(Error != ERROR_SUCCESS) {
		return Error;
	}

	Error = FsrUnicodeToChar(CharSet, TargetFilename->Buf, pTargetFilename);
	free(TargetFilename);
	return Error;
}

// -------------------------------------------------------------------------------------------------

DWORD __stdcall HookGetPrivateProfileStringA(char *GroupName, char *KeyName, char *DefValue,
											 char *Buf, DWORD BufLen, char *FileName)
{
	DWORD Error, Result;
	char *TargetFilename;
	TypeGetPrivateProfileStringA GetPrivProfileString;

	GetPrivProfileString = HookGetOEP(HOOK_PROC_GETPRIVATEPROFILESTRINGA);
	if(!FsrData.HooksEnabled) {
		return GetPrivProfileString(GroupName, KeyName, DefValue, Buf, BufLen, FileName);
	}

	if((Error = FsrGetIniFilenameA(&TargetFilename, FileName, 1)) != ERROR_SUCCESS) {
		SetLastError(Error);
		return 0;
	}

	if((Result = GetPrivProfileString(GroupName, KeyName, DefValue, Buf, BufLen, TargetFilename)) == 0) {
		FsrFreeKeepError(TargetFilename);
		return 0;
	}

	free(TargetFilename);
	return Result;
}

// -------------------------------------------------------------------------------------------------

DWORD __stdcall HookGetPrivateProfileSectionA(char *GroupName, char *Buf, DWORD BufLen, char *FileName)
{
	DWORD Error, Result;
	char *TargetFilename;
	TypeGetPrivateProfileSectionA GetPrivProfileSection;

	GetPrivProfileSection = HookGetOEP(HOOK_PROC_GETPRIVATEPROFILESECTIONA);
	if(!FsrData.HooksEnabled) {
		return GetPrivProfileSection(GroupName, Buf, BufLen, FileName);
	}

	if((Error = FsrGetIniFilenameA(&TargetFilename, FileName, 1)) != ERROR_SUCCESS) {
		SetLastError(Error);
		return 0;
	}

	if((Result = GetPrivProfileSection(GroupName, Buf, BufLen, TargetFilename)) == 0) {
		FsrFreeKeepError(TargetFilename);
		return 0;
	}

	free(TargetFilename);
	return Result;
}

// -------------------------------------------------------------------------------------------------

BOOL __stdcall HookWritePrivateProfileStringA(char *GroupName, char *KeyName, char *Value, char *FileName)
{
	BOOL Result;
	DWORD Error;
	char *TargetFilename;
	TypeWritePrivateProfileStringA WritePrivProfileString;

	WritePrivProfileString = HookGetOEP(HOOK_PROC_WRITEPRIVATEPROFILESTRINGA);
	if(!FsrData.HooksEnabled) {
		return WritePrivProfileString(GroupName, KeyName, Value, FileName);
	}

	if((Error = FsrGetIniFilenameA(&TargetFilename, FileName, 0)) != ERROR_SUCCESS) {
		SetLastError(Error);
		return 0;
	}

	if((Result = WritePrivProfileString(GroupName, KeyName, Value, TargetFilename)) == FALSE) {
		FsrFreeKeepError(TargetFilename);
		return FALSE;
	}

	free(TargetFilename);
	return Result;
}

// -------------------------------------------------------------------------------------------------

BOOL __stdcall HookWritePrivateProfileSectionA(char *GroupName, char *Data, char *FileName)
{
	BOOL Result;
	DWORD Error;
	char *TargetFilename;
	TypeWritePrivateProfileSectionA WritePrivProfileSection;

	WritePrivProfileSection = HookGetOEP(HOOK_PROC_WRITEPRIVATEPROFILESECTIONA);
	if(!FsrData.HooksEnabled) {
		return WritePrivProfileSection(GroupName, Data, FileName);
	}

	if((Error = FsrGetIniFilenameA(&TargetFilename, FileName, 0)) != ERROR_SUCCESS) {
		SetLastError(Error);
		return 0;
	}

	if((Result = WritePrivProfileSection(GroupName, Data, FileName)) == FALSE) {
		FsrFreeKeepError(TargetFilename);
		return FALSE;
	}

	free(TargetFilename);
	return Result;
}

// -------------------------------------------------------------------------------------------------

static WCHAR *FsrExpandEnv(WCHAR *EnvStr)
{
	WCHAR *Buffer, *Temp;
	DWORD BufLen, ResultLen;

	BufLen = MAX_PATH;
	if((Buffer = malloc(BufLen)) == NULL)
		return NULL;

	ResultLen = ExpandEnvironmentStrings(EnvStr, Buffer, BufLen);
	if(ResultLen > BufLen) {
		BufLen = ResultLen;
		if((Temp = realloc(Buffer, BufLen * sizeof(WCHAR))) == NULL) {
			free(Buffer);
			return NULL;
		}
		Buffer = Temp;
		ResultLen = ExpandEnvironmentStrings(EnvStr, Buffer, BufLen);
	}

	if( (ResultLen == 0) || (ResultLen > BufLen) ) {
		free(Buffer);
		return NULL;
	}

	return Buffer;
}

// -------------------------------------------------------------------------------------------------

static WCHAR *FsrGetEnv(WCHAR *Name)
{
	WCHAR *Buffer, *Temp;
	DWORD BufLen, ResultLen;

	BufLen = MAX_PATH;
	if((Buffer = malloc(BufLen * sizeof(WCHAR))) == NULL)
		return NULL;

	ResultLen = GetEnvironmentVariable(Name, Buffer, BufLen);
	if(ResultLen > BufLen) {
		BufLen = ResultLen;
		if((Temp = realloc(Buffer, BufLen * sizeof(WCHAR))) == NULL) {
			free(Buffer);
			return NULL;
		}
		Buffer = Temp;
		ResultLen = GetEnvironmentVariable(Name, Buffer, BufLen);
	}

	if( (ResultLen == 0) || (ResultLen >= BufLen) ) {
		free(Buffer);
		return NULL;
	}

	return Buffer;
}

// -------------------------------------------------------------------------------------------------

static WCHAR *FsrGetUserSid()
{
	WCHAR *SidName;
	HANDLE TokenHandle;
	UNICODE_STRING SidBuffer;
	SID_AND_ATTRIBUTES *TokenInfo;
	ULONG SidNameLength, TokenInfoBufSize, Status, TokenInfoSize;

	// open token for current proccess
	Status = OpenToken(GetCurrentProcess(), TOKEN_QUERY, &TokenHandle);
	if(Status != STATUS_SUCCESS)
		return NULL;

	// get token information (256 bytes should be really enough)
	TokenInfoBufSize = 256;
	if((TokenInfo = malloc(TokenInfoBufSize)) == NULL) {
		CloseHdl(TokenHandle);
		return NULL;
	}

	Status = QueryToken(TokenHandle, TokenUser, TokenInfo, TokenInfoBufSize, &TokenInfoSize);
	CloseHdl(TokenHandle);

	if(Status != STATUS_SUCCESS) {
		free(TokenInfo);
		return NULL;
	}

	// format user sid
	Status = FormatSid(&SidBuffer, TokenInfo->Sid, TRUE);
	free(TokenInfo);
	if(Status != STATUS_SUCCESS)
		return NULL;

	// return sid name as zero-terminated string
	SidNameLength = SidBuffer.Length / sizeof(WCHAR);

	if((SidName = malloc((SidNameLength + 1) * sizeof(WCHAR))) != NULL) {
		wcsncpy(SidName, (void*)(SidBuffer.Buffer), SidNameLength);
		SidName[SidNameLength] = 0;
	}

	FreeString(&SidBuffer);

	return SidName;
}

// -------------------------------------------------------------------------------------------------

static WCHAR *FsrGetUserProfileFromReg(WCHAR *SidName)
{
	WCHAR *UserProfilePath, *ExpandedPath;
	HANDLE KeyHandle;
	WCHAR * BaseKeyName;
	ULONG PathLength, BaseKeyNameLen, SidNameLen;
	ULONG KeyValueBufSize, KeyValueSize, Status;
	UNICODE_STRING KeyNameData, ValueNameData;
	OBJECT_ATTRIBUTES ObjectAttr;
	KEY_VALUE_PARTIAL_INFORMATION *KeyValue, *TempKeyValue;

	SidNameLen = wcslen(SidName) * sizeof(WCHAR);

	// base key for user profiles
	BaseKeyName = L"\\Registry\\Machine\\Software\\Microsoft\\"
		L"Windows NT\\CurrentVersion\\ProfileList\\";
	BaseKeyNameLen = wcslen(BaseKeyName) * sizeof(WCHAR);

	// append sid to profile base key
	KeyNameData.Length = (USHORT)(BaseKeyNameLen + SidNameLen);
	KeyNameData.MaximumLength = KeyNameData.Length;
	if((KeyNameData.Buffer = malloc(KeyNameData.MaximumLength)) == NULL) {
		return NULL;
	}
	memcpy((char*)(KeyNameData.Buffer), BaseKeyName, BaseKeyNameLen);
	memcpy((char*)(KeyNameData.Buffer) + BaseKeyNameLen, SidName, SidNameLen);

	// open profiles key
	memset(&ObjectAttr, 0, sizeof(ObjectAttr));
	ObjectAttr.Length = sizeof(ObjectAttr);
	ObjectAttr.ObjectName = &KeyNameData;
	ObjectAttr.Attributes = 0x640;

	Status = OpenKey(&KeyHandle, KEY_QUERY_VALUE, &ObjectAttr);
	free(KeyNameData.Buffer);
	if(Status != STATUS_SUCCESS)
		return NULL;

	// get profile image path value
	KeyValueBufSize = 256;
	if((KeyValue = malloc(KeyValueBufSize)) == NULL) {
		CloseHdl(KeyHandle);
		return NULL;
	}

	ValueNameData.Buffer = L"ProfileImagePath";
	ValueNameData.MaximumLength = ValueNameData.Length = wcslen(ValueNameData.Buffer) * sizeof(WCHAR);

	Status = QueryValue(KeyHandle, &ValueNameData, KeyValuePartialInformation, KeyValue, KeyValueBufSize, &KeyValueSize);
	if((Status == STATUS_BUFFER_OVERFLOW) || (Status == STATUS_BUFFER_TOO_SMALL)) {
		if((TempKeyValue = realloc(KeyValue, KeyValueSize)) == NULL) {
			free(KeyValue);
			CloseHdl(KeyHandle);
			return NULL;
		}
		KeyValue = TempKeyValue;
		KeyValueBufSize = KeyValueSize;
		Status = QueryValue(KeyHandle, &ValueNameData, KeyValuePartialInformation, KeyValue, KeyValueBufSize, &KeyValueSize);
	}
	CloseHdl(KeyHandle);

	if(Status != STATUS_SUCCESS) {
		free(KeyValue);
		return NULL;
	}

	// check value type
	if( (KeyValue->Type != REG_SZ) && (KeyValue->Type != REG_EXPAND_SZ) ) {
		free(KeyValue);
		return NULL;
	}

	// convert profile path to zero-terminated string
	PathLength = KeyValue->DataLength / sizeof(WCHAR);
	if((UserProfilePath = malloc((PathLength + 1) * sizeof(WCHAR))) == NULL) {
		free(KeyValue);
		return NULL;
	}
	wcsncpy(UserProfilePath, (void*)(KeyValue->Data), PathLength);
	UserProfilePath[PathLength] = 0;
	free(KeyValue);

	// return path if type = REG_SZ
	if(KeyValue->Type == REG_SZ)
		return UserProfilePath;

	// expand and return path if type = REG_EXPAND_SZ
	ExpandedPath = FsrExpandEnv(UserProfilePath);
	free(UserProfilePath);

	return ExpandedPath;
}

// -------------------------------------------------------------------------------------------------

FSR_STATUS FsrRedirectsAddSystem()
{
	FSR_STATUS Status;
	R2I_WSTR *PathName;
	DWORD NameLen;
	WCHAR *WinDir = NULL, *Temp = NULL;

	if((PathName = WstrAlloc(MAX_PATH)) == NULL) {
		return FSR_NOMEM;
	}

	// get windows directory and replace slashes to backslashes
	if(!(NameLen = GetWindowsDirectory(PathName->Buf, PathName->BufLength))) {
		free(PathName);
		return FSR_ERROR;
	}
	PathName->Length = NameLen;

	FsrReplaceSlashes(PathName);

	// add windows directory
	if((Status = FsrRedirectAdd(PathName->Buf, FSR_SUBDIR_WINDOWS, 0)) != FSR_OK) {
		free(PathName);
		return Status;
	}

	// add system drive root directory
	if(!FsrGetRootPath(PathName)) {
		free(PathName);
		return FSR_ERROR;
	}

	if((Status = FsrRedirectAdd(PathName->Buf, FSR_SUBDIR_SYSTEMDRIVE, 0)) != FSR_OK) {
		free(PathName);
		return Status;
	}

	// set systemdrive variable if not set
	if((Temp = FsrGetEnv(L"SystemDrive")) == NULL) {
		Temp = PathName->Buf + PathName->Length - 1;
		if(*Temp == L'\\')
			*Temp = 0;
		SetEnvironmentVariable(L"SystemDrive", PathName->Buf);
	} else {
		free(Temp);
	}

	// add system directory
	if((NameLen = GetSystemDirectory(PathName->Buf, PathName->BufLength)) != 0) {
		PathName->Length = NameLen;
		FsrReplaceSlashes(PathName);
		FsrRedirectAdd(PathName->Buf, FSR_SUBDIR_SYSTEM, 0);
	}

	// add temp directory
	if((NameLen = GetTempPath(PathName->BufLength, PathName->Buf)) != 0) {
		PathName->Length = NameLen;
		FsrReplaceSlashes(PathName);
		FsrRedirectAdd(PathName->Buf, FSR_SUBDIR_TEMP, 0);
	}

	free(PathName);
	return FSR_OK;
}

// -------------------------------------------------------------------------------------------------

static void FsrRedirectsAddCustom(WCHAR *UserProfile)
{
}

// -------------------------------------------------------------------------------------------------

static int FsrMakeZeroString(WCHAR **pBuf, ULONG *pBufSize, WCHAR *Data, ULONG DataSize)
{
	ULONG BufSize = *pBufSize;
	WCHAR *Buf = *pBuf;
	ULONG Len, RequiredSize;

	Len = DataSize / sizeof(WCHAR);
	RequiredSize = (Len + 1) * sizeof(WCHAR);

	if(BufSize < RequiredSize) {
		RequiredSize = (RequiredSize + 31) & ~31;
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

static int FsrRedirectsAddUser()
{
	int Success = 0;
	WCHAR *UserSidName = NULL;
	WCHAR *UserProfilePath = NULL;
	WCHAR *BaseKeyName, *KeyName = NULL;
	ULONG BaseKeyNameLen, UserSidNameLen, KeyNameSize;
	ULONG Index, NtStatus;
	HANDLE KeyHandle = NULL;
	UNICODE_STRING StrData;
	OBJECT_ATTRIBUTES ObjectAttr;
	KEY_VALUE_FULL_INFORMATION *ValueInfo = NULL;
	ULONG ValueInfoBufSize, ValueInfoSize;
	WCHAR *EntryName = NULL, *EntryValue = NULL, *EntryExpandedPath = NULL, *p;
	ULONG EntryNameMaxLen = 0, EntryValueMaxLen = 0, ExpandedPathLen, ExpandedPathMaxLen;
	void *Temp;

	// get user profile path and sid
	UserProfilePath = FsrGetEnv(L"UserProfile");
	UserSidName = FsrGetUserSid();

	// userprofile variable not set, restore it from registry and set
	if(UserProfilePath == NULL) {
		if(UserSidName == NULL)
			goto __cleanup;
		if((UserProfilePath = FsrGetUserProfileFromReg(UserSidName)) == NULL)
			goto __cleanup;
		SetEnvironmentVariable(L"UserProfile", UserProfilePath);
	}

	// add redirection for userprofile path
	FsrRedirectAdd(UserProfilePath, FSR_SUBDIR_USER, 0);

	// add custom redirections
	FsrRedirectsAddCustom(UserProfilePath);

	// format keyname for getting user's directories
	if(UserSidName == NULL)
		goto __cleanup;

	BaseKeyName = L"\\Registry\\User\\%s\\Software\\Microsoft\\"
		L"Windows\\CurrentVersion\\Explorer\\Shell Folders";

	BaseKeyNameLen = wcslen(BaseKeyName);
	UserSidNameLen = wcslen(UserSidName);

	KeyNameSize = (BaseKeyNameLen + UserSidNameLen + 4) * sizeof(WCHAR);
	if((KeyName = malloc(KeyNameSize)) == NULL)
		goto __cleanup;

	swprintf(KeyName, BaseKeyName, UserSidName);

	// open key
	StrData.Buffer = KeyName;
	StrData.Length = (USHORT)(wcslen(KeyName) * sizeof(WCHAR));
	StrData.MaximumLength = (USHORT)KeyNameSize;

	memset(&ObjectAttr, 0, sizeof(ObjectAttr));
	ObjectAttr.Length = sizeof(ObjectAttr);
	ObjectAttr.ObjectName = &StrData;
	ObjectAttr.Attributes = 0x640;

	NtStatus = OpenKey(&KeyHandle, KEY_QUERY_VALUE, &ObjectAttr);
	if(NtStatus != STATUS_SUCCESS)
		goto __cleanup;

	// allocate buffer for getting directories
	ValueInfoBufSize = 0x300;
	ValueInfo = malloc(ValueInfoBufSize);

	ExpandedPathMaxLen = MAX_PATH;
	EntryExpandedPath = malloc(ExpandedPathMaxLen * sizeof(WCHAR));

	if( (ValueInfo == NULL) || (EntryExpandedPath == NULL) )
		goto __cleanup;

	// enumerate directories
	for(Index = 0; ; Index++) {

		// query directory name
		NtStatus = EnumValue(KeyHandle, Index, KeyValueFullInformation, ValueInfo, ValueInfoBufSize, &ValueInfoSize);
		if((NtStatus == STATUS_BUFFER_TOO_SMALL) || (NtStatus == STATUS_BUFFER_OVERFLOW)) {
			ValueInfoBufSize = (ValueInfoSize + 0x7f) & ~0x7f;
			if((Temp = realloc(ValueInfo, ValueInfoBufSize)) == NULL)
				goto __cleanup;
			ValueInfo = Temp;
			NtStatus = EnumValue(KeyHandle, Index, KeyValueFullInformation, ValueInfo, ValueInfoBufSize, &ValueInfoSize);
		}

		if(NtStatus != STATUS_SUCCESS) {
			if(NtStatus == STATUS_NO_MORE_ENTRIES)
				break;
			goto __cleanup;
		}

		// check value type
		if( (ValueInfo->Type != REG_SZ) && (ValueInfo->Type != REG_EXPAND_SZ) )
			continue;

		Temp = (char*)ValueInfo + ValueInfo->DataOffset;

		// make zero strings for value name and value
		if(!FsrMakeZeroString(&EntryName, &EntryNameMaxLen, ValueInfo->Name, ValueInfo->NameLength))
			goto __cleanup;

		if(!FsrMakeZeroString(&EntryValue, &EntryValueMaxLen, Temp, ValueInfo->DataLength))
			goto __cleanup;

		// ignore empty values
		if( (EntryName[0] == 0) || (EntryValue[0] == 0) )
			continue;

		// convert value name to lowercase and replace spaces to underlines
		wcslwr(EntryName);

		p = EntryName;
		while( (p = wcschr(p, L' ')) != NULL )
			*(p++) = L'_';

		// for type REG_SZ just add redirection
		if(ValueInfo->Type == REG_SZ) {
			FsrRedirectAdd(EntryValue, EntryName, 0);
		}

		// for type REG_EXPAND_SZ
		if(ValueInfo->Type == REG_EXPAND_SZ) {

			// expand value
			ExpandedPathLen = ExpandEnvironmentStrings(EntryValue, EntryExpandedPath, ExpandedPathMaxLen);
			if(ExpandedPathLen > ExpandedPathMaxLen) {
				ExpandedPathMaxLen = ExpandedPathLen;
				if((Temp = realloc(EntryExpandedPath, ExpandedPathMaxLen * sizeof(WCHAR))) == NULL)
					goto __cleanup;
				EntryExpandedPath = Temp;
				ExpandedPathLen = ExpandEnvironmentStrings(EntryValue, EntryExpandedPath, ExpandedPathMaxLen);
			}

			// add redirection for directory
			if((ExpandedPathLen != 0) && (ExpandedPathLen <= ExpandedPathMaxLen)) {
				FsrRedirectAdd(EntryExpandedPath, EntryName, 0);
			}

		}

	}

	Success = 1;

__cleanup:

	if(KeyHandle != NULL) {
		CloseHdl(KeyHandle);
	}

	free(EntryExpandedPath);
	free(EntryValue);
	free(EntryName);
	free(ValueInfo);
	free(KeyName);
	free(UserProfilePath);
	free(UserSidName);

	return Success;
}

// -------------------------------------------------------------------------------------------------

static FSR_STATUS FsrRedirectsInit()
{
	FSR_STATUS status;

	if((FsrData.RootNode = calloc(1, sizeof(FSR_NODE))) == NULL)
		return FSR_NOMEM;

	if((status = FsrRedirectAdd(FsrData.DataDir->Buf, L"data", 1)) != FSR_OK)
		return status;

	if((status = FsrRedirectsAddSystem()) != FSR_OK) {
		FsrNodeFree(FsrData.RootNode);
		return status;
	}

	FsrRedirectsAddUser();

	FsrRedirectPrint(FsrData.RootNode, 0);

	return FSR_OK;

}

// -------------------------------------------------------------------------------------------------

static int FsrImportApis()
{
	HMODULE ntdll;

	ntdll = GetModuleHandle(_T("ntdll.dll"));
	if(ntdll == NULL)
		return 0;

	OpenToken = (void*)GetProcAddress(ntdll, "NtOpenProcessToken");
	QueryToken = (void*)GetProcAddress(ntdll, "NtQueryInformationToken");
	FormatSid = (void*)GetProcAddress(ntdll, "RtlConvertSidToUnicodeString");
	FreeString = (void*)GetProcAddress(ntdll, "RtlFreeUnicodeString");
	if( (OpenToken == NULL ) || (QueryToken == NULL) || (FormatSid == NULL) || (FreeString == NULL) )
		return 0;

	OpenKey = (void*)GetProcAddress(ntdll, "NtOpenKey");
	CloseHdl = (void*)GetProcAddress(ntdll, "NtClose");
	QueryValue = (void*)GetProcAddress(ntdll, "NtQueryValueKey");
	EnumValue = (void*)GetProcAddress(ntdll, "NtEnumerateValueKey");
	if( (OpenKey == NULL) || (CloseHdl == NULL) || (QueryValue == NULL) || (EnumValue == NULL) )
		return 0;

	return 1;
}

// -------------------------------------------------------------------------------------------------

static R2I_WSTR *FsrGetProgramStartPath()
{
	R2I_WSTR *Buffer;
	DWORD Result;
	WCHAR *Temp, *CutPos;

	if((Buffer = WstrAlloc(MAX_PATH)) == NULL)
		return NULL;

	while(1) {

		Result = GetModuleFileName(NULL, Buffer->Buf, Buffer->BufLength);

		if((Result == 0) || (Result > Buffer->BufLength)) {
			free(Buffer);
			return NULL;
		}

		if(Result < Buffer->BufLength) {
			Buffer->Length = Result;
			break;
		}

		if(!WstrExpand(&Buffer, Buffer->BufLength * 3 / 2)) {
			free(Buffer);
			return NULL;
		}

	}

	// remove program filename name
	Temp = Buffer->Buf;
	CutPos = NULL;
	while((Temp = wcspbrk(Temp, L"\\/")) != NULL)
		CutPos = ++Temp;

	if(CutPos == NULL) {
		free(Buffer);
		return NULL;
	}

	Buffer->Length = CutPos - Buffer->Buf;
	*CutPos = 0;

	WstrCompact(&Buffer);

	return Buffer;
}

// -------------------------------------------------------------------------------------------------

void FsrSrchPthAddEntry(WCHAR *EntryPath)
{
	FSR_SEARCH_PATH_LIST *Entry, **pEntry;
	R2I_WSTR *SourcePath, *TargetPath;
	DWORD SrcPathSize, TgtPathSize;
	char *Cursor;

	// if path not redirected, return
	if(FsrRedirectPath(&TargetPath, EntryPath, WSTR_LENGTH_UNKNOWN, 0) != FSR_OK)
		return;
	if(TargetPath == NULL)
		return;

	// normalize original path
	if(FsrNormalizePath(&SourcePath, NULL, EntryPath, WSTR_LENGTH_UNKNOWN, 0) != FSR_OK) {
		free(TargetPath);
		return;
	}

	// if search entry exists, return
	for(pEntry = &(FsrData.SrchPthItems); *pEntry != NULL; pEntry = &((*pEntry)->next)) {
		if(wcsicmp((*pEntry)->source_name, SourcePath->Buf) == 0)
			return;
	}

	// allocate search entry
	SrcPathSize = (SourcePath->Length + 1) * sizeof(WCHAR);
	TgtPathSize = (TargetPath->Length + 1) * sizeof(WCHAR);

	if((Entry = malloc(sizeof(FSR_SEARCH_PATH_LIST) + SrcPathSize + TgtPathSize)) == NULL) {
		free(SourcePath);
		free(TargetPath);
		return;
	}

	Cursor = (char*)Entry + sizeof(FSR_SEARCH_PATH_LIST);

	// initialize search entry
	Entry->next = NULL;

	Entry->source_name = (void*)Cursor;
	Cursor += SrcPathSize;
	wcscpy(Entry->source_name, SourcePath->Buf);

	Entry->target_name = (void*)Cursor;
	Cursor += TgtPathSize;
	wcscpy(Entry->target_name, TargetPath->Buf);

	// add search entry to list back
	*pEntry = Entry;

	free(SourcePath);
	free(TargetPath);
}

// -------------------------------------------------------------------------------------------------

void FsrSrchPthAddEntries()
{
	WCHAR SystemDir[MAX_PATH], *PathBuffer;
	WCHAR *PathEntry, *NextPathEntry, *Temp;
	int IsQuoted = 0;

	// add systemroot to search entries
	if(GetSystemDirectory(SystemDir, MAX_PATH))
		FsrSrchPthAddEntry(SystemDir);

	// add windir to search entries
	if(GetWindowsDirectory(SystemDir, MAX_PATH))
		FsrSrchPthAddEntry(SystemDir);

	// get path variable
	if((PathBuffer = FsrGetEnv(L"PATH")) != NULL) {

		// loop by path variable items
		for(PathEntry = PathBuffer; PathEntry != NULL; PathEntry = NextPathEntry) {

			/*if((NextPathEntry = wcschr(PathEntry, L';')) != NULL)
				*(NextPathEntry++) = 0;*/

			// find next item
			NextPathEntry = NULL;
			for(Temp = PathEntry; *Temp != 0; Temp++) {
				if(*Temp == L'\"') IsQuoted = !IsQuoted;
				if((*Temp == L';') && (!IsQuoted)) {
					*Temp = 0;
					NextPathEntry = Temp + 1;
					break;
				}
			}

			// remove trailing and leading spaces
			while( (*PathEntry == L' ') || (*PathEntry == L'\t') )
				PathEntry++;

			Temp = PathEntry + wcslen(PathEntry);
			while( (Temp > PathEntry) && ((Temp[-1] == L' ') || (Temp[-1] == L'\t')) )
				Temp--;
			*Temp = 0;

			// remove quotes
			if(*PathEntry == L'\"') {
				PathEntry++;
				if((Temp = wcschr(PathEntry, L'\"')) != NULL)
					*Temp = 0;
			}

			// add item
			FsrSrchPthAddEntry(PathEntry);

		}

		free(PathBuffer);

	}

}

// -------------------------------------------------------------------------------------------------

FSR_STATUS FsrInitPathes(WCHAR *DataDir)
{
	FSR_STATUS status;
	DWORD Result;

	if((status = FsrNormalizePath(&(FsrData.DataDir), NULL, DataDir, WSTR_LENGTH_UNKNOWN, 0)) != FSR_OK)
		return status;
	WstrCompact(&(FsrData.DataDir));

	if(!CreateDirectory(DataDir, NULL)) {
		if(GetLastError() != ERROR_ALREADY_EXISTS)
			return FSR_BADPATH;
	}

	FsrData.ProgramStartPath = FsrGetProgramStartPath();
	if(FsrData.ProgramStartPath == NULL)
		return FSR_NOMEM;

	if((FsrData.WinDir = WstrAlloc(MAX_PATH)) == NULL)
		return FSR_NOMEM;

	if((Result = GetWindowsDirectory(FsrData.WinDir->Buf, FsrData.WinDir->BufLength)) == 0)
		return FSR_ERROR;
	FsrData.WinDir->Length = Result;

	WstrCompact(&(FsrData.WinDir));

	return FSR_OK;
}

// -------------------------------------------------------------------------------------------------

FSR_STATUS FsrInit(WCHAR *DataDir)
{
	FSR_STATUS status;

	if(!FsrData.HooksEnabled) {

		memset(&FsrData, 0, sizeof(FsrData));

		if(!FsrImportApis()) {
			return FSR_PROCNOTFOUND;
		}

		if((status = FsrInitPathes(DataDir)) != FSR_OK)
			goto __error_exit;

		if((status = FsrRedirectsInit()) != FSR_OK)
			goto __error_exit;

		FsrSrchPthAddEntries();

		InitializeCriticalSection(&(FsrData.searches_lock));

		FsrData.HooksEnabled = 1;

	}

	return FSR_OK;

__error_exit:
	FsrCleanup();
	return status;
}

// -------------------------------------------------------------------------------------------------

void FsrCleanup()
{
	FSR_SEARCH *SearchData, *NextSearchData;
	FSR_SEARCH_PATH_LIST *SearchPathEntry, *NextSearchPathEntry;

	if(FsrData.HooksEnabled) {

		FsrData.HooksEnabled = 0;

		DeleteCriticalSection(&(FsrData.searches_lock));

		for(SearchData = FsrData.searches; SearchData != NULL; SearchData = NextSearchData) {
			NextSearchData = SearchData->next;
			while(SearchData->entrycnt > 0)
				free(SearchData->entry[--(SearchData->entrycnt)]);
			free(SearchData->entry);
			free(SearchData);
		}

		for(SearchPathEntry = FsrData.SrchPthItems; SearchPathEntry != NULL; SearchPathEntry = NextSearchPathEntry) {
			NextSearchPathEntry = SearchPathEntry->next;
			free(SearchPathEntry);
		}

		FsrNodeFree(FsrData.RootNode);

		free(FsrData.WinDir);
		free(FsrData.ProgramStartPath);
		free(FsrData.DataDir);

	}
}

// -------------------------------------------------------------------------------------------------
