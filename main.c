#include "common.h"
#include "game.h"
#include "gfx.h"
#include "menu.h"
#include "system.h"
#include "track.h"

#include <dos/dos.h>

int main() {
  Status status = StatusOK;

  common_init();
  ASSERT(system_init());
  ASSERT(track_init());
  ASSERT(gfx_init());
  ASSERT(menu_init());
  ASSERT(game_loop());

cleanup:
  menu_fini();
  gfx_fini();
  system_fini();

  return (status == StatusOK) ? RETURN_OK : RETURN_FAIL;
}
