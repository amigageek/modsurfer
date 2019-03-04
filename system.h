#pragma once

#include "common.h"
#include "dtypes.h"

#include <graphics/view.h>

#define kNumKeycodes 0x80
#define kKeycodeA 0x20
#define kKeycodeD 0x22
#define kKeycodeEsc 0x45

extern Status system_init();
extern void system_fini();
extern void system_print_error(STRPTR msg);
extern Status system_time_micros(ULONG* time_micros);  // SystemError
extern Status system_add_input_handler(APTR handler_func,
                                       APTR handler_data);
extern void system_remove_input_handler();
extern void system_load_view(struct View* view);
extern void system_unload_view();
extern Status system_list_drives(dirlist_t* drives);   // SystemError
extern Status system_list_path(STRPTR path,
                               dirlist_t* entries);    // SystemError
extern void system_acquire_control();
extern void system_release_control();
extern void system_acquire_blitter();
extern void system_release_blitter();
extern BOOL system_is_rtg();

extern volatile UBYTE keyboard_state[kNumKeycodes];
