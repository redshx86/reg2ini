// -------------------------------------------------------------------------------------------------

#include "ini.h"

// -------------------------------------------------------------------------------------------------

#define length(a) (sizeof(a) / sizeof(a[0]))

// -------------------------------------------------------------------------------------------------

static int IniNodeCompare(const void **pa, const void **pb);
static int IniValueCompare(const void **pa, const void **pb);

static INI_STATUS IniListAdd(INI_LIST *List, void *Item, int (*Compare)(const void*,const void*));
static INI_STATUS IniListReplace(INI_LIST *List, void *Item, void *ItemNew);
static INI_STATUS IniListRemove(INI_LIST *List, void *Item);
static void * IniListLookup(INI_LIST *List, void *TestItem, int (*Compare)(const void*,const void*));
static void IniListClear(INI_LIST *List, void (*DestroyItem)(void*));

static INI_STATUS IniNodeInit(INI_NODE **pNode, INI_NODE *ParentNode, WCHAR *Name);
static INI_STATUS IniValueInit(INI_VALUE **pValue, WCHAR *Name, INI_VALUE_TYPE Type, void *Data, ULONG DataSize);

static INI_STATUS IniNodeAdd(INI_NODE **pNode, INI_NODE *ParentNode, WCHAR *Name);
static void IniNodeClear(INI_NODE *Node);
static void IniNodeFree(INI_NODE *Node);
static void IniNodeMakeOpaque(INI_NODE *Node);

static void IniPrintValue(INI_VALUE *Value, ULONG level);

// -------------------------------------------------------------------------------------------------

static INI_DATA IniData;

// -------------------------------------------------------------------------------------------------

static WCHAR * IniValueTypeName[] = {
	L"deleted",		// INI_VALUE_DELETED
	L"notype",		// INI_VALUE_NOTYPE
	L"binary",		// INI_VALUE_BINARY
	L"dword",		// INI_VALUE_DWORD
	L"qword",		// INI_VALUE_QWORD
	L"string",		// INI_VALUE_STRING
	L"env",			// INI_VALUE_STRING_ENV
	L"multiple",	// INI_VALUE_STRING_MULTI
};

// -------------------------------------------------------------------------------------------------

static BYTE HexParseTable[] = {
	   0,    1,    2,    3,    4,    5,    6,    7,    8,    9, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF,   10,   11,   12,   13,   14,   15, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF,   10,   11,   12,   13,   14,   15,
};

// -------------------------------------------------------------------------------------------------

void IniPrint()
{
	IniPrintNode(&(IniData.RootNode), 0);
}

// -------------------------------------------------------------------------------------------------

void IniPrintNode(INI_NODE *Node, ULONG level)
{
	ULONG i;

	for(i = 0; i < level; ++i)
		printf("  ");

	printf("INI_NODE \"%S\"", Node->Name);

	printf(" Status = ");
	switch(Node->State) {
		case INI_NODE_DELETED: printf("INI_NODE_DELETED"); break;
		case INI_NODE_TRANSPARENT: printf("INI_NODE_TRANSPARENT"); break;
		case INI_NODE_OPAQUE: printf("INI_NODE_OPAQUE"); break;
	}

	puts("");

	for(i = 0; i < Node->Values.Count; ++i)
		IniPrintValue(Node->Values.Value[i], level + 1);

	for(i = 0; i < Node->Subnodes.Count; ++i)
		IniPrintNode(Node->Subnodes.Node[i], level + 1);
}

// -------------------------------------------------------------------------------------------------

INI_NODE *IniNodeLookup(INI_NODE *ParentNode, WCHAR *NodeName)
{
	INI_NODE DummyNode;

	DummyNode.Name = NodeName;
	return IniListLookup(&(ParentNode->Subnodes), &DummyNode, IniNodeCompare);
}

// -------------------------------------------------------------------------------------------------

INI_STATUS IniNodeCreate(INI_NODE **pNode, INI_NODE *ParentNode, WCHAR *NodeName)
{
	INI_NODE *Node;

	// find existing node
	if((Node = IniNodeLookup(ParentNode, NodeName)) != NULL) {
		if(Node->State == INI_NODE_DELETED) {
			Node->State = INI_NODE_OPAQUE;
			IniData.Unsaved = 1;
		}
		*pNode = Node;
		return INI_OK;
	}

	// create node
	return IniNodeAdd(pNode, ParentNode, NodeName);
}

// -------------------------------------------------------------------------------------------------

INI_STATUS IniNodeCreateByPath(INI_NODE **pNode, WCHAR *NodePath)
{
	INI_STATUS status;
	INI_NODE *Node = NULL, *ParentNode;
	WCHAR *PathCopy, *NodeName, *NextNodeName;

	// copy node path
	if((PathCopy = wcsdup(NodePath)) == NULL)
		return INI_NOMEM;

	// loop by path nodes
	for(NodeName = PathCopy; NodeName != NULL; NodeName = NextNodeName) {

		// find next node name, cut
		if((NextNodeName = wcschr(NodeName, L'\\')) != NULL)
			*(NextNodeName++) = 0;

		if(NodeName[0] != 0) {
			// find existing node
			ParentNode = (Node != NULL) ? Node : &(IniData.RootNode);
			if((Node = IniNodeLookup(ParentNode, NodeName)) != NULL) {
				if(Node->State == INI_NODE_DELETED) {
					Node->State = INI_NODE_OPAQUE;
					IniData.Unsaved = 1;
				}
			}
			// or create new node
			else {
				if((status = IniNodeAdd(&Node, ParentNode, NodeName)) != INI_OK) {
					free(PathCopy);
					return status;
				}
			}
		}
	}

	// return node
	if(pNode != NULL) {
		*pNode = Node;
	}

	free(PathCopy);
	return INI_OK;
}

// -------------------------------------------------------------------------------------------------

INI_STATUS IniNodeStateInfo(WCHAR *NodePath, int *pIsExists, int *pIsOpaque)
{
	INI_NODE *Node = NULL, *ParentNode;
	WCHAR *PathCopy, *NodeName, *NextNodeName;

	// copy node path
	if((PathCopy = wcsdup(NodePath)) == NULL)
		return INI_NOMEM;

	// loop by path nodes
	for(NodeName = PathCopy; NodeName != NULL; NodeName = NextNodeName) {

		// get next node, cut path at backslash
		if((NextNodeName = wcschr(NodeName, L'\\')) != NULL)
			*(NextNodeName++) = 0;

		// lookup node
		if(NodeName[0] != 0) {
			ParentNode = (Node != NULL) ? Node : &(IniData.RootNode);
			if((Node = IniNodeLookup(ParentNode, NodeName)) == NULL)
				break;
			if(Node->State == INI_NODE_DELETED)
				break;
		}
	}

	// node not found
	if(Node == NULL) {
		*pIsExists = 0;
		*pIsOpaque = 0;
	} else {
		// node deleted
		if(Node->State == INI_NODE_DELETED) {
			*pIsExists = 0;
			*pIsOpaque = 1;
		}
		// node opaque
		else if(Node->State == INI_NODE_OPAQUE) {
			*pIsExists = 1;
			*pIsOpaque = 1;
		}
		// node transparent
		else {
			*pIsExists = 1;
			*pIsOpaque = 0;
		}
	}

	free(PathCopy);
	return INI_OK;
}

