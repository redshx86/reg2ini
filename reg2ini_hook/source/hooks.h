// -------------------------------------------------------------------------------------------------

#pragma once
#include <windows.h>
#include <stdio.h>
#include "codehook.h"
#include "hooktbl.h"

// -------------------------------------------------------------------------------------------------

CODE_HOOK_ERROR HooksInstall(int vectormode, int forcemode);
CODE_HOOK_ERROR HooksUninstall();
void *HookGetOEP(int proc);

// -------------------------------------------------------------------------------------------------
