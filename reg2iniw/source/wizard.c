// -------------------------------------------------------------------------------------------------

#include "wizard.h"

// -------------------------------------------------------------------------------------------------

static WCHAR *ConfigFilename = NULL;

// -------------------------------------------------------------------------------------------------

void SetCheckboxState(HWND hDlg, int CheckboxId, int On)
{
	int state;

	state = On ? BST_CHECKED : BST_UNCHECKED;
	SendDlgItemMessage(hDlg, CheckboxId, BM_SETCHECK, state, 0);
}

// -------------------------------------------------------------------------------------------------

int GetCheckboxState(HWND hDlg, int CheckboxId)
{
	int result;

	result = SendDlgItemMessage(hDlg, CheckboxId, BM_GETCHECK, 0, 0);
	return (result == BST_CHECKED);
}

// -------------------------------------------------------------------------------------------------

void AppendEdit(HWND hDlg, int EditId, WCHAR *Text)
{
	SendDlgItemMessage(hDlg, EditId, EM_SETSEL, 0x7fff, 0x7fff);
	SendDlgItemMessage(hDlg, EditId, EM_REPLACESEL, 0, (LPARAM)Text);
}

// -------------------------------------------------------------------------------------------------

void TrimString(WCHAR *Buf)
{
	WCHAR *p;
	DWORD len;

	len = wcslen(Buf);

	for(p = Buf; iswspace(*p); p++)
		len--;
	if(p != Buf) {
		memmove(Buf, p, (len + 1) * sizeof(WCHAR));
	}

	p = Buf + len - 1;
	while((len != 0) && (iswspace(*p))) {
		*(p--) = 0;
		len--;
	}
}

// -------------------------------------------------------------------------------------------------

