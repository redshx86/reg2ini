// -------------------------------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <windows.h>
#include "../../common/dllexprt.h"
#include "../../common/startup.h"

// -------------------------------------------------------------------------------------------------

void showhelp()
{
	puts(
		"Usage: reg2ini [switches] <program name> [params]\n\n"
		"Possible switches:\n"
		"/?         - show this help message\n"
		"/z         - turn off all default (*) options\n"
		"/k         - terminate process if injection failed (*)\n"
		"/v         - enable hook vectoring (*)\n"
		"/f         - enable fixing externally modifed DLL code\n"
		"/g         - global mode (inject to child processes too)\n"
		"/s         - shell mode (install no hooks, but inject to child processes)\n"
		"/t xxx     - set asyncronous INI save timeout (msecs, default: 200)\n"
		"/n xxx.exe - disable inject to child process (XP+, e.g.: notepad.exe)\n"
		"/c xxx.ini - INI filename (e.g. config.ini, default: <program>.r2i)\n"
		"/d xxx     - data directory name (e.g. datadir, default: not defined)"
		);
}

// -------------------------------------------------------------------------------------------------

void print_error(DWORD errorcode)
{
	DWORD status;
	WCHAR *buf, *temp;

	status = FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER|
		FORMAT_MESSAGE_IGNORE_INSERTS|
		FORMAT_MESSAGE_FROM_SYSTEM,
		NULL,
		errorcode,
		0,
		(void*)&buf,
		0,
		NULL);

	if(!status) {
		printf("error %u\n", errorcode);
		return;
	}

	if((temp = wcspbrk(buf, L"\r\n")) != NULL)
		*temp = 0;

	printf("%S\n", buf);
	LocalFree(buf);
	return;
}

// -------------------------------------------------------------------------------------------------

int is_command_switch(WCHAR *arg)
{
	return ((arg[0] == L'-') || (arg[0] == L'/'));
}

// -------------------------------------------------------------------------------------------------

