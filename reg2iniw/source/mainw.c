// -------------------------------------------------------------------------------------------------

#include <windows.h>
#include <tchar.h>
#include <stdarg.h>
#include <locale.h>
#include "../../common/startup.h"
#include "../../common/dllexprt.h"
#include "../../common/options.h"
#include "wizard.h"

// -------------------------------------------------------------------------------------------------

const WCHAR *CommandLineHelp = 
	L"Usage: reg2ini [switches] <program name> [params]\r\n\r\n"
	L"Possible switches:\r\n"
	L"/?         - show this help message\r\n"
	L"/z         - turn off all default (*) options\r\n"
	L"/k         - terminate process if injection failed (*)\r\n"
	L"/v         - enable hook vectoring (*)\r\n"
	L"/f         - enable fixing externally modifed DLL code\r\n"
	L"/g         - global mode (inject to child processes too)\r\n"
	L"/s         - shell mode (install no hooks, but inject to child processes)\r\n"
	L"/min       - show window minimized\r\n"
	L"/max       - show window maximized\r\n"
	L"/t xxx     - set asyncronous INI save timeout (msecs, default: 200)\r\n"
	L"/n xxx.exe - disable inject to child process (XP+, e.g.: notepad.exe)\r\n"
	L"/c xxx.ini - INI filename (e.g. config.ini, default: <program>.r2i)\r\n"
	L"/d xxx     - data directory name (e.g. data, default: not defined)";

// -------------------------------------------------------------------------------------------------

static void ShowMessage(WCHAR *fmt, ...)
{
	WCHAR buf[1024];
	va_list ap;
	
	va_start(ap, fmt);
	vswprintf(buf, fmt, ap);
	MessageBox(NULL, buf, _T("Reg2Ini"), MB_ICONEXCLAMATION|MB_OK);
	va_end(ap);
}

// -------------------------------------------------------------------------------------------------

static void ShowMessageAndError(DWORD error, WCHAR *fmt, ...)
{
	DWORD status;
	WCHAR *message, *temp;
	WCHAR buf[1024];
	va_list ap;
	
	va_start(ap, fmt);
	vswprintf(buf, fmt, ap);

	status = FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER|
		FORMAT_MESSAGE_IGNORE_INSERTS|
		FORMAT_MESSAGE_FROM_SYSTEM,
		NULL,
		error,
		0,
		(void*)&message,
		0,
		NULL);

	if(!status) {
		swprintf(buf + wcslen(buf), L"\r\n\r\nError code %u", error);
	} else {
		if((temp = wcspbrk(message, L"\r\n")) != NULL)
			*temp = 0;
		swprintf(buf + wcslen(buf), L"\r\n\r\n%s", message);
		LocalFree(message);
	}

	MessageBox(NULL, buf, _T("Reg2Ini"), MB_ICONEXCLAMATION|MB_OK);

	va_end(ap);
}

// -------------------------------------------------------------------------------------------------

static int IsCommandLineSwitch(WCHAR *Arg)
{
	return ((Arg[0] == L'-') || (Arg[0] == L'/'));
}

// -------------------------------------------------------------------------------------------------