// -------------------------------------------------------------------------------------------------

static INI_STATUS IniNodeAdd(INI_NODE **pNode, INI_NODE *ParentNode, WCHAR *Name)
{
	INI_STATUS status;
	INI_NODE *Node;

	// initialize new node
	if((status = IniNodeInit(&Node, ParentNode, Name)) != INI_OK)
		return status;

	// insert node to parent's subnode list
	if((status = IniListAdd(&(ParentNode->Subnodes), Node, IniNodeCompare)) != INI_OK) {
		free(Node);
		return status;
	}

	*pNode = Node;

	IniData.Unsaved = 1;
	return INI_OK;
}

// -------------------------------------------------------------------------------------------------

INI_STATUS IniNodeDelete(INI_NODE *Node)
{
	INI_STATUS status;
	INI_NODE *ParentNode;

	// root node cannot be deleted
	if((ParentNode = Node->ParentNode) == NULL)
		return INI_BADCALL;

	// parent node opaque, destroy node
	if(ParentNode->State == INI_NODE_OPAQUE) {
		if((status = IniListRemove(&(ParentNode->Subnodes), Node)) != INI_OK)
			return status;
		IniNodeFree(Node);
	}
	// parent node transparent,
	// delete content and mark node as deleted
	else {
		IniNodeClear(Node);
		Node->State = INI_NODE_DELETED;
	}

	// set unsaved
	IniData.Unsaved = 1;
	return INI_OK;
}

// -------------------------------------------------------------------------------------------------

INI_STATUS IniNodeCopy(INI_NODE *DstNode, INI_NODE *SrcNode)
{
	INI_STATUS status;
	ULONG index;
	INI_NODE *SrcSubNode, *DstSubNode;
	INI_VALUE *SrcValue, *DstValue;

	for(index = 0; index < SrcNode->Subnodes.Count; ++index) {
		SrcSubNode = SrcNode->Subnodes.Node[index];
		DstSubNode = IniNodeLookup(DstNode, SrcSubNode->Name);
		if(DstSubNode == NULL) {
			if( (SrcSubNode->State != INI_NODE_DELETED) || (DstNode->State != INI_NODE_OPAQUE) ) {
				if((status = IniNodeAdd(&DstSubNode, DstNode, SrcSubNode->Name)) != INI_OK)
					return status;
				if(SrcSubNode->State != INI_NODE_TRANSPARENT)
					DstSubNode->State = SrcSubNode->State;
			}
		} else {
			if(SrcSubNode->State != DstSubNode->State)
				IniNodeMakeOpaque(DstSubNode);
		}
		if(SrcSubNode->State != INI_NODE_DELETED) {
			if((status = IniNodeCopy(DstSubNode, SrcSubNode)) != INI_OK)
				return status;
		}
	}

	for(index = 0; index < SrcNode->Values.Count; ++index) {
		SrcValue = SrcNode->Values.Value[index];
		DstValue = IniValueLookup(DstNode, SrcValue->Name);
		if( (SrcValue->Type != INI_VALUE_DELETED) || ((DstValue == NULL) && (DstNode->State == INI_NODE_TRANSPARENT)) ) {
			IniValueSetRaw(DstNode, SrcValue->Name, SrcValue->Type, SrcValue->Data, SrcValue->DataSize);
		}
	}

	return INI_OK;
}

// -------------------------------------------------------------------------------------------------

INI_STATUS IniValueSet(INI_NODE *Node, WCHAR *Name, INI_VALUE_TYPE Type, void *Data, ULONG DataSize)
{
	INI_STATUS status;
	ULONG dword, MaxStringLen, StringLen, StringSize;
	ULONGLONG qword;
	WCHAR *str;

	// notype or binary values sets raw
	if( (Type == INI_VALUE_NOTYPE) || (Type == INI_VALUE_BINARY) ) {
		return IniValueSetRaw(Node, Name, Type, Data, DataSize);
	}

	// dword value type
	if(Type == INI_VALUE_DWORD) {
		if(DataSize < sizeof(ULONG)) {
			dword = 0;
			memcpy(&dword, Data, DataSize);
			return IniValueSetRaw(Node, Name, Type, &dword, sizeof(ULONG));
		}
		return IniValueSetRaw(Node, Name, Type, Data, sizeof(ULONG));
	}

	// qword value type
	if(Type == INI_VALUE_QWORD) {
		if(DataSize < sizeof(ULONGLONG)) {
			qword = 0;
			memcpy(&dword, Data, DataSize);
			return IniValueSetRaw(Node, Name, Type, &qword, sizeof(ULONGLONG));
		}
		return IniValueSetRaw(Node, Name, Type, Data, sizeof(ULONGLONG));
	}

	// string value type
	if( (Type == INI_VALUE_STRING) || (Type == INI_VALUE_STRING_ENV) ) {

		// maximum chars
		MaxStringLen = DataSize / sizeof(WCHAR);

		// calculate real string length
		for(str = Data, StringLen = 0; (StringLen < MaxStringLen) && (str[StringLen] != 0); StringLen++);
		StringSize = (StringLen + 1) * sizeof(WCHAR);

		// string have zero terminator
		if( (StringLen > 0) && (str[StringLen] == 0) ) {
			return IniValueSetRaw(Node, Name, Type, Data, StringSize);
		}

		// string have no zero terminator, append it
		if((str = malloc(StringSize)) == NULL)
			return INI_NOMEM;
		memcpy(str, Data, StringLen * sizeof(WCHAR));
		str[StringLen] = 0;
		status = IniValueSetRaw(Node, Name, Type, str, StringSize);
		free(str);
		return status;
	}

	// multi string value type
	if(Type == INI_VALUE_STRING_MULTI) {

		// char count passed
		MaxStringLen = DataSize / sizeof(WCHAR);

		StringLen = 0;
		str = Data;

		while(StringLen < MaxStringLen) {

			// found final terminator, set value
			if(*str == 0) {
				StringSize = (StringLen + 1) * sizeof(WCHAR);
				return IniValueSetRaw(Node, Name, INI_VALUE_STRING_MULTI, Data, StringSize);
			}

			// calculate substring length
			while( (StringLen < MaxStringLen) && (*str != 0) ) {
				StringLen++;
				str++;
			}

			// no space for double terminator, return
			if(StringLen >= MaxStringLen - 1)
				break;

			StringLen++;
			str++;
		}

		// string unterminated, add double terminator
		StringSize = (StringLen + 2) * sizeof(WCHAR);
		if((str = malloc(StringSize)) == NULL)
			return INI_NOMEM;
		memcpy(str, Data, StringLen * sizeof(WCHAR));
		str[StringLen] = 0;
		str[StringLen + 1] = 0;
		status = IniValueSetRaw(Node, Name, INI_VALUE_STRING_MULTI, str, StringSize);
		free(str);
		return status;
	}

	// data type unknown
	return INI_BADTYPE;
}

// -------------------------------------------------------------------------------------------------

INI_VALUE *IniValueLookup(INI_NODE *Node, WCHAR *Name)
{
	INI_VALUE *Value, DummyValue;

	// empty value name same as default value
	if( (Name != NULL) && (*Name == 0) )
		Name = NULL;

	// find value with bsearch
	DummyValue.Name = Name;
	Value = IniListLookup(&(Node->Values), &DummyValue, IniValueCompare);

	return Value;
}