int parse_command_line(REG2INI_SETUP *setup, int argc, WCHAR **argv)
{
	DWORD cntnew, error, params_len, prog_name_len;
	void *temp;
	int current_arg, i, break_line = 0, quote_arg;
	WCHAR *data_dir_name = NULL, *config_name = NULL;
	WCHAR *prog_full_name = NULL;
	WCHAR *config_full_name = NULL;
	WCHAR *data_dir_full_name = NULL;
	WCHAR *command_line = NULL;
	WCHAR *params, *cursor;

	memset(setup, 0, sizeof(REG2INI_SETUP));

	setup->def_flags		= 1;
	setup->save_timeout		= 200;

	// get command line switches
	for(current_arg = 1; current_arg < argc; ++current_arg) {

		if(!is_command_switch(argv[current_arg]))
			break;

		switch(argv[current_arg][1]) {

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

			case 't':
				if( (current_arg + 1 == argc) || is_command_switch(argv[current_arg + 1]) )
					goto __no_parameter;
				setup->save_timeout = _wtoi(argv[++current_arg]);
				break;

			case 'n':
				if( (current_arg + 1 == argc) || is_command_switch(argv[current_arg + 1]) )
					goto __no_parameter;
				if(setup->no_inject_count == setup->no_inject_max) {
					cntnew = (setup->no_inject_max + 1) * 3 / 2;
					if((temp = realloc(setup->no_inject_items, cntnew * sizeof(WCHAR*))) == NULL)
						goto __no_memory;
					setup->no_inject_max = cntnew;
					setup->no_inject_items = temp;
				}
				current_arg++;
				if((setup->no_inject_items[setup->no_inject_count] = wcsdup(argv[current_arg])) == NULL)
					goto __no_memory;
				setup->no_inject_count++;
				break;

			case 'c':
				if( (current_arg + 1 == argc) || is_command_switch(argv[current_arg + 1]) )
					goto __no_parameter;
				config_name = argv[++current_arg];
				setup->flags |= REG2INI_HOOK_REGISTRY;
				break;

			case 'd':
				if( (current_arg + 1 == argc) || is_command_switch(argv[current_arg + 1]) )
					goto __no_parameter;
				data_dir_name = argv[++current_arg];
				setup->flags |= REG2INI_HOOK_FILESYSTEM;
				break;

			default:
				printf("Unknown switch %S!\n", argv[current_arg]);
				setup->show_help = 1;
				break_line = 1;
				break;
		}

	}

	if(break_line) {
		puts("");
	}

	// show help and exit
	if(setup->show_help) {
		showhelp();
		goto __cleanup;
	}

	// check save timeout parameter
	if( (setup->save_timeout < 100) || (setup->save_timeout > 60000) ) {
		printf("Invalid save timeout %u, valid range: 100..60000\n", setup->save_timeout);
		goto __cleanup;
	}

	// apply default flags
	if(setup->def_flags) {
		setup->flags |= REG2INI_DEFAULT_FLAGS;
	}

	// check flags
	if(!(setup->flags & (REG2INI_HOOK_REGISTRY|REG2INI_HOOK_FILESYSTEM))) {
		puts("Registry and filesystem redirection disabled both. Nothing to do!");
		goto __cleanup;
	}

	// find program to run
	if(current_arg == argc) {
		puts("Program name required!\n");
		showhelp();
		goto __cleanup;
	}

	if((error = StpFindExecutableFile(&prog_full_name, argv[current_arg])) != ERROR_SUCCESS) {
		printf("Can't find \"%S\"\n", argv[current_arg]);
		print_error(error);
		goto __cleanup;
	}
	current_arg++;

	// config filename
	if((error = StpGetDataPath(&config_full_name, prog_full_name, config_name, NULL, L".r2i")) != ERROR_SUCCESS) {
		printf("Can't get INI filename (%S)\n", config_name ? config_name : L"<program>.r2i");
		print_error(error);
		goto __cleanup;
	}

	// data directory
	if((error = StpGetDataPath(&data_dir_full_name, prog_full_name, data_dir_name, L"data", NULL)) != ERROR_SUCCESS) {
		printf("Can't get data directory name (%S)\n", data_dir_name ? data_dir_name : L"data");
		print_error(error);
		goto __cleanup;
	}

	// command line
	params_len = 0;
	for(i = current_arg; i < argc; i++) {
		params_len += wcslen(argv[i]) + 3;
	}

	if((cursor = params = malloc(params_len * sizeof(WCHAR))) == NULL)
		goto __no_memory;

	for(i = current_arg; i < argc; i++) {
		quote_arg = (wcspbrk(argv[i], L" \t") != NULL);
		if(i > current_arg) *(cursor++) = L' ';
		if(quote_arg) *(cursor++) = L'\"';
		wcscpy(cursor, argv[i]);
		cursor += wcslen(argv[i]);
		if(quote_arg) *(cursor++) = L'\"';
	}
	*cursor = 0;

	prog_name_len = wcslen(prog_full_name);

	if((cursor = command_line = malloc((prog_name_len + params_len + 4) * sizeof(WCHAR))) == NULL)
		goto __no_memory;

	*(cursor++) = '\"';
	wcscpy(cursor, prog_full_name);
	cursor += prog_name_len;
	*(cursor++) = '\"';
	if(params_len > 0) {
		*(cursor++) = ' ';
		wcscpy(cursor, params);
	} else {
		*cursor = 0;
	}

	// check data directory
	if(wcspbrk(data_dir_full_name, L";%") != NULL) {
		puts("Data directory full pathname should not contain ';' nor '%'!");
		goto __cleanup;
	}

	// result
	setup->prog_full_name = prog_full_name;
	setup->config_full_name = config_full_name;
	setup->data_dir_full_name = data_dir_full_name;
	setup->command_line = command_line;

	return 1;

__no_parameter:
	printf("Parameter required for \"%S\"\n\n", argv[current_arg]);
	showhelp();
	goto __cleanup;

__no_memory:
	puts("Not enough memory!");
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

int wmain(int argc, WCHAR **argv)
{
	REG2INI_SETUP setup;
	DWORD error;
	STARTUPINFO si;
	REG2INI_OPTIONS *options;

	setlocale(LC_ALL, ".OCP");

	puts("\n*** reg2ini by redsh\n");

	if(!parse_command_line(&setup, argc, argv)) {
		return 1;
	}

	if(!Reg2IniOptionsInitialize(&options, setup.data_dir_full_name, setup.config_full_name, setup.no_inject_items, setup.no_inject_count)) {
		StpSetupFree(&setup);
		puts("No enough memory!");
		return 1;
	}

	if((error = StpUpdatePath(setup.data_dir_full_name)) != ERROR_SUCCESS) {
		printf("Can't modify PATH variable: ");
		print_error(error);
		StpSetupFree(&setup);
		free(options);
		return 1;
	}

	options->Flags = setup.flags;
	options->IniSaveTimeout = setup.save_timeout;

	if((error = Reg2IniEnableDebugPrivilege()) != ERROR_SUCCESS) {
		printf("Can't get debug privilege: ");
		print_error(error);
		StpSetupFree(&setup);
		free(options);
		return 2;
	}

	memset(&si, 0, sizeof(si));
	si.cb = sizeof(si);

	printf("Executing %S\n", setup.command_line);

	if((error = Reg2IniCreateProcess(setup.command_line, &si, options)) != ERROR_SUCCESS) {
		print_error(error);
		StpSetupFree(&setup);
		free(options);
		return 3;
	}

	print_error(ERROR_SUCCESS);

	StpSetupFree(&setup);
	free(options);
	return 0;
}

// -------------------------------------------------------------------------------------------------