static int ParseCommandLine(REG2INI_SETUP *setup, STARTUPINFO *psi)
{
	DWORD i, argc, CurArg, cntnew, error, params_len, prog_name_len;
	void *temp;
	WCHAR *data_dir_name = NULL, *config_name = NULL;
	WCHAR *prog_full_name = NULL;
	WCHAR *config_full_name = NULL;
	WCHAR *data_dir_full_name = NULL;
	WCHAR *command_line = NULL;
	WCHAR *params, *cursor, **argv;
	int quote_arg;

	argv = CommandLineToArgvW(GetCommandLine(), &argc);

	memset(setup, 0, sizeof(REG2INI_SETUP));

	setup->def_flags		= 1;
	setup->save_timeout		= 200;

	// get command line switches
	for(CurArg = 1; CurArg < argc; ++CurArg) {

		if(!IsCommandLineSwitch(argv[CurArg]))
			break;

		switch(argv[CurArg][1]) {

			case '?':
			case 'h':
				setup->show_help = 1;
				break;

			case 'z':
				setup->def_flags = 0;
				break;

			case 'k': setup->flags |= REG2INI_TERMINATE_ON_FAIL;	break;
			case 'v': setup->flags |= REG2INI_VECTORED_HOOKS;		break;
			case 'f': setup->flags |= REG2INI_FORCED_HOOKS;			break;
			case 'g': setup->flags |= REG2INI_INJECT_CHILDS;		break;
			case 's': setup->flags |= REG2INI_SHELL_MODE;			break;

			case 'm':
				switch(argv[CurArg][2]) {
					case 'i': psi->wShowWindow = SW_MINIMIZE; break;
					case 'a': psi->wShowWindow = SW_MAXIMIZE; break;
				}
				break;

			case 't':
				if( (CurArg + 1 == argc) || IsCommandLineSwitch(argv[CurArg + 1]) )
					goto __no_parameter;
				setup->save_timeout = _wtoi(argv[++CurArg]);
				break;

			case 'n':
				if( (CurArg + 1 == argc) || IsCommandLineSwitch(argv[CurArg + 1]) )
					goto __no_parameter;
				if(setup->no_inject_count == setup->no_inject_max) {
					cntnew = (setup->no_inject_max + 1) * 3 / 2;
					if((temp = realloc(setup->no_inject_items, cntnew * sizeof(WCHAR*))) == NULL)
						goto __no_memory;
					setup->no_inject_max = cntnew;
					setup->no_inject_items = temp;
				}
				CurArg++;
				if((setup->no_inject_items[setup->no_inject_count] = wcsdup(argv[CurArg])) == NULL)
					goto __no_memory;
				setup->no_inject_count++;
				break;

			case 'c':
				if( (CurArg + 1 == argc) || IsCommandLineSwitch(argv[CurArg + 1]) )
					goto __no_parameter;
				config_name = argv[++CurArg];
				setup->flags |= REG2INI_HOOK_REGISTRY;
				break;

			case 'd':
				if( (CurArg + 1 == argc) || IsCommandLineSwitch(argv[CurArg + 1]) )
					goto __no_parameter;
				data_dir_name = argv[++CurArg];
				setup->flags |= REG2INI_HOOK_FILESYSTEM;
				break;

			default:
				ShowMessage(L"Unknown switch \"%s\".\r\n\r\nUse reg2ini /? for help.", argv[CurArg]);
				goto __cleanup;
		}

	}

	// show help and exit
	if(setup->show_help) {
		ShowMessage(L"%s", CommandLineHelp);
		goto __cleanup;
	}

	// check save timeout parameter
	if( (setup->save_timeout < 100) || (setup->save_timeout > 60000) ) {
		ShowMessage(L"Invalid save timeout %u, valid range: 100..60000", setup->save_timeout);
		goto __cleanup;
	}

	// apply default flags
	if(setup->def_flags) {
		setup->flags |= REG2INI_DEFAULT_FLAGS;
	}

	// check flags
	if(!(setup->flags & (REG2INI_HOOK_REGISTRY|REG2INI_HOOK_FILESYSTEM))) {
		ShowMessage(L"Registry and filesystem redirection disabled both. Nothing to do!");
		goto __cleanup;
	}

	// find program to run
	if(CurArg == argc) {
		ShowMessage(L"Program name required.\r\n\r\n"
			L"Use reg2ini /? for help.");
		goto __cleanup;
	}

	if((error = StpFindExecutableFile(&prog_full_name, argv[CurArg])) != ERROR_SUCCESS) {
		ShowMessageAndError(error, L"Can't execute \"%s\"", argv[CurArg]);
		goto __cleanup;
	}
	CurArg++;

	// config filename
	if((error = StpGetDataPath(&config_full_name, prog_full_name, config_name, NULL, L".r2i")) != ERROR_SUCCESS) {
		ShowMessageAndError(error, L"Can't get INI filename (%s)", config_name ? config_name : L"<program>.r2i");
		goto __cleanup;
	}

	// data directory
	if((error = StpGetDataPath(&data_dir_full_name, prog_full_name, data_dir_name, L"data", NULL)) != ERROR_SUCCESS) {
		ShowMessageAndError(error, L"Can't get data directory name (%s)", data_dir_name ? data_dir_name : L"data");
		goto __cleanup;
	}

	// command line
	params_len = 0;
	for(i = CurArg; i < argc; i++) {
		params_len += wcslen(argv[i]) + 3;
	}

	if((cursor = params = malloc(params_len * sizeof(WCHAR))) == NULL)
		goto __no_memory;

	for(i = CurArg; i < argc; i++) {
		quote_arg = (wcspbrk(argv[i], L" \t") != NULL);
		if(i > CurArg) *(cursor++) = L' ';
		if(quote_arg) *(cursor++) = L'\"';
		wcscpy(cursor, argv[i]);
		cursor += wcslen(argv[i]);
		if(quote_arg) *(cursor++) = L'\"';
	}
	*cursor = 0;

	prog_name_len = wcslen(prog_full_name);

	if((cursor = command_line = malloc((prog_name_len + params_len + 4) * sizeof(WCHAR))) == NULL)
		goto __no_memory;

	*(cursor++) = L'\"';
	wcscpy(cursor, prog_full_name);
	cursor += prog_name_len;
	*(cursor++) = L'\"';
	if(params_len > 0) {
		*(cursor++) = ' ';
		wcscpy(cursor, params);
	} else {
		*cursor = 0;
	}

	// check data directory
	if(wcspbrk(data_dir_full_name, L";%") != NULL) {
		ShowMessage(L"Data directory full pathname should not contain ';' nor '%'!");
		goto __cleanup;
	}

	// result
	setup->prog_full_name = prog_full_name;
	setup->config_full_name = config_full_name;
	setup->data_dir_full_name = data_dir_full_name;
	setup->command_line = command_line;

	return 1;

__no_parameter:
	ShowMessage(L"Parameter required for \"%s\"\r\n\r\n"
		L"Use reg2ini /? for help.", argv[CurArg]);
	goto __cleanup;

__no_memory:
	ShowMessage(L"Not enough memory!");
	goto __cleanup;

__cleanup:
	free(command_line);
	free(data_dir_full_name);
	free(config_full_name);
	free(prog_full_name);
	StpSetupFree(setup);
	return 0;
}

