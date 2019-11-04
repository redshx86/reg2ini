// -------------------------------------------------------------------------------------------------

#pragma once
#include <windows.h>
#include <tchar.h>
#include "resource/resource.h"

// -------------------------------------------------------------------------------------------------

typedef struct __ABOUT_DLG_DATA {
	int Width;
	int Height;
	int Effect1[3][3];
	int Effect2[3][3];
	BYTE *Buffer;
	BYTE *TempBuffer;
	HDC hCompDC;
	HBITMAP hCompBitmap;
	HFONT hFont;
	DWORD counter;
	DWORD pause;
	int mode;
	int message;
} ABOUT_DLG_DATA;

// -------------------------------------------------------------------------------------------------

void ShowAboutDialog(HWND hwndOwner);

// -------------------------------------------------------------------------------------------------
