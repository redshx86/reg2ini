// -------------------------------------------------------------------------------------------------

#include "about.h"

// -------------------------------------------------------------------------------------------------

#define IDT_FRAME		1
#define FPS_COUNT		24

// -------------------------------------------------------------------------------------------------

WCHAR *TextMessages[] = 
{
	L"",
	L"MADE IN USSR",
	L"BY REDSH",
	L"REG2INI",
	NULL
};

COLORREF MessageColor[] =
{
	RGB(0,0,0),
	RGB(255,0,0),
	RGB(255,32,0),
	RGB(255,64,0),
};

// -------------------------------------------------------------------------------------------------

void MakeEffect(ABOUT_DLG_DATA *data)
{
	data->Effect1[2][0] = 0;   data->Effect1[2][1] = 256; data->Effect1[2][2] = 0;
	data->Effect1[1][0] = 0;   data->Effect1[1][1] = 1;   data->Effect1[1][2] = 0;
	data->Effect1[0][0] = 0;   data->Effect1[0][1] = 0;   data->Effect1[0][2] = 0;

	data->Effect1[2][0] = rand() % 2;
	data->Effect1[2][2] = 1 - data->Effect1[2][0];

	data->Effect2[2][0] = 1;  data->Effect2[2][1] = 1;  data->Effect2[2][2] = 1;
	data->Effect2[1][0] = 1;  data->Effect2[1][1] = 260; data->Effect2[1][2] = 1;
	data->Effect2[0][0] = 1;  data->Effect2[0][1] = 1;  data->Effect2[0][2] = 1;
}

// -------------------------------------------------------------------------------------------------

void ApplyEffect(ABOUT_DLG_DATA *data, int id)
{
	BITMAPINFO bi;

	int x, y, mx, my, sx, sy;
	BYTE *DstRow, *SrcRow;
	BYTE *DstPixel, *SrcPixel;
	int r, g, b, n;

	memset(&bi, 0, sizeof(bi));
	bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
	bi.bmiHeader.biWidth = data->Width;
	bi.bmiHeader.biHeight = data->Height;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 24;
	bi.bmiHeader.biCompression = BI_RGB;
	bi.bmiHeader.biSizeImage = (data->Width * data->Height * 3);

	GetDIBits(data->hCompDC, data->hCompBitmap, 0, data->Height, data->TempBuffer, &bi, DIB_RGB_COLORS);

	for(y = 0; y < data->Height; ++y) {
		DstRow = data->Buffer + y * data->Width * 3;
		for(x = 0; x < data->Width; ++x) {
			DstPixel = DstRow + x * 3;
			r = g = b = n = 0;
			for(my = 0; my < 3; my++) {
				sy = y + my - 1;
				if((sy >= 0) && (sy < data->Height)) {
					SrcRow = data->TempBuffer + sy * data->Width * 3;
					for(mx = 0; mx < 3; mx++) {
						sx = x + mx - 1;
						if((sx >= 0) && (sx < data->Width)) {
							SrcPixel = SrcRow + sx * 3;
							if(id == 1) {
								b += SrcPixel[0] * data->Effect1[my][mx];
								g += SrcPixel[1] * data->Effect1[my][mx];
								r += SrcPixel[2] * data->Effect1[my][mx];
							} else if(id == 2) {
								b += SrcPixel[0] * data->Effect2[my][mx];
								g += SrcPixel[1] * data->Effect2[my][mx];
								r += SrcPixel[2] * data->Effect2[my][mx];
							}
						}
					}
				}
			}
			b >>= 8;
			g >>= 8;
			r >>= 8;

			if(b > 255) b = 255;
			if(g > 255) g = 255;
			if(r > 255) r = 255;

			DstPixel[0] = b;
			DstPixel[1] = g;
			DstPixel[2] = r;
		}
	}

	SetDIBits(data->hCompDC, data->hCompBitmap, 0, data->Height, data->Buffer, &bi, DIB_RGB_COLORS);
}

// -------------------------------------------------------------------------------------------------

void MakeSnow(ABOUT_DLG_DATA *data, int y, int factor)
{
	int i, j;

	for(i = 1; i < y; ++i) {
		for(j = 1; j < data->Width - 1; ++j) {
			if(rand() % factor == 0) {
				SetPixel(data->hCompDC, j, i, RGB(255,255,255));
			}
		}
	}

}

// -------------------------------------------------------------------------------------------------

void MakeText(ABOUT_DLG_DATA *data, COLORREF color, WCHAR *text)
{
	int length, cxchar = 18, cychar = 32;
	int x, y, x2, y2, i;

	SetBkMode(data->hCompDC, TRANSPARENT);
	SetTextColor(data->hCompDC, color);

	length = wcslen(text);

	y = 8;
	x = data->Width / 2 - cxchar * length / 2;

	for(i = 0; i < length; ++i) {
		y2 = (y + rand() % 4) - 2;
		x2 = x;//(x + rand() % 2) - 1;
		TextOut(data->hCompDC, x2, y2, text + i, 1);
		x += cxchar;
	}
}

// -------------------------------------------------------------------------------------------------