int BuildCommandLine(HWND hDlg, int ShowErrors)
{
	int success = 1;
	int NoDefOpt, TermOnFail;
	int HookVect, HookForce;
	int GlobMode, ShellMode;
	int EnableConfig, EnableFsRedir;
	int SaveDelay;
	WCHAR *ProgName, *ConfigName;
	WCHAR *DataDirName, *IgnoreItems;
	WCHAR *Item, *NextItem;
	WCHAR buf[14];
	int Quotes;

	ProgName = malloc(MAX_PATH * sizeof(WCHAR));
	ConfigName = malloc(MAX_PATH * sizeof(WCHAR));
	DataDirName = malloc(MAX_PATH * sizeof(WCHAR));
	IgnoreItems = malloc(MAX_PATH * sizeof(WCHAR));

	// get options
	TermOnFail = GetCheckboxState(hDlg, IDC_TERMINATE);
	HookVect = GetCheckboxState(hDlg, IDC_VECTORED);
	HookForce = GetCheckboxState(hDlg, IDC_FORCED);
	GlobMode = GetCheckboxState(hDlg, IDC_GLOBAL);
	ShellMode = GetCheckboxState(hDlg, IDC_SHELL);
	EnableConfig = !GetCheckboxState(hDlg, IDC_DISABLE_CONFIG);
	EnableFsRedir = !GetCheckboxState(hDlg, IDC_DISABLE_FS_REDIR);

	SaveDelay = GetDlgItemInt(hDlg, IDC_DELAY, NULL, TRUE);

	GetDlgItemText(hDlg, IDC_PROGRAM_NAME, ProgName, MAX_PATH);
	GetDlgItemText(hDlg, IDC_CONFIG_NAME, ConfigName, MAX_PATH);
	GetDlgItemText(hDlg, IDC_DATA_DIRECTORY_NAME, DataDirName, MAX_PATH);
	GetDlgItemText(hDlg, IDC_IGNORE, IgnoreItems, MAX_PATH);

	TrimString(ProgName);
	TrimString(ConfigName);
	TrimString(DataDirName);

	// clear command line window
	SetDlgItemText(hDlg, IDC_COMMAND, L"");

	// program name
	if(GetCheckboxState(hDlg, IDC_CONSOLE)) {
		AppendEdit(hDlg, IDC_COMMAND, L"reg2ini");
	} else {
		AppendEdit(hDlg, IDC_COMMAND, L"reg2iniw");
	}

	// check default options
	if(TermOnFail && HookVect && EnableConfig) {
		NoDefOpt = 0;
		TermOnFail = 0;
		HookVect = 0;
	} else {
		NoDefOpt = 1;
	}

	// put options
	if(NoDefOpt)	AppendEdit(hDlg, IDC_COMMAND, L" /z");
	if(TermOnFail)	AppendEdit(hDlg, IDC_COMMAND, L" /k");
	if(HookVect)	AppendEdit(hDlg, IDC_COMMAND, L" /v");
	if(HookForce)	AppendEdit(hDlg, IDC_COMMAND, L" /f");
	if(GlobMode)	AppendEdit(hDlg, IDC_COMMAND, L" /g");
	if(ShellMode)	AppendEdit(hDlg, IDC_COMMAND, L" /s");

	// config name
	if(EnableConfig) {
		if(wcslen(ConfigName) == 0) {
			if(NoDefOpt) {
				AppendEdit(hDlg, IDC_COMMAND, L" /c config.ini");
			}
		} else {
			Quotes = (wcspbrk(ConfigName, L"\t ") != NULL);
			AppendEdit(hDlg, IDC_COMMAND, L" /c ");
			if(Quotes) AppendEdit(hDlg, IDC_COMMAND, L"\"");
			AppendEdit(hDlg, IDC_COMMAND, ConfigName);
			if(Quotes) AppendEdit(hDlg, IDC_COMMAND, L"\"");
		}
	}

	// data directory name
	if(EnableFsRedir) {
		if(wcslen(DataDirName) == 0) {
			AppendEdit(hDlg, IDC_COMMAND, L" /d data");
		} else {
			Quotes = (wcspbrk(DataDirName, L"\t ") != NULL);
			AppendEdit(hDlg, IDC_COMMAND, L" /d ");
			if(Quotes) AppendEdit(hDlg, IDC_COMMAND, L"\"");
			AppendEdit(hDlg, IDC_COMMAND, DataDirName);
			if(Quotes) AppendEdit(hDlg, IDC_COMMAND, L"\"");
		}
	}

	// save delay
	if((SaveDelay != 0) && (SaveDelay != 200)) {
		_itow(SaveDelay, buf, 10);
		AppendEdit(hDlg, IDC_COMMAND, L" /t ");
		AppendEdit(hDlg, IDC_COMMAND, buf);
	}

	// ignore items
	for(Item = IgnoreItems; Item != NULL; Item = NextItem) {

		while(*Item == ' ')
			Item++;

		if(*Item == L'\"') {
			Item++;
			if((NextItem = wcschr(Item, L'\"')) != NULL)
				*(NextItem++) = 0;
		} else {
			NextItem = wcschr(Item, L' ');
		}

		if(NextItem != NULL) {
			while(*NextItem == ' ')
				*(NextItem++) = 0;
		}

		if(*Item != 0) {
			Quotes = (wcschr(Item, ' ') != NULL);
			AppendEdit(hDlg, IDC_COMMAND, L" /n ");
			if(Quotes) AppendEdit(hDlg, IDC_COMMAND, L"\"");
			AppendEdit(hDlg, IDC_COMMAND, Item);
			if(Quotes) AppendEdit(hDlg, IDC_COMMAND, L"\"");
		}
	
	}

	// program name
	if(wcslen(ProgName) != 0) {
		Quotes = (wcspbrk(ProgName, L"\t ") != NULL);
		AppendEdit(hDlg, IDC_COMMAND, L" ");
		if(Quotes) AppendEdit(hDlg, IDC_COMMAND, L"\"");
		AppendEdit(hDlg, IDC_COMMAND, ProgName);
		if(Quotes) AppendEdit(hDlg, IDC_COMMAND, L"\"");
	}

	if((SaveDelay != 0) && ((SaveDelay < 100) || (SaveDelay > 60000))) {
		if(ShowErrors) {
			MessageBox(hDlg, L"Invalid save delay (valid range: 100 to 60,000 milliseconds).", 
				L"Confguration error!", MB_ICONEXCLAMATION|MB_OK);
		}
		success = 0;
	}

	if(wcslen(ProgName) == 0) {
		if(ShowErrors) {
			MessageBox(hDlg, L"Program name not specified.", 
				L"Confguration error!", MB_ICONEXCLAMATION|MB_OK);
		}
		success = 0;
	}

	if((!EnableConfig) && (!EnableFsRedir)) {
		if(ShowErrors) {
			MessageBox(hDlg, 
				L"Both configuration file and data directory disabled.",
				L"Useless configuration!", MB_ICONEXCLAMATION|MB_OK);
		}
		success = 0;
	}

	free(IgnoreItems);
	free(DataDirName);
	free(ConfigName);
	free(ProgName);

	return success;
}

