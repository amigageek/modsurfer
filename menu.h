#pragma once

#include "common.h"

extern Status menu_init();
extern void menu_fini();
extern Status menu_redraw();
extern Status menu_event_loop();
extern void menu_redraw_button(STRPTR text);