// -------------------------------------------------------------------------------------------------

INI_STATUS IniValueSetRaw(INI_NODE *Node, WCHAR *Name, INI_VALUE_TYPE Type, void *Data, ULONG DataSize)
{
	INI_STATUS status;
	INI_VALUE *Value, *ValueNew, ValueDummy;

	// empty value name same as default value
	if( (Name != NULL) && (*Name == 0) )
		Name = NULL;

	// find existing value
	ValueDummy.Name = Name;
	if((Value = IniListLookup(&(Node->Values), &ValueDummy, IniValueCompare)) != NULL) {
		// should reduce or expand data buffer
		if( (DataSize > Value->MaxDataSize) || (Value->MaxDataSize > DataSize + 32) ) {
			// init new value
			if((status = IniValueInit(&ValueNew, Name, Type, Data, DataSize)) != INI_OK)
				return status;
			// replace existing value
			if((status = IniListReplace(&(Node->Values), Value, ValueNew)) != INI_OK) {
				free(ValueNew);
				return status;
			}
			free(Value);
		}
		// use existing data buffer
		else {
			// mark value as touched
			Value->IsTouched = 1;
			// data not changed, return success
			if( (Type == Value->Type) && (DataSize == Value->DataSize) && (!memcmp(Value->Data, Data, DataSize)) )
				return INI_OK;
			// copy new data
			Value->Type = Type;
			Value->DataSize = DataSize;
			memcpy(Value->Data, Data, DataSize);
		}
	}
	// value not found
	else {
		// initialize value
		if((status = IniValueInit(&Value, Name, Type, Data, DataSize)) != INI_OK)
			return status;
		// add value to node's list
		if((status = IniListAdd(&(Node->Values), Value, IniValueCompare)) != INI_OK) {
			free(Value);
			return status;
		}
	}

	// set unsaved
	IniData.Unsaved = 1;
	return INI_OK;
}

// -------------------------------------------------------------------------------------------------

INI_STATUS IniValueDelete(INI_NODE *Node, WCHAR *Name)
{
	INI_STATUS status;
	INI_VALUE *Value, *ValueNew, DummyVal;

	// empty value name same as default value
	if( (Name != NULL) && (*Name == 0) ) {
		Name = NULL;
	}

	// find value
	DummyVal.Name = Name;
	if((Value = IniListLookup(&(Node->Values), &DummyVal, IniValueCompare)) == NULL)
		return INI_NOTFOUND;

	// node opaque, destroy value
	if(Node->State == INI_NODE_OPAQUE) {
		// remove value from node
		if((status = IniListRemove(&(Node->Values), Value)) != INI_OK)
			return status;
		// free value data
		free(Value);
		// set unsaved
		IniData.Unsaved = 1;
		return INI_OK;
	}

	// value have large data buffer, allocate new value with no data
	if(Value->DataSize >= 32) {
		// initialize new value
		if(IniValueInit(&ValueNew, Name, INI_VALUE_DELETED, NULL, 0) == INI_OK) {
			// replace existing value
			if(IniListReplace(&(Node->Values), Value, ValueNew) == INI_OK) {
				free(Value);
				// set unsaved and return
				IniData.Unsaved = 1;
				return INI_OK;
			}
			free(ValueNew);
		}
	}

	// mark value deleted
	Value->Type = INI_VALUE_DELETED;

	// set unsaved and return
	IniData.Unsaved = 1;
	return INI_OK;
}

// -------------------------------------------------------------------------------------------------

static void IniPrintValue(INI_VALUE *Value, ULONG level)
{
	ULONG i;
	WCHAR *str;

	for(i = 0; i < level; ++i)
		printf("  ");

	printf("INI_VALUE ");
	if(Value->Name != NULL) {
		printf("\"%S\"", Value->Name);
	} else {
		printf("(no name)");
	}

	printf(" Type = ");
	switch(Value->Type) {
		case INI_VALUE_DELETED: printf("INI_VALUE_DELETED"); break;
		case INI_VALUE_NOTYPE: printf("INI_VALUE_NOTYPE"); break;
		case INI_VALUE_BINARY: printf("INI_VALUE_BINARY"); break;
		case INI_VALUE_DWORD: printf("INI_VALUE_DWORD"); break;
		case INI_VALUE_QWORD: printf("INI_VALUE_QWORD"); break;
		case INI_VALUE_STRING: printf("INI_VALUE_STRING"); break;
		case INI_VALUE_STRING_ENV: printf("INI_VALUE_STRING_ENV"); break;
		case INI_VALUE_STRING_MULTI: printf("INI_VALUE_STRING_MULTI"); break;
	}

	printf(" ");

	switch(Value->Type) {
		case INI_VALUE_DELETED:
			break;
		case INI_VALUE_NOTYPE:
		case INI_VALUE_BINARY:
			for(i = 0; i < Value->DataSize; ++i) {
				if(i != 0) printf(",");
				printf("%02x", ((BYTE*)(Value->Data))[i]);
			}
			break;
		case INI_VALUE_DWORD:
			printf("%08x", *(ULONG*)(Value->Data));
			break;
		case INI_VALUE_QWORD:
			printf("%08x%08x", *((ULONG*)(Value->Data) + 1), *(ULONG*)(Value->Data));
			break;
		case INI_VALUE_STRING:
		case INI_VALUE_STRING_ENV:
			printf("\"%S\"", Value->Data);
			break;
		case INI_VALUE_STRING_MULTI:
			for(i = 0, str = Value->Data; *str; i++, str += wcslen(str) + 1) {
				if(i != 0) printf(",");
				printf("%S", str);
			}
			break;
	}

	printf(" (%u)\n", Value->DataSize);
}

// -------------------------------------------------------------------------------------------------

static void IniNodeMakeOpaque(INI_NODE *Node)
{
	INI_VALUE *Value;
	INI_NODE *SubNode;
	LONG i;

	if(Node->State == INI_NODE_OPAQUE)
		return;

	// loop by subnodes
	for(i = (LONG)(Node->Subnodes.Count) - 1; i >= 0; i--) {
		SubNode = Node->Subnodes.Node[i];
		// remove subnodes marked as deleted
		if(SubNode->State == INI_NODE_DELETED) {
			if(IniListRemove(&(Node->Subnodes), SubNode) == INI_OK)
				IniNodeFree(SubNode);
		}
		// do same for subnodes
		else if(SubNode->State == INI_NODE_TRANSPARENT) {
			IniNodeMakeOpaque(Node);
		}
	}

	// loop by values, remove values marked as deleted
	for(i = (LONG)(Node->Values.Count) - 1; i >= 0; i--) {
		Value = Node->Values.Value[i];
		if(Value->Type == INI_VALUE_DELETED) {
			if(IniListRemove(&(Node->Values), Value) == INI_OK)
				free(Value);
		}
	}

	// mark node opaque
	Node->State = INI_NODE_OPAQUE;

	// set unsaved
	IniData.Unsaved = 1;
}

// -------------------------------------------------------------------------------------------------