// -------------------------------------------------------------------------------------------------

void SelectProgram(HWND hDlg)
{
	OPENFILENAME ofn;
	WCHAR buf[MAX_PATH];

	GetDlgItemText(hDlg, IDC_PROGRAM_NAME, buf, MAX_PATH);

	memset(&ofn, 0, sizeof(ofn));

	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = hDlg;
	ofn.hInstance = (void*)GetWindowLong(hDlg, GWL_HINSTANCE);
	ofn.lpstrFilter = L"Program (*.exe)\0*.exe\0Anything (*.*)\0*\0";
	ofn.lpstrFile = buf;
	ofn.nMaxFile = MAX_PATH;
	ofn.Flags = OFN_ENABLESIZING | OFN_HIDEREADONLY;
	ofn.lpstrDefExt = L"exe";
	ofn.lpstrTitle = L"Select program to run";
	
	if(GetOpenFileName(&ofn)) {
		SetDlgItemText(hDlg, IDC_PROGRAM_NAME, buf);
	}

}

// -------------------------------------------------------------------------------------------------

void LoadCfgString(WCHAR *Buf, DWORD BufSize, HWND hDlg, WCHAR *KeyName, int EditId, WCHAR *DefString)
{
	GetPrivateProfileString(L"wizard", KeyName, DefString, Buf, BufSize, ConfigFilename);
	SetDlgItemText(hDlg, EditId, Buf);
}

// -------------------------------------------------------------------------------------------------

void LoadCfgBool(HWND hDlg, WCHAR *KeyName, int CheckboxId, int Def)
{
	int value;

	value = GetPrivateProfileInt(L"wizard", KeyName, Def, ConfigFilename);
	SetCheckboxState(hDlg, CheckboxId, value);
}

// -------------------------------------------------------------------------------------------------

void SaveCfgString(HWND hDlg, WCHAR *Buf, DWORD BufSize, WCHAR *KeyName, int EditId)
{
	GetDlgItemText(hDlg, EditId, Buf, BufSize);
	WritePrivateProfileString(L"wizard", KeyName, Buf, ConfigFilename);
}

// -------------------------------------------------------------------------------------------------

void SaveCfgBool(HWND hDlg, WCHAR *KeyName, int CheckboxId)
{
	int value;
	WCHAR buf[14];

	value = (GetCheckboxState(hDlg, CheckboxId) ? 1 : 0);
	_itow(value, buf, 10);
	WritePrivateProfileString(L"wizard", KeyName, buf, ConfigFilename);
}

// -------------------------------------------------------------------------------------------------

