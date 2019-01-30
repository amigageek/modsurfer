#pragma once

#include "common.h"
#include "dtypes.h"

extern Status system_init();
extern VOID system_fini();
extern Status system_list_drives(dirlist_t* drives);
extern Status system_list_path(STRPTR path,
                               dirlist_t* entries);
extern VOID system_append_path(STRPTR base,
                               STRPTR subdir);
extern VOID system_acquire_control();
extern VOID system_release_control();

extern volatile UBYTE key_state[0x80];