static INI_STATUS IniNodeInit(INI_NODE **pNode, INI_NODE *ParentNode, WCHAR *Name)
{
	ULONG Size, NameSize;
	INI_NODE *Node;
	char *Cursor;

	// calculate node size
	NameSize = (wcslen(Name) + 1) * sizeof(WCHAR);
	Size = sizeof(INI_NODE) + NameSize;

	// allocat node buffer
	if((Node = malloc(Size)) == NULL)
		return INI_NOMEM;

	Cursor = (void*)Node;
	Cursor += sizeof(INI_NODE);

	// set node fields
	Node->ParentNode = ParentNode;
	Node->State = ParentNode->State;
	Node->Name = (void*)Cursor;
	Cursor += NameSize;
	Node->IsTouched = 1;
	memset(&(Node->Subnodes), 0, sizeof(INI_LIST));
	memset(&(Node->Values), 0, sizeof(INI_LIST));

	memcpy(Node->Name, Name, NameSize);

	*pNode = Node;

	return INI_OK;
}

// -------------------------------------------------------------------------------------------------

static INI_STATUS IniValueInit(INI_VALUE **pValue, WCHAR *Name, INI_VALUE_TYPE Type, void *Data, ULONG DataSize)
{
	ULONG Size, NameSize, MaxDataSize;
	INI_VALUE *Value;
	char *Cursor;

	// calculate value buffer size
	NameSize = (Name != NULL) ? ((wcslen(Name) + 1) * sizeof(WCHAR)) : 0;
	MaxDataSize = (DataSize + 7) & ~7;

	Size = sizeof(INI_VALUE) + NameSize + MaxDataSize;

	// allocate value buffer
	if((Value = malloc(Size)) == NULL)
		return INI_NOMEM;

	Cursor = (void*)Value;
	Cursor += sizeof(INI_VALUE);

	// initialize value
	memset(Value, 0, sizeof(INI_VALUE));
	Value->Type = Type;
	if(Name != NULL) {
		Value->Name = (void*)Cursor;
		Cursor += NameSize;
		memcpy(Value->Name, Name, NameSize);
	} else {
		Value->Name = NULL;
	}
	Value->MaxDataSize = MaxDataSize;
	Value->DataSize = DataSize;
	Value->Data = (void*)Cursor;
	Cursor += MaxDataSize;
	memcpy(Value->Data, Data, DataSize);
	Value->IsTouched = 1;

	*pValue = Value;

	return INI_OK;
}

// -------------------------------------------------------------------------------------------------

static void IniNodeFree(INI_NODE *Node)
{
	// remove subnodes and values
	IniNodeClear(Node);
	// free node buffer
	free(Node);
}

// -------------------------------------------------------------------------------------------------

static void IniNodeClear(INI_NODE *Node)
{
	// remove subnodes
	IniListClear(&(Node->Subnodes), IniNodeFree);
	// remove values
	IniListClear(&(Node->Values), free);
}

// -------------------------------------------------------------------------------------------------

static INI_STATUS IniListAdd(INI_LIST *List, void *Item, int (*Compare)(const void*,const void*))
{
	ULONG Index, MaxCountNew;
	void *ItemsNew;
	int CompareResult;

	// list buffer full, allocate new slots
	if(List->Count == List->MaxCount) {
		MaxCountNew = (List->Count * 3 / 2) + 1;
		if((ItemsNew = realloc(List->Item, MaxCountNew * sizeof(void*))) == NULL)
			return INI_NOMEM;
		List->MaxCount = MaxCountNew;
		List->Item = ItemsNew;
	}

	// find insert position
	for(Index = 0; Index < List->Count; Index++) {
		CompareResult = Compare(List->Item + Index, &Item);
		if(CompareResult > 0)
			break;
		if(CompareResult == 0)
			return INI_FOUND;
	}

	// move items
	memmove(List->Item + Index + 1, List->Item + Index,
		(List->Count - Index) * sizeof(void*));
	List->Count++;

	// insert item
	List->Item[Index] = Item;

	return INI_OK;
}

// -------------------------------------------------------------------------------------------------

static INI_STATUS IniListReplace(INI_LIST *List, void *Item, void *ItemNew)
{
	ULONG Index;

	// find existing item position
	for(Index = 0; Index < List->Count; ++Index) {
		if(Item == List->Item[Index])
			break;
	}
	if(Index == List->Count)
		return INI_NOTFOUND;

	// remove operation, move items
	if(ItemNew == NULL) {
		memmove(List->Item + Index, List->Item + Index + 1,
			(List->Count - Index - 1) * sizeof(void*));
		List->Count--;
	}
	// replace item
	else {
		List->Item[Index] = ItemNew;
	}

	return INI_OK;
}

// -------------------------------------------------------------------------------------------------

static INI_STATUS IniListRemove(INI_LIST *List, void *Item)
{
	return IniListReplace(List, Item, NULL);
}

// -------------------------------------------------------------------------------------------------

static void * IniListLookup(INI_LIST *List, void *TestItem, int (*Compare)(const void*,const void*))
{
	void **pItem;

	if((pItem = bsearch(&TestItem, List->Item, List->Count, sizeof(void*), Compare)) == NULL)
		return NULL;
	return (*pItem);
}

// -------------------------------------------------------------------------------------------------

static void IniListClear(INI_LIST *List, void (*DestroyItem)(void*))
{
	ULONG i;

	for(i = 0; i < List->Count; ++i)
		DestroyItem(List->Item[i]);

	free(List->Item);

	List->MaxCount = 0;
	List->Count = 0;
	List->Item = NULL;
}

// -------------------------------------------------------------------------------------------------

static int IniNodeCompare(const void **pa, const void **pb)
{
	const INI_NODE *a = *pa, *b = *pb;
	return wcsicmp(a->Name, b->Name);
}

// -------------------------------------------------------------------------------------------------

static int IniValueCompare(const void **pa, const void **pb)
{
	const INI_VALUE *a = *pa, *b = *pb;
	if( (a->Name == NULL) && (b->Name == NULL) ) return 0;
	if(a->Name == NULL) return -1;
	if(b->Name == NULL) return 1;
	return wcsicmp(a->Name, b->Name);
}

// -------------------------------------------------------------------------------------------------

static void IniSaveFlush()
{
	DWORD WriteBytes, dw;

	WriteBytes = IniData.BufUsed * sizeof(WCHAR);
	WriteFile(IniData.hFile, IniData.Buffer, WriteBytes, &dw, NULL);
	IniData.BufUsed = 0;
}

// -------------------------------------------------------------------------------------------------

static void IniSaveChar(WCHAR c)
{
	// flush buffer if full
	if(IniData.BufUsed == IniData.BufLen)
		IniSaveFlush();
	// append char
	IniData.Buffer[IniData.BufUsed++] = c;
}

// -------------------------------------------------------------------------------------------------

static void IniSaveString(WCHAR *str)
{
	DWORD len, blocklen;

	len = wcslen(str);

	while(len != 0) {
		// flush buffer if full
		if(IniData.BufUsed == IniData.BufLen)
			IniSaveFlush();
		// copy block
		blocklen = IniData.BufLen - IniData.BufUsed;
		if(len < blocklen)
			blocklen = len;
		memcpy(IniData.Buffer + IniData.BufUsed, str, blocklen * sizeof(WCHAR));
		// move pointers
		IniData.BufUsed += blocklen;
		str += blocklen;
		len -= blocklen;
	}
}