void LoadSettings(HWND hDlg)
{
	WCHAR Temp[MAX_PATH], *p;
	WCHAR Def[MAX_PATH];

	ConfigFilename = malloc(MAX_PATH * sizeof(WCHAR));
	GetModuleFileName(NULL, ConfigFilename, MAX_PATH);

	if((p = wcsrchr(ConfigFilename, L'\\')) == NULL) 
		return;

	if((p = wcsrchr(p, L'.')) != NULL) {
		wcscpy(p, L".ini");
	} else {
		wcscat(ConfigFilename, L".ini");
	}

	GetTempPath(MAX_PATH, Def);
	p = Def + wcslen(Def);

	LoadCfgString(Temp, MAX_PATH, hDlg, L"run", IDC_PROGRAM_NAME, L"notepad");
	LoadCfgString(Temp, MAX_PATH, hDlg, L"dont inject", IDC_IGNORE, L"");

	wcscpy(p, L"notepad.ini");
	LoadCfgString(Temp, MAX_PATH, hDlg, L"config", IDC_CONFIG_NAME, Def);

	wcscpy(p, L"notepad");
	LoadCfgString(Temp, MAX_PATH, hDlg, L"data directory", IDC_DATA_DIRECTORY_NAME, Def);

	SetDlgItemInt(hDlg, IDC_DELAY, GetPrivateProfileInt(L"wizard", L"save delay", 200, ConfigFilename), TRUE);

	LoadCfgBool(hDlg, L"vectored hooks", IDC_VECTORED, 1);
	LoadCfgBool(hDlg, L"forced hooks", IDC_FORCED, 0);
	LoadCfgBool(hDlg, L"terminate on error", IDC_TERMINATE, 1);
	LoadCfgBool(hDlg, L"global mode", IDC_GLOBAL, 0);
	LoadCfgBool(hDlg, L"shell mode", IDC_SHELL, 0);
	LoadCfgBool(hDlg, L"console launcher", IDC_CONSOLE, 1);
	LoadCfgBool(hDlg, L"disable inireg", IDC_DISABLE_CONFIG, 0);
	LoadCfgBool(hDlg, L"disable fsredir", IDC_DISABLE_FS_REDIR, 1);
}

// -------------------------------------------------------------------------------------------------

void SaveSettings(HWND hDlg)
{
	WCHAR Temp[MAX_PATH];

	SaveCfgString(hDlg, Temp, MAX_PATH, L"run", IDC_PROGRAM_NAME);
	SaveCfgString(hDlg, Temp, MAX_PATH, L"config", IDC_CONFIG_NAME);
	SaveCfgString(hDlg, Temp, MAX_PATH, L"data directory", IDC_DATA_DIRECTORY_NAME);
	SaveCfgString(hDlg, Temp, MAX_PATH, L"dont inject", IDC_IGNORE);

	_itow(GetDlgItemInt(hDlg, IDC_DELAY, NULL, TRUE), Temp, 10);
	WritePrivateProfileString(L"wizard", L"save delay", Temp, ConfigFilename);

	SaveCfgBool(hDlg, L"disable inireg", IDC_DISABLE_CONFIG);
	SaveCfgBool(hDlg, L"disable fsredir", IDC_DISABLE_FS_REDIR);
	SaveCfgBool(hDlg, L"terminate on error", IDC_TERMINATE);
	SaveCfgBool(hDlg, L"vectored hooks", IDC_VECTORED);
	SaveCfgBool(hDlg, L"forced hooks", IDC_FORCED);
	SaveCfgBool(hDlg, L"global mode", IDC_GLOBAL);
	SaveCfgBool(hDlg, L"shell mode", IDC_SHELL);

	free(ConfigFilename);
}

// -------------------------------------------------------------------------------------------------

void SetIcons(HWND hDlg)
{
	HICON SmallIcon, BigIcon;
	HINSTANCE hInst;

	hInst = (void*)GetWindowLong(hDlg, GWL_HINSTANCE);
	SmallIcon = LoadImage(hInst, MAKEINTRESOURCE(IDI_MAIN), IMAGE_ICON, 16, 16, LR_SHARED);
	BigIcon = LoadImage(hInst, MAKEINTRESOURCE(IDI_MAIN), IMAGE_ICON, 32, 32, LR_SHARED);
	
	SendMessage(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)SmallIcon);
	SendMessage(hDlg, WM_SETICON, ICON_BIG, (LPARAM)BigIcon);
}

// -------------------------------------------------------------------------------------------------

