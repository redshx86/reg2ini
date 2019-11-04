// -------------------------------------------------------------------------------------------------

#include "memstr.h"

// -------------------------------------------------------------------------------------------------

R2I_WSTR *WstrAlloc(DWORD BufLength)
{
	R2I_WSTR *Wstr;
	DWORD BufSize;

	if(BufLength == 0)
		BufLength++;
	
	BufSize = ((BufLength + 1) * sizeof(WCHAR) + sizeof(R2I_WSTR) + 15) & ~15;

	if((Wstr = malloc(BufSize)) == NULL)
		return NULL;

	Wstr->BufLength = (BufSize - sizeof(R2I_WSTR)) / sizeof(WCHAR) - 1;
	Wstr->Length = 0;
	Wstr->Buf = (void*)((BYTE*)Wstr + sizeof(R2I_WSTR));
	Wstr->Buf[0] = 0;

	return Wstr;
}

// -------------------------------------------------------------------------------------------------

R2I_WSTR *WstrDup(WCHAR *Buf, DWORD Length, DWORD BufLength)
{
	R2I_WSTR *Wstr;

	if(Length == WSTR_LENGTH_UNKNOWN)
		Length = wcslen(Buf);

	if(BufLength < Length)
		BufLength = Length;

	if((Wstr = WstrAlloc(BufLength)) == NULL)
		return NULL;

	memcpy(Wstr->Buf, Buf, Length * sizeof(WCHAR));
	Wstr->Buf[Length] = 0;
	Wstr->Length = Length;

	return Wstr;
}

// -------------------------------------------------------------------------------------------------

int WstrExpand(R2I_WSTR **pWstr, DWORD BufLength)
{
	DWORD BufSize;
	R2I_WSTR *Wstr = *pWstr;

	if(BufLength <= Wstr->BufLength)
		return 1;

	BufSize = ((BufLength + 1) * sizeof(WCHAR) + sizeof(R2I_WSTR) + 15) & ~15;

	if((Wstr = realloc(Wstr, BufSize)) == NULL)
		return 0;

	Wstr->BufLength = (BufSize - sizeof(R2I_WSTR)) / sizeof(WCHAR) - 1;
	Wstr->Buf = (void*)((BYTE*)Wstr + sizeof(R2I_WSTR));
	
	*pWstr = Wstr;
	return 1;
}

// -------------------------------------------------------------------------------------------------

int WstrCompact(R2I_WSTR **pWstr)
{
	R2I_WSTR *Wstr = *pWstr;
	DWORD BufSize;

	BufSize = (sizeof(R2I_WSTR) + (Wstr->Length + 1) * sizeof(WCHAR) + 15) & ~15;

	if((Wstr = realloc(Wstr, BufSize)) == NULL)
		return 0;

	Wstr->BufLength = (BufSize - sizeof(R2I_WSTR)) / sizeof(WCHAR) - 1;
	Wstr->Buf = (void*)((BYTE*)Wstr + sizeof(R2I_WSTR));

	*pWstr = Wstr;
	return 1;
}

// -------------------------------------------------------------------------------------------------

int WstrSet(R2I_WSTR **pWstr, WCHAR *Buf, DWORD Length)
{
	R2I_WSTR *Wstr = *pWstr;

	if(Length == WSTR_LENGTH_UNKNOWN)
		Length = wcslen(Buf);

	if(Length > Wstr->BufLength) {
		if(!WstrExpand(&Wstr, Length))
			return 0;
	}

	Wstr->Length = Length;
	memcpy(Wstr->Buf, Buf, Length * sizeof(WCHAR));
	Wstr->Buf[Length] = 0;

	*pWstr = Wstr;
	return 1;
}

// -------------------------------------------------------------------------------------------------

int WstrAppend(R2I_WSTR **pWstr, WCHAR *Buf, DWORD Length)
{
	R2I_WSTR *Wstr = *pWstr;
	DWORD ResultLength;

	if(Length == WSTR_LENGTH_UNKNOWN)
		Length = wcslen(Buf);

	ResultLength = Wstr->Length + Length;

	if(ResultLength > Wstr->BufLength) {
		if(!WstrExpand(&Wstr, ResultLength))
			return 0;
	}

	memcpy(Wstr->Buf + Wstr->Length, Buf, Length * sizeof(WCHAR));
	Wstr->Buf[ResultLength] = 0;
	Wstr->Length = ResultLength;

	*pWstr = Wstr;
	return 1;
}

// -------------------------------------------------------------------------------------------------

void WstrDelete(R2I_WSTR *Wstr, DWORD Start, DWORD Length)
{
	if(Start >= Wstr->Length)
		return;

	if(Start + Length >= Wstr->Length) {
		Wstr->Length = Start;
		Wstr->Buf[Start] = 0;
		return;
	}

	memmove(Wstr->Buf + Start, Wstr->Buf + Start + Length, 
		(Wstr->Length - Start - Length + 1) * sizeof(WCHAR));
	Wstr->Length -= Length;
}

// -------------------------------------------------------------------------------------------------

int WstrInsert(R2I_WSTR **pWstr, DWORD Pos, WCHAR *Buf, DWORD Length)
{
	R2I_WSTR *Wstr = *pWstr;
	DWORD ResultLength;

	if(Length == WSTR_LENGTH_UNKNOWN)
		Length = wcslen(Buf);

	if(Pos >= Wstr->Length) {
		return WstrAppend(pWstr, Buf, Length);
	}

	ResultLength = Wstr->Length + Length;

	if(!WstrExpand(&Wstr, ResultLength))
		return 0;

	memmove(Wstr->Buf + Pos + Length, Wstr->Buf + Pos, 
		(Wstr->Length - Pos + 1) * sizeof(WCHAR));
	memmove(Wstr->Buf + Pos, Buf, Length * sizeof(WCHAR));

	*pWstr = Wstr;
	return 1;
}

// -------------------------------------------------------------------------------------------------

WCHAR WstrLastChar(R2I_WSTR *Wstr)
{
	return Wstr->Buf[Wstr->Length - 1];
}

// -------------------------------------------------------------------------------------------------

WCHAR *_wcsrpbrk(WCHAR *buf, WCHAR *cs)
{
	WCHAR *p, *q, *r = NULL;

	for(p = buf; *p != 0; p++) {
		for(q = cs; *q != 0; q++) {
			if(*p == *q) {
				r = p;
			}
		}
	}

	return r;
}

// -------------------------------------------------------------------------------------------------