// -------------------------------------------------------------------------------------------------

static void IniSaveByte(BYTE byte)
{
	WCHAR *Hex = L"0123456789abcdef";
	IniSaveChar(Hex[byte >> 4]);
	IniSaveChar(Hex[byte & 0x0F]);
}

// -------------------------------------------------------------------------------------------------

static void IniSaveCharEscaped(WCHAR c)
{
	// escape chars below 0x20 and escape char itself
	if( (c < 32) || (c == L'`') ) {
		// save escape char
		IniSaveChar(L'`');
		// save chars below 0x20
		if(c < 32) {
			if(c == L'\t') IniSaveChar(L't');
			else if(c == L'\n') IniSaveChar(L'n');
			else if(c == L'\r') IniSaveChar(L'r');
			else IniSaveByte((BYTE)c);
			return;
		}
	}
	// save char
	IniSaveChar(c);
}

// -------------------------------------------------------------------------------------------------

static void IniSaveStringEscaped(WCHAR *str)
{
	while(*str != 0) {
		IniSaveCharEscaped(*str);
		str++;
	}
}

// -------------------------------------------------------------------------------------------------

static void IniSaveStringQuoted(WCHAR *str)
{
	ULONG row = 60;

	// save quote
	IniSaveChar(L'\"');
	// save string
	while(*str != 0) {
		// newline after 60/80 chars
		if(row == 0) {
			row = 80;
			IniSaveString(L"\"\\\r\n  \"");
		}
		// double quotes
		if(*str == L'\"') IniSaveChar(L'\"');
		// save char
		IniSaveCharEscaped(*str);
		// move pointer
		str++;
		// count chars
		row--;
	}
	// save quote
	IniSaveChar(L'\"');
}

// -------------------------------------------------------------------------------------------------

static void IniSaveBinary(BYTE *Data, ULONG DataSize)
{
	ULONG i, row = 30;

	for(i = 0; i < DataSize; ++i) {
		// separate bytes with comma
		if(i != 0) {
			IniSaveChar(L',');
		}
		// newline after 30/40 bytes
		if(row == 0) {
			row = 40;
			IniSaveString(L"\\\r\n  ");
		}
		// save byte
		IniSaveByte(Data[i]);
		// count chars
		row--;
	}
}

// -------------------------------------------------------------------------------------------------

static void IniSaveDword(void *ValueData)
{
	WCHAR buf[16];

	// save dword as decimal
	_ltow(*(LONG*)ValueData, buf, 10);
	IniSaveString(buf);
}

// -------------------------------------------------------------------------------------------------

static void IniSaveQword(void *ValueData)
{
	WCHAR buf[32];

	// save qword as decimal
	_i64tow(*(LONGLONG*)ValueData, buf, 10);
	IniSaveString(buf);
}

// -------------------------------------------------------------------------------------------------

static void IniSaveMultiString(WCHAR *str)
{
	int i;
	LONG row = 60, len;

	// loop by substrings
	for(i = 0; *str != 0; i++, str += len + 1) {
		// substring length
		len = wcslen(str);
		// separate substrings with comma
		if(i != 0) IniSaveChar(L',');
		// newline after 60/80 chars
		if(row <= 0) {
			row = 80;
			IniSaveString(L"\\\r\n  ");
		}
		// count chars
		row -= len;
		// save substring
		IniSaveStringQuoted(str);
	}
}

// -------------------------------------------------------------------------------------------------

static void IniSaveNodeHeader(INI_NODE *Node, int FirstCall)
{
	INI_NODE *ParentNode;
	int OpaqueRoot;
	WCHAR lbkt, rbkt;

	ParentNode = Node->ParentNode;

	// save left bracket
	if(FirstCall) {
		OpaqueRoot = (Node->State == INI_NODE_OPAQUE) &&
			(ParentNode->State != INI_NODE_OPAQUE);
		if(Node->State == INI_NODE_DELETED) { lbkt = L'<'; rbkt = L'>'; }
		else if(OpaqueRoot) { lbkt = L'{'; rbkt = L'}'; }
		else { lbkt = L'['; rbkt = L']'; }
		IniSaveChar(lbkt);
	}

	// save previous node names
	if(ParentNode->ParentNode != NULL) {
		IniSaveNodeHeader(ParentNode, 0);
	}

	// save node name
	IniSaveStringEscaped(Node->Name);

	// save separator or right bracket
	if(FirstCall) {
		IniSaveChar(rbkt);
		IniSaveString(L"\r\n");
	} else {
		IniSaveChar(L'\\');
	}
}

// -------------------------------------------------------------------------------------------------

static void IniSaveValue(INI_VALUE *Value)
{
	// save value name
	if(Value->Name != NULL) {
		IniSaveStringQuoted(Value->Name);
	} else {
		IniSaveChar(L'@');
	}

	// save equals mark
	IniSaveString(L" = ");

	// save value type
	IniSaveString(IniValueTypeName[Value->Type]);

	// save value data
	if(Value->Type != INI_VALUE_DELETED) {
		IniSaveChar(L':');
		switch(Value->Type) {
			case INI_VALUE_NOTYPE:
			case INI_VALUE_BINARY:
				IniSaveBinary(Value->Data, Value->DataSize);
				break;
			case INI_VALUE_DWORD:
				IniSaveDword(Value->Data);
				break;
			case INI_VALUE_QWORD:
				IniSaveQword(Value->Data);
				break;
			case INI_VALUE_STRING:
			case INI_VALUE_STRING_ENV:
				IniSaveStringQuoted(Value->Data);
				break;
			case INI_VALUE_STRING_MULTI:
				IniSaveMultiString(Value->Data);
				break;
		}
	}

	// newline
	IniSaveString(L"\r\n");
}

// -------------------------------------------------------------------------------------------------

static void IniSaveNode(INI_NODE *Node)
{
	ULONG i;
	int OpaqueRoot;

	OpaqueRoot = (Node->State == INI_NODE_OPAQUE) &&
		(Node->ParentNode->State != INI_NODE_OPAQUE);

	// save node if have values, or marks opaque tree,
	// or have not subnodes (to indicate this node exists)
	if( (Node->Subnodes.Count == 0) || (Node->Values.Count != 0) || OpaqueRoot ) {
		// save title string
		IniSaveNodeHeader(Node, 1);
		// save values
		for(i = 0; i < Node->Values.Count; ++i) {
			IniSaveValue(Node->Values.Value[i]);
		}
		// newline
		IniSaveString(L"\r\n");
	}

	// save subnodes
	for(i = 0; i < Node->Subnodes.Count; ++i) {
		IniSaveNode(Node->Subnodes.Node[i]);
	}
}

// -------------------------------------------------------------------------------------------------

