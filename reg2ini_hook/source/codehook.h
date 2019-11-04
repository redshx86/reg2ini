// -------------------------------------------------------------------------------------------------

#pragma once
#include <windows.h>
#include <winnt.h>

#define CODE_HOOK_MAX_MOVED_CODE			24
#define CODE_HOOK_CHECK_BLOCK				8
#define CODE_HOOK_CHECK_VECT_BLOCK			4

#define CODE_HOOK_FORCED_MODE_COMPARESIZE	24
#define CODE_HOOK_FORCED_MODE_MAX_FIXSIZE	10

#define CODE_HOOK_ENABLE_UNSAFE_VECTORING	1

// -------------------------------------------------------------------------------------------------

typedef enum __CODE_HOOK_ERROR {
	CODE_HOOK_OK,
	CODE_HOOK_NOMEM,
	CODE_HOOK_BADREADPTR,
	CODE_HOOK_CANTPROTECT,
	// install
	CODE_HOOK_CONFLICT,
	CODE_HOOK_BADCODE,
	CODE_HOOK_CANTFIX,
	// check, remove
	CODE_HOOK_ABSENCE,
	CODE_HOOK_CHANGED,
	// dll
	CODE_HOOK_BADPROC,
} CODE_HOOK_ERROR;

// -------------------------------------------------------------------------------------------------

#pragma pack(push, 1)
typedef struct code_hook {

	int is_active;
	int is_vectored;

	void *location;
	void *target;

	unsigned long codesize;
	unsigned char nops[16];
	unsigned char code[CODE_HOOK_MAX_MOVED_CODE + 8];
	unsigned char check[CODE_HOOK_CHECK_BLOCK];
	unsigned char checkvec[CODE_HOOK_CHECK_VECT_BLOCK];

} code_hook_t;
#pragma pack(pop)

// -------------------------------------------------------------------------------------------------

CODE_HOOK_ERROR code_hook_install(code_hook_t *phook, void *location, void *target, int vector);
CODE_HOOK_ERROR code_hook_remove(code_hook_t *phook);
CODE_HOOK_ERROR code_hook_verify(code_hook_t *phook);
void *code_hook_get_oep(code_hook_t *phook);

CODE_HOOK_ERROR code_hook_install_dll(code_hook_t *phook, HMODULE hmodule, LPCSTR procname, void *target, int vector, int forced_mode);

// -------------------------------------------------------------------------------------------------