void CopyCommandLine(HWND hDlg)
{
	if(!BuildCommandLine(hDlg, 1))
		return;

	SendDlgItemMessage(hDlg, IDC_COMMAND, EM_SETSEL, 0, -1);
	SendDlgItemMessage(hDlg, IDC_COMMAND, WM_COPY, 0, 0);
}

// -------------------------------------------------------------------------------------------------

void RunCommand(HWND hDlg)
{
	WCHAR *Buf;
	DWORD Length, Status;
	STARTUPINFO si;
	PROCESS_INFORMATION pi;

	if(!BuildCommandLine(hDlg, 1))
		return;

	Length = GetWindowTextLength(GetDlgItem(hDlg, IDC_COMMAND));
	Buf = malloc((Length + 1) * sizeof(WCHAR));
	GetDlgItemText(hDlg, IDC_COMMAND, Buf, Length + 1);

	memset(&si, 0, sizeof(si));

	if(CreateProcess(NULL, Buf, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
		free(Buf);
		return;
	}

	Status = GetLastError();

	free(Buf);

	Length = FormatMessage(
		FORMAT_MESSAGE_FROM_SYSTEM|
		FORMAT_MESSAGE_ALLOCATE_BUFFER|
		FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,
		Status,
		0,
		(void*)&Buf,
		0,
		NULL);

	if(!Length) {
		MessageBox(hDlg, L"Can't start process", NULL, MB_ICONEXCLAMATION|MB_OK);
		return;
	}

	MessageBox(hDlg, Buf, L"Can't start process", MB_ICONEXCLAMATION|MB_OK);
	
	LocalFree(Buf);
}

// -------------------------------------------------------------------------------------------------

BOOL CALLBACK WizardDlg(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch(uMsg) {
		
		case WM_INITDIALOG:
			SetIcons(hDlg);
			LoadSettings(hDlg);
			BuildCommandLine(hDlg, 0);
			return TRUE;

		case WM_DESTROY:
			SaveSettings(hDlg);
			PostQuitMessage(0);
			return TRUE;

		case WM_CLOSE:
			DestroyWindow(hDlg);
			return TRUE;

		case WM_COMMAND:
			
			switch(LOWORD(wParam)) {

				case IDC_VECTORED:
				case IDC_FORCED:
				case IDC_TERMINATE:
				case IDC_GLOBAL:
				case IDC_SHELL:
				case IDC_CONSOLE:
				case IDC_DISABLE_CONFIG:
				case IDC_DISABLE_FS_REDIR:
					BuildCommandLine(hDlg, 0);
					return TRUE;
				
				case IDC_PROGRAM_NAME:
				case IDC_CONFIG_NAME:
				case IDC_DATA_DIRECTORY_NAME:
				case IDC_DELAY:
				case IDC_IGNORE:
					if(HIWORD(wParam) == EN_UPDATE) {
						BuildCommandLine(hDlg, 0);
						return TRUE;
					}
					break;

				case IDC_PROGRAM_NAME_SELECT:
					SelectProgram(hDlg);
					break;

				case IDC_SHOW_ABOUT:
					if(GetKeyState(VK_CONTROL) & 0x8000) {
						ShowAboutDialog(hDlg);
					} else {
						MessageBox(hDlg, L"Reg2Ini by redsh\r\n\r\n"
							L"mailto: redsh@redsh.ru", L"About Reg2Ini",
							MB_ICONINFORMATION|MB_OK);
					}
					return TRUE;

				case IDC_COPY_CMD:
					CopyCommandLine(hDlg);
					return TRUE;

				case IDC_RUN_CMD:
					RunCommand(hDlg);
					return TRUE;
			
			}
			break;
	}

	return FALSE;
}

// -------------------------------------------------------------------------------------------------

void ShowWizardDialog(HINSTANCE hInst)
{
	MSG msg;
	HWND hDlg;

	hDlg = CreateDialog(hInst, MAKEINTRESOURCE(IDD_WIZARD), NULL, WizardDlg);

	if(hDlg != NULL) {

		ShowWindow(hDlg, SW_SHOW);

		while(GetMessage(&msg, NULL, 0, 0)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

	}
}

// -------------------------------------------------------------------------------------------------