INI_STATUS IniSave()
{
	ULONG BufLen, i;
	OVERLAPPED ovl;

	// if already saved, return
	if(!IniData.Unsaved)
		return INI_OK;

	// allocate buffer (256 chars at least)
	BufLen = 256;
	if(IniData.BufLen < BufLen) {
		if((IniData.Buffer = realloc(IniData.Buffer, BufLen * sizeof(WCHAR))) == NULL)
			return INI_NOMEM;
		IniData.BufLen = BufLen;
	}

	// locak file
	memset(&ovl, 0, sizeof(ovl));
	if(!LockFileEx(IniData.hFile, LOCKFILE_EXCLUSIVE_LOCK, 0, INI_LOCK_LEN, 0, &ovl))
		return INI_ERROR;

	// set pointer to start
	SetFilePointer(IniData.hFile, 0, NULL, FILE_BEGIN);

	// save unicode BOM
	IniSaveChar(L'\xFEFF');

	// save nodes
	for(i = 0; i < IniData.RootNode.Subnodes.Count; ++i) {
		IniSaveNode(IniData.RootNode.Subnodes.Node[i]);
	}

	// flush buffer
	IniSaveFlush();

	// set EOF
	SetEndOfFile(IniData.hFile);

	// set last modified time
	GetFileTime(IniData.hFile, NULL, NULL, &(IniData.LastSaved));

	// unlock file
	UnlockFileEx(IniData.hFile, 0, INI_LOCK_LEN, 0, &ovl);

	// set saved
	IniData.Unsaved = 0;

	return INI_OK;
}

// -------------------------------------------------------------------------------------------------

static void IniSkipWhitespace(WCHAR **pstr)
{
	WCHAR *str = *pstr;

	// move pointer to first non-space char
	while( (*str == L' ') || (*str == L'\t') )
		str++;
	*pstr = str;
}

// -------------------------------------------------------------------------------------------------

static void IniCutWhitespaceTail(WCHAR *str, WCHAR *end)
{
	WCHAR *p;

	// replace trailing space chars with zero
	for(p = end - 1; p >= str; p--) {
		if( (*p != ' ') && (*p != '\t') )
			break;
		*p = 0;
	}
}

// -------------------------------------------------------------------------------------------------

static WCHAR *IniTrimString(WCHAR *str)
{
	WCHAR *p;

	// move pointer to first non-space char
	while( (*str == L' ') || (*str == L'\t') )
		str++;

	// move p pointer to last char
	for(p = str; *p != 0; p++)
		;
	p--;

	// zero trailing whitespace chars
	while( (p >= str) && ((*p == L' ') || (*p == L'\t')) ) {
		*p = 0;
		p--;
	}

	// return pointer
	return str;
}

// -------------------------------------------------------------------------------------------------

static int IniParseByte(WCHAR *str, BYTE *pByte)
{
	unsigned int i, j;
	BYTE h, l;

	// align char codes to table start
	i = (unsigned int)(str[0]) - '0';
	j = (unsigned int)(str[1]) - '0';

	// check char codes vs table bound
	if( (i >= length(HexParseTable)) || (j >= length(HexParseTable)) )
		return 0;

	// get hex digit values
	h = HexParseTable[i];
	l = HexParseTable[j];

	// validate digts
	if( (h == 0xFF) || (l == 0xFF) )
		return 0;

	// return byte
	*pByte = ((h << 4) | l);
	return 1;
}

// -------------------------------------------------------------------------------------------------

static WCHAR IniParseCharEscaped(WCHAR **pBuf)
{
	BYTE b;
	WCHAR *buf = *pBuf, ch;

	ch = *(buf++);

	// check for escape char
	if(ch == L'`') {
		// check for hex byte
		if(IniParseByte(buf, &b)) {
			buf += 2;
			ch = b;
		}
		// check for special char
		// or use next char itself
		else {
			ch = *buf;
			if(ch != 0) {
				if(ch == L't') ch = L'\t';
				else if(ch == L'n') ch = L'\n';
				else if(ch == L'r') ch = L'\r';
				buf++;
			}
		}
	}

	// move pointer and return char
	*pBuf = buf;
	return ch;
}

// -------------------------------------------------------------------------------------------------

static void IniParseStringEscaped(WCHAR *str)
{
	WCHAR *dst = str;

	// parse chars
	while(*str != 0) {
		*(dst++) = IniParseCharEscaped(&str);
	}
	*dst = 0;
}

// -------------------------------------------------------------------------------------------------

static int IniParseStringQuoted(WCHAR *str)
{
	int quotes = 0;
	WCHAR *dst = str, *src = str;

	// skip whitespace
	IniSkipWhitespace(&str);

	// test string quoted
	if(*src == L'\"') {
		quotes = 1;
		src++;
	}

	// loop by chars
	while(*src != 0) {
		// if have quote
		if(*src == L'\"') {
			src++;
			// quotes restricted if whole string not quoted
			if(!quotes) return 0;
			// closing quote (not double quote!)
			if(*src != L'\"') {
				// skip whitespace after closing quote
				IniSkipWhitespace(&src);
				// string terminator
				if(*src == 0) {
					*dst = 0;
					return 1;
				}
				// re-opening quote
				if(*src == L'\"') {
					src++;
					continue;
				}
				// invalid char
				return 0;
			}
		}
		// copy char
		*(dst++) = IniParseCharEscaped(&src);
	}

	// string ended, if missing closing quote, return error
	if(quotes) return 0;

	// terminate destination string
	*dst = 0;

	// remove copied trailing whitespace
	IniCutWhitespaceTail(str, dst);

	return 1;
}

// -------------------------------------------------------------------------------------------------

static void IniParseNodeHeader(WCHAR *Line, INI_NODE **pCurrentNode)
{
	INI_NODE_STATE NodeNewState;
	WCHAR rbkt, *LastChar;
	WCHAR *NodeName, *NextNodeName;
	INI_NODE *Node = NULL, *ParentNode;

	*pCurrentNode = NULL;

	// check opening bracket
	switch(*Line) {
		case L'<': rbkt = L'>'; NodeNewState = INI_NODE_DELETED; break;
		case L'[': rbkt = L']'; NodeNewState = INI_NODE_TRANSPARENT; break;
		case L'{': rbkt = L'}'; NodeNewState = INI_NODE_OPAQUE; break;
		default: return;
	}

	// check closing bracket
	LastChar = Line + wcslen(Line) - 1;
	if(*LastChar != rbkt)
		return;

	// remove brackets
	Line++;
	*LastChar = 0;

	// find/create node
	for(NodeName = Line; NodeName != NULL; NodeName = NextNodeName) {

		// next node name
		if((NextNodeName = wcschr(NodeName, L'\\')) != NULL)
			*(NextNodeName++) = 0;

		// skip empty name
		if(*NodeName == 0)
			continue;

		ParentNode = (Node != NULL) ? Node : &(IniData.RootNode);

		// unescape component node name
		IniParseStringEscaped(NodeName);

		// lookup existing node
		if((Node = IniNodeLookup(ParentNode, NodeName)) != NULL) {

			// set node touched
			Node->IsTouched = 1;

			// current node deleted
			if(Node->State == INI_NODE_DELETED) {
				// if operation = delete, return
				if(NodeNewState == INI_NODE_DELETED) {
					Node = NULL;
					break;
				}
				// mark node as opaque now
				Node->State = INI_NODE_OPAQUE;
			}
		}

		// add new node
		else {
			if(IniNodeAdd(&Node, ParentNode, NodeName) != INI_OK)
				break;
		}
	}

	// node deleted, return
	if(Node == NULL)
		return;

	// operation = delete node
	if(NodeNewState == INI_NODE_DELETED) {
		IniNodeDelete(Node);
		return;
	}

	// operation = make node opaque
	if(NodeNewState == INI_NODE_OPAQUE) {
		IniNodeMakeOpaque(Node);
	}

	*pCurrentNode = Node;
}

