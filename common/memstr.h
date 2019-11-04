// -------------------------------------------------------------------------------------------------

#pragma once
#include <malloc.h>
#include <windows.h>
#include <string.h>

// -------------------------------------------------------------------------------------------------

typedef struct __R2I_BUF {
	DWORD SizeMax;
	DWORD Size;
	void *Data;
} R2I_BUF;

// -------------------------------------------------------------------------------------------------

typedef struct __R2I_WSTR {
	DWORD BufLength;
	DWORD Length;
	WCHAR *Buf;
} R2I_WSTR;

// -------------------------------------------------------------------------------------------------

#define WSTR_LENGTH_UNKNOWN			0xffffffff
#define WSTR_END					0x7fffffff

// -------------------------------------------------------------------------------------------------

R2I_BUF *BufAlloc(DWORD Size, DWORD SizeMax);
R2I_BUF *BufAllocZero(DWORD Size, DWORD SizeMax);
int BufExpand(R2I_BUF **pBuf, DWORD SizeMax);

// -------------------------------------------------------------------------------------------------

R2I_WSTR *WstrAlloc(DWORD BufLength);
R2I_WSTR *WstrDup(WCHAR *Buf, DWORD Length, DWORD BufLength);
int WstrExpand(R2I_WSTR **pWstr, DWORD BufLength);
int WstrCompact(R2I_WSTR **pWstr);
int WstrSet(R2I_WSTR **pWstr, WCHAR *Buf, DWORD Length);
int WstrAppend(R2I_WSTR **pWstr, WCHAR *Buf, DWORD Length);
void WstrDelete(R2I_WSTR *Wstr, DWORD Start, DWORD Length);
int WstrInsert(R2I_WSTR **pWstr, DWORD Pos, WCHAR *Buf, DWORD Length);
WCHAR WstrLastChar(R2I_WSTR *Wstr);

// -------------------------------------------------------------------------------------------------

WCHAR *_wcsrpbrk(WCHAR *buf, WCHAR *cs);

// -------------------------------------------------------------------------------------------------