// -------------------------------------------------------------------------------------------------

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE unused, LPSTR CmdLine, int CmdShow)
{
	REG2INI_SETUP setup;
	DWORD error;
	STARTUPINFO si;
	REG2INI_OPTIONS *options;

	setlocale(LC_ALL, ".ACP");

	memset(&si, 0, sizeof(si));
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = CmdShow;

	if(strlen(CmdLine) == 0) {
		ShowWizardDialog(hInst);
		return 0;
	}

	if(!ParseCommandLine(&setup, &si)) {
		return 1;
	}

	if(!Reg2IniOptionsInitialize(&options, setup.data_dir_full_name, setup.config_full_name, setup.no_inject_items, setup.no_inject_count)) {
		StpSetupFree(&setup);
		ShowMessage(L"Not enough memory.");
		return 1;
	}

	if((error = StpUpdatePath(setup.data_dir_full_name)) != ERROR_SUCCESS) {
		ShowMessageAndError(error, L"Can't modify PATH variable.");
		StpSetupFree(&setup);
		free(options);
		return 1;
	}

	options->Flags = setup.flags;
	options->IniSaveTimeout = setup.save_timeout;

	if((error = Reg2IniEnableDebugPrivilege()) != ERROR_SUCCESS) {
		ShowMessageAndError(error, L"Can't get debug privilege.");
		StpSetupFree(&setup);
		free(options);
		return 2;
	}

	if((error = Reg2IniCreateProcess(setup.command_line, &si, options)) != ERROR_SUCCESS) {
		ShowMessageAndError(error, L"Can't launch program.");
		StpSetupFree(&setup);
		free(options);
		return 3;
	}

	StpSetupFree(&setup);
	free(options);
	return 0;
}

// -------------------------------------------------------------------------------------------------