// -------------------------------------------------------------------------------------------------

static void IniParseValueBinary(INI_NODE *CurrentNode, WCHAR *ValueName, INI_VALUE_TYPE ValueType, WCHAR *ValueData)
{
	ULONG DataSize = 0;
	BYTE *dst = (void*)ValueData, b;
	WCHAR *p;

	// trim whitespace
	ValueData = IniTrimString(ValueData);

	for(p = ValueData; *p != 0; p++) {
		// skip garbage chars
		while( (*p == L',') || (*p == L' ') || (*p == L'\t') )
			p++;
		if(*p == 0)
			break;
		// parse byte
		if(IniParseByte(p, &b)) {
			*(dst++) = b;
			DataSize++;
			p++;
		}
	}

	// set value
	IniValueSetRaw(CurrentNode, ValueName, ValueType, ValueData, DataSize);
}

// -------------------------------------------------------------------------------------------------

static void IniParseValueDword(INI_NODE *CurrentNode, WCHAR *ValueName, WCHAR *ValueData)
{
	LONG dword;

	// parse dword as decimal
	dword = _wtol(ValueData);
	IniValueSetRaw(CurrentNode, ValueName, INI_VALUE_DWORD, &dword, sizeof(LONG));
}

// -------------------------------------------------------------------------------------------------

static void IniParseValueQword(INI_NODE *CurrentNode, WCHAR *ValueName, WCHAR *ValueData)
{
	LONGLONG qword;

	// parse qword as decimal
	qword = _wtoi64(ValueData);
	IniValueSetRaw(CurrentNode, ValueName, INI_VALUE_QWORD, &qword, sizeof(LONGLONG));
}

// -------------------------------------------------------------------------------------------------

static void IniParseValueString(INI_NODE *CurrentNode, WCHAR *ValueName, INI_VALUE_TYPE ValueType, WCHAR *ValueData)
{
	ULONG DataSize;

	// remove quotes, unescape string
	if(!IniParseStringQuoted(ValueData))
		return;

	// set value
	DataSize = (wcslen(ValueData) + 1) * sizeof(WCHAR);
	IniValueSetRaw(CurrentNode, ValueName, ValueType, ValueData, DataSize);
}

// -------------------------------------------------------------------------------------------------

static void IniParseValueMultiString(INI_NODE *CurrentNode, WCHAR *ValueName, WCHAR *ValueData)
{
	int quotes = 0;
	WCHAR *Str, *Dst, *DstElem;
	ULONG DataSize;

	// eg "value 1",value2,value3

	Str = ValueData;
	Dst = ValueData;

	// skip whitespace
	IniSkipWhitespace(&Str);

	while(*Str != 0) {

		quotes = 0;

		// dest buffer pointer to substring start
		DstElem = Dst;

		// test substring quoted
		if(*Str == L'\"') {
			quotes = 1;
			Str++;
		}

		// copy substring
		while(*Str != 0) {
			// have quote
			if(*Str == L'\"') {
				Str++;
				// quotes restricted if substring not quoted
				if(!quotes) return;
				// closing quote (not double quote)
				if(*Str != L'\"') {
					// skip whitesapace after closing quote
					IniSkipWhitespace(&Str);
					// re-opening quote
					if(*Str == L'\"') {
						Str++;
						continue;
					}
					// substring terminator
					if((*Str == L',') || (*Str == 0))
						break;
					// all other chars invalid
					return;
				}
			}
			// comma is substring terminator if string not quoted
			if( (!quotes) && (*Str == L',') )
				break;
			// copy char
			*(Dst++) = IniParseCharEscaped(&Str);
		}

		// current char can be comma or terminating zero
		// snap to next substring
		if(*Str == ',') {
			Str++;
			IniSkipWhitespace(&Str);
		}

		// trim trailing whitespace if substring not quoted
		if(!quotes) {
			while( (Dst > DstElem) && ((Dst[-1] == L' ') || (Dst[-1] == L'\t')) ) Dst--;
		}

		// terminate substring (empty strings not allowed in multistring values, ignore them)
		if(Dst != DstElem)
			*(Dst++) = 0;
	}

	// add final terminator
	*(Dst++) = 0;

	// set value
	DataSize = (Dst - ValueData) * sizeof(WCHAR);
	IniValueSetRaw(CurrentNode, ValueName, INI_VALUE_STRING_MULTI, ValueData, DataSize);
}

// -------------------------------------------------------------------------------------------------

static void IniParseValue(WCHAR *Line, INI_NODE *CurrentNode)
{
	int quoted = 0;
	WCHAR *p, *EqSignPos;
	WCHAR *ValueName, *ValueTypeName, *ValueData;
	INI_VALUE_TYPE ValueType = 0;
	ULONG i;

	// find equals sign
	EqSignPos = NULL;
	for(p = Line; *p != 0; p++) {
		if(*p == L'\"') quoted = !quoted;
		if((*p == L'=') && (!quoted)) {
			EqSignPos = p;
			break;
		}
	}
	if(EqSignPos == NULL)
		return;

	// value name ends with equals sign
	ValueName = Line;
	*EqSignPos = 0;

	IniSkipWhitespace(&ValueName);
	IniCutWhitespaceTail(ValueName, EqSignPos);

	// value type name after equals sign position
	ValueTypeName = EqSignPos + 1;
	IniSkipWhitespace(&ValueTypeName);

	// data separated from type with colon
	if((ValueData = wcschr(ValueTypeName, L':')) != NULL) {
		*ValueData = 0;
		IniCutWhitespaceTail(ValueTypeName, ValueData);
		ValueData++;
		IniSkipWhitespace(&ValueData);
	} else {
		IniTrimString(ValueTypeName);
	}

	// @ is default value name
	if( (ValueName[0] == L'@') && (ValueName[1] == 0) ) {
		ValueName = NULL;
	} else {
		// unquote and unescape value name
		if(!IniParseStringQuoted(ValueName))
			return;
	}

	// lookup value type
	for(i = 0; i < INI_VALUE_TYPE_COUNT; ++i) {
		if(wcsicmp(ValueTypeName, IniValueTypeName[i]) == 0) {
			ValueType = i;
			break;
		}
	}
	if(i == INI_VALUE_TYPE_COUNT)
		return;

	// only deleted value type can have no data
	if( (ValueType != INI_VALUE_DELETED) && (ValueData == NULL) )
		return;

	// opaque nodes have no deleted values
	if( (ValueType == INI_VALUE_DELETED) && (CurrentNode->State == INI_NODE_OPAQUE) )
		return;

	// parse data
	switch(ValueType) {
		case INI_VALUE_DELETED:
			IniValueSetRaw(CurrentNode, ValueName, INI_VALUE_DELETED, NULL, 0);
			break;
		case INI_VALUE_NOTYPE:
		case INI_VALUE_BINARY:
			IniParseValueBinary(CurrentNode, ValueName, ValueType, ValueData);
			break;
		case INI_VALUE_DWORD:
			IniParseValueDword(CurrentNode, ValueName, ValueData);
			break;
		case INI_VALUE_QWORD:
			IniParseValueQword(CurrentNode, ValueName, ValueData);
			break;
		case INI_VALUE_STRING:
		case INI_VALUE_STRING_ENV:
			IniParseValueString(CurrentNode, ValueName, ValueType, ValueData);
			break;
		case INI_VALUE_STRING_MULTI:
			IniParseValueMultiString(CurrentNode, ValueName, ValueData);
			break;
	}
}

