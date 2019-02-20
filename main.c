#include "common.h"
#include "game.h"
#include "gfx.h"
#include "menu.h"
#include "system.h"
#include "track.h"

#include <stdio.h>

int main() {
  Status status = StatusOK;

  ASSERT(common_init());
  ASSERT(system_init());
  track_init();
  ASSERT(gfx_init());
  ASSERT(menu_init());
  game_init();

  ASSERT(game_main_loop());

cleanup:
  menu_fini();
  gfx_fini();
  system_fini();

  return 0;
}