void FrameMove(HWND hDlg)
{
	HDC hDC;
	ABOUT_DLG_DATA *data;
	int nextmode;

	data = (void*)GetWindowLong(hDlg, DWL_USER);

	nextmode = data->mode;

	MakeEffect(data);

	switch(data->mode) {
		
		case 1:
			MakeSnow(data, 2, 500);
			MakeSnow(data, data->Height - 1, 30000);
			if(data->counter >= 30) {
				MakeText(data, MessageColor[data->message], TextMessages[data->message]);
				data->message++;
				if(TextMessages[data->message] == NULL) {
					data->message = 0;
					nextmode = 3;
				}
				data->counter = 0;
			}
			ApplyEffect(data, 1);
			break;
		
		case 3:
			if(data->counter >= 40) {
				data->counter = 0;
				nextmode = 4;
			}
			ApplyEffect(data, 2);
			break;
	
		case 4:
			if(data->counter >= 500) {
				data->counter = 0;
				nextmode = 1;
			}
			break;
	}

	if((data->mode == 1) || (data->mode == 3)) {
		hDC = GetDC(hDlg);
		BitBlt(hDC, 0, 0, data->Width, data->Height, data->hCompDC, 0, 0, SRCCOPY);
		ReleaseDC(hDlg, hDC);
	}

	data->counter++;
	data->mode = nextmode;
}

// -------------------------------------------------------------------------------------------------

void AboutDlgInit(HWND hDlg)
{
	ABOUT_DLG_DATA *data;
	int cxwindow, cywindow;
	LOGFONT lf;
	HDC hDC;

	data = calloc(1, sizeof(ABOUT_DLG_DATA));

	data->Width = 256;
	data->Height = 100;

	data->Buffer = malloc(data->Width * data->Height * 3);
	data->TempBuffer = malloc(data->Width * data->Height * 3);

	hDC = GetDC(hDlg);
	data->hCompDC = CreateCompatibleDC(hDC);
	data->hCompBitmap = CreateCompatibleBitmap(hDC, data->Width, data->Height);
	SelectObject(data->hCompDC, data->hCompBitmap);

	memset(&lf, 0, sizeof(lf));
	lf.lfHeight = 30;
	lf.lfWeight = FW_NORMAL;
	lf.lfCharSet = DEFAULT_CHARSET;
	_tcscpy(lf.lfFaceName, _T("Courier"));
	data->hFont = CreateFontIndirect(&lf);
	SelectObject(data->hCompDC, data->hFont);

	ReleaseDC(hDlg, hDC);

	SetTimer(hDlg, IDT_FRAME, 1000 / FPS_COUNT, NULL);

	cxwindow = data->Width + GetSystemMetrics(SM_CXDLGFRAME) * 2;
	cywindow = data->Height + GetSystemMetrics(SM_CYDLGFRAME) * 2 + GetSystemMetrics(SM_CYSMCAPTION);
	SetWindowPos(hDlg, NULL, 0, 0, cxwindow, cywindow, SWP_NOMOVE|SWP_NOZORDER);

	SetWindowLong(hDlg, DWL_USER, (LONG)data);

	MakeSnow(data, data->Height - 1, 500);
	data->mode = 1;
}

// -------------------------------------------------------------------------------------------------

void AboutDlgDestroy(HWND hDlg)
{
	ABOUT_DLG_DATA *data;

	data = (void*)GetWindowLong(hDlg, DWL_USER);

	KillTimer(hDlg, IDT_FRAME);

	DeleteObject(data->hCompBitmap);
	DeleteDC(data->hCompDC);
	free(data->TempBuffer);
	free(data->Buffer);
	free(data);
}

// -------------------------------------------------------------------------------------------------

void AboutDlgPaint(HWND hDlg)
{
	ABOUT_DLG_DATA *data;
	PAINTSTRUCT ps;
	HDC hDC;

	data = (void*)GetWindowLong(hDlg, DWL_USER);
	
	hDC = BeginPaint(hDlg, &ps);
	BitBlt(hDC, 0, 0, data->Width, data->Height, data->hCompDC, 0, 0, SRCCOPY);
	EndPaint(hDlg, &ps);
}

// -------------------------------------------------------------------------------------------------

BOOL CALLBACK AboutDlg(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch(uMsg) {
		
		case WM_INITDIALOG:
			AboutDlgInit(hDlg);
			return TRUE;

		case WM_DESTROY:
			AboutDlgDestroy(hDlg);
			return TRUE;

		case WM_PAINT:
			AboutDlgPaint(hDlg);
			return TRUE;

		case WM_TIMER:
			switch(wParam) {
				case IDT_FRAME:
					FrameMove(hDlg);
					return TRUE;
			}
			break;

		case WM_CLOSE:
		case WM_LBUTTONDOWN:
		case WM_RBUTTONDOWN:
			DestroyWindow(hDlg);
			return TRUE;

	}

	return FALSE;
}

// -------------------------------------------------------------------------------------------------

void ShowAboutDialog(HWND hwndOwner)
{
	HINSTANCE hInst;
	HWND hDlg;

	hInst = (void*)GetWindowLong(hwndOwner, GWL_HINSTANCE);
	
	hDlg = CreateDialog(hInst, MAKEINTRESOURCE(IDD_ABOUT), hwndOwner, AboutDlg);

	if(hDlg != NULL) {
		ShowWindow(hDlg, SW_SHOW);
	}
}

// -------------------------------------------------------------------------------------------------