// -------------------------------------------------------------------------------------------------

static void IniParseLine(WCHAR *Line, INI_NODE **pCurrentNode)
{
	// skip leading whitespace
	IniSkipWhitespace(&Line);

	// ignore comments and empty lines
	if( (Line[0] == L';') || (Line[0] == 0) )
		return;

	// node title begins with brackets
	if( (Line[0] == L'[') || (Line[0] == L'{') || (Line[0] == L'<') ) {
		Line = IniTrimString(Line);
		IniParseNodeHeader(Line, pCurrentNode);
	}
	// try parse line as value
	else if(*pCurrentNode != NULL) {
		IniParseValue(Line, *pCurrentNode);
	}
}

// -------------------------------------------------------------------------------------------------

static void IniNodeMarkTreeForDelete(INI_NODE *Node)
{
	ULONG i;

	// mark subnodes as untouched
	for(i = 0; i < Node->Subnodes.Count; ++i)
		IniNodeMarkTreeForDelete(Node->Subnodes.Node[i]);

	// mark values as untouched
	for(i = 0; i < Node->Values.Count; ++i)
		Node->Values.Value[i]->IsTouched = 0;

	// mark node as untouched (except root node)
	if(Node->ParentNode != NULL)
		Node->IsTouched = 0;
}

// -------------------------------------------------------------------------------------------------

static void IniNodeDeleteMarked(INI_NODE *Node)
{
	INI_NODE *ParentNode;
	INI_VALUE *Value;
	LONG i;

	// node untouched (not loaded, delete it)
	if(!Node->IsTouched) {
		// root node cannot be deleted
		if((ParentNode = Node->ParentNode) == NULL)
			return;
		// remove node
		if(IniListRemove(&(ParentNode->Subnodes), Node) == INI_OK)
			IniNodeFree(Node);
		return;
	}

	// process subnodes
	for(i = (LONG)(Node->Subnodes.Count) - 1; i >= 0; i--) {
		IniNodeDeleteMarked(Node->Subnodes.Node[i]);
	}

	// remove untouched values
	for(i = (LONG)(Node->Values.Count) - 1; i >= 0; i--) {
		Value = Node->Values.Value[i];
		if(!Value->IsTouched) {
			if(IniListRemove(&(Node->Values), Value) == INI_OK)
				free(Value);
		}
	}
}

// -------------------------------------------------------------------------------------------------

INI_STATUS IniLoad()
{
	INI_NODE *CurrentNode = NULL;
	int FirstChar = 1, IgnoreEndline = 0;
	WCHAR Buf[256];
	WCHAR c, *Line = NULL, *LineTmp;
	ULONG LineMaxLen, LineLen;
	FILETIME LastWrite;
	OVERLAPPED ovl;
	DWORD CharsRead, i;

	// return if file not changed
	if(GetFileTime(IniData.hFile, NULL, NULL, &LastWrite)) {
		if(memcmp(&LastWrite, &(IniData.LastSaved), sizeof(FILETIME)) == 0)
			return INI_OK;
	}

	// lock file
	memset(&ovl, 0, sizeof(ovl));
	if(!LockFileEx(IniData.hFile, 0, 0, INI_LOCK_LEN, 0, &ovl))
		return INI_ERROR;

	// mark all data as untouched
	IniNodeMarkTreeForDelete(&(IniData.RootNode));

	// set file pointer to start
	SetFilePointer(IniData.hFile, 0, NULL, FILE_BEGIN);

	// use global buffer
	Line = IniData.Buffer;
	LineMaxLen = IniData.BufLen;
	LineLen = 0;

	while(1) {

		// read data
		ReadFile(IniData.hFile, Buf, sizeof(Buf), &CharsRead, NULL);

		CharsRead /= sizeof(WCHAR);
		if(CharsRead == 0)
			break;

		// loop by chars
		for(i = 0; i < CharsRead; ++i) {

			c = Buf[i];

			// ignore '\r' and BOM chars
			if( (FirstChar && (c == L'\xFEFF')) || (c == L'\r') )
				continue;

			// endline
			if(c == L'\n') {
				// replace endline with space if previous char is backslash
				if(IgnoreEndline) {
					Line[LineLen - 1] = L' ';
					IgnoreEndline = 0;
				}
				// terminate buffer and parse line
				else if(LineLen != 0) {
					Line[LineLen] = 0;
					IniParseLine(Line, &CurrentNode);
					LineLen = 0;
				}
			}

			// copy char
			else {
				// expand buffer if full
				if(LineLen + 8 >= LineMaxLen) {
					LineMaxLen = (LineMaxLen * 3 / 2) + 64;
					if((LineTmp = realloc(Line, LineMaxLen * sizeof(WCHAR))) == NULL)
						break;
					Line = LineTmp;
					IniData.BufLen = LineMaxLen;
					IniData.Buffer = LineTmp;
				}
				// copy char
				Line[LineLen++] = c;
				// final '\\' escapes endline
				IgnoreEndline = (c == L'\\');
			}

			FirstChar = 0;

		}
	}

	// parse last string
	if(LineLen != 0) {
		Line[LineLen] = 0;
		IniParseLine(Line, &CurrentNode);
	}

	// unlock file
	UnlockFileEx(IniData.hFile, 0, INI_LOCK_LEN, 0, &ovl);

	// delete nodes still untouched
	IniNodeDeleteMarked(&(IniData.RootNode));

	// remember file time
	memcpy(&(IniData.LastSaved), &LastWrite, sizeof(FILETIME));

	// set saved
	IniData.Unsaved = 0;

	return INI_OK;
}

// -------------------------------------------------------------------------------------------------

INI_STATUS IniInitialize(WCHAR *Filename)
{
	memset(&IniData, 0, sizeof(IniData));

	// create file
	IniData.hFile = CreateFile(Filename, GENERIC_READ|GENERIC_WRITE,
		FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if(IniData.hFile == INVALID_HANDLE_VALUE)
		return INI_CANTOPEN;

	// initialize root node
	IniData.RootNode.State = INI_NODE_TRANSPARENT;
	IniData.RootNode.Name = L"Root";
	IniData.RootNode.IsTouched = 1;

	// load
	IniLoad();

	return INI_OK;
}

// -------------------------------------------------------------------------------------------------

void IniCleanup()
{
	// save
	IniSave();

	// close file
	CloseHandle(IniData.hFile);

	// free data
	IniNodeClear(&(IniData.RootNode));

	// free buffer
	free(IniData.Buffer);
}

// -------------------------------------------------------------------------------------------------

ULONGLONG IniGetLastSaved()
{
	ULONGLONG SaveTime;

	// return file time as ULONGLONG
	memcpy(&SaveTime, &(IniData.LastSaved), sizeof(ULONGLONG));
	return SaveTime;
}

// -------------------------------------------------------------------------------------------------
