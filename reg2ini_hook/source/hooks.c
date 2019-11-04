// -------------------------------------------------------------------------------------------------

#include "hooks.h"

// -------------------------------------------------------------------------------------------------

static WCHAR * hook_module_name_list[] = {
	HOOK_MODULE_NAME_LIST
};

// -------------------------------------------------------------------------------------------------

static hook_proc_entry_t hook_proc_list[] = {
	HOOK_PROC_LIST
};

// -------------------------------------------------------------------------------------------------

static code_hook_t * hook_table = NULL;

// -------------------------------------------------------------------------------------------------

static void hook_debug_print(char *msg)
{
#ifdef _DEBUG
	puts(msg);
#endif
}

// -------------------------------------------------------------------------------------------------

static void hook_print_info(char *op, char *name, CODE_HOOK_ERROR status)
{
#ifdef _DEBUG
	char *message;

	switch(status) {
		case CODE_HOOK_OK:			message = "OK";						break;
		case CODE_HOOK_NOMEM:		message = "Not enough memory";		break;
		case CODE_HOOK_BADREADPTR:	message = "Bad read pointer";		break;
		case CODE_HOOK_CANTPROTECT:	message = "Protection error";		break;
		case CODE_HOOK_CONFLICT:	message = "Conflict";				break;
		case CODE_HOOK_BADCODE:		message = "Unknown/bad opcode";		break;
		case CODE_HOOK_CANTFIX:		message = "Can't fix DLL code";		break;
		case CODE_HOOK_ABSENCE:		message = "Not installed";			break;
		case CODE_HOOK_CHANGED:		message = "Code changed";			break;
		case CODE_HOOK_BADPROC:		message = "Procedure not found";	break;
		default:					message = "Unknown error";			break;
	}

	printf("%s %s [%s]\n", op, message, name);
#endif
}

// -------------------------------------------------------------------------------------------------

CODE_HOOK_ERROR HooksInstall(int vectormode, int forcemode)
{
	CODE_HOOK_ERROR error, result;
	int module_id, proc_id;
	HANDLE hmodule;
	DWORD hook_table_size;
	hook_proc_entry_t *proc;

	hook_debug_print("\nInstalling hooks...\n");

	result = CODE_HOOK_OK;

	// allocate hook data storage (with execution permission)
	hook_table_size = (sizeof(code_hook_t) * HOOK_PROC_COUNT + 0xFFF) & ~0xFFF;
	if((hook_table = VirtualAlloc(NULL, hook_table_size, MEM_COMMIT, PAGE_EXECUTE_READWRITE)) == NULL)
		return CODE_HOOK_NOMEM;
	memset(hook_table, 0, hook_table_size);

	// install hooks
	for(module_id = 0; module_id < HOOK_MODULE_COUNT; ++module_id) {
		if((hmodule = GetModuleHandle(hook_module_name_list[module_id])) != NULL) {
			for(proc_id = 0; proc_id < HOOK_PROC_COUNT; ++proc_id) {
				// hook procedure data
				proc = hook_proc_list + proc_id;
				if(proc->module_id == module_id) {
					// write hook
					error = code_hook_install_dll(hook_table + proc_id,
						hmodule, proc->name, proc->target, vectormode, forcemode);
					// print debug info for hook
					hook_print_info("Hook", proc->name, error);
					// required hook failed, return error
					if( (error != CODE_HOOK_OK) && (proc->required) ) {
						result = error;
						break;
					}
				}
			}
		}
	}

	hook_debug_print("");

	if(result != CODE_HOOK_OK) {
		HooksUninstall();
		return result;
	}

	return CODE_HOOK_OK;
}

// -------------------------------------------------------------------------------------------------

CODE_HOOK_ERROR HooksUninstall()
{
	CODE_HOOK_ERROR error, result;
	int i;

	result = CODE_HOOK_OK;

	if(hook_table != NULL) {

		hook_debug_print("\nUninstalling hooks...\n");

		// uninstall hooks
		for(i = 0; i < HOOK_PROC_COUNT; ++i) {
			error = code_hook_remove(hook_table + i);
			if((error != CODE_HOOK_OK) && (error != CODE_HOOK_ABSENCE))
				result = error;
			hook_print_info("Unhook", hook_proc_list[i].name, error);
		}

		// if all hooks removed, free hook data block
		if(result == CODE_HOOK_OK) {
			VirtualFree(hook_table, 0, MEM_RELEASE);
			hook_table = NULL;
		}

		hook_debug_print("");

		hook_table = NULL;

	}

	return result;
}

// -------------------------------------------------------------------------------------------------

void *HookGetOEP(int proc)
{
	return code_hook_get_oep(hook_table + proc);
}

// -------------------------------------------------------------------------------------------------
