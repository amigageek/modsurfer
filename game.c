#include "game.h"
#include "custom.h"
#include "gfx.h"
#include "menu.h"
#include "module.h"
#include "ptplayer/ptplayer.h"
#include "system.h"
#include "track.h"

#include <proto/graphics.h>

static VOID game_action_loop();
static UBYTE read_mousex();

Status game_loop() {
  Status status = StatusOK;
  BOOL last_success = TRUE;

  while (TRUE) {
    if (last_success) {
      ASSERT(menu_redraw());
      gfx_fade_menu(TRUE);
    }

    Status menu_status = menu_event_loop();
    ASSERT(menu_status);

    if (menu_status == StatusQuit) {
      break;
    }

    last_success = module_load_all() && track_build();

    OwnBlitter();

    if (last_success) {
      gfx_fade_menu(FALSE);
      game_action_loop();
      track_free();
    }
    else {
      menu_redraw_button("NOT ENOUGH MEMORY");
    }

    DisownBlitter();
  }

cleanup:
  return status;
}

#define kVolumeMax 0x40
#define kNumFadeFrames 0x20
#define kNumTimeoutFrames 200
#define kKeycodeEsc 0x45

VOID game_action_loop() {
  UWORD next_step_idx = 0;
  WORD player_x = 0;
  ULONG camera_z = 0;
  UWORD camera_z_inc = 0;
  TrackStep* steps = track_get_steps();
  UWORD last_step_idx = track_get_length() - kNumPaddingSteps - 1;
  UBYTE last_mousex = read_mousex();
  BOOL running = TRUE;
  UWORD fade_frames = kNumFadeFrames;
  UWORD timeout_frames = kNumTimeoutFrames;
  UWORD num_blocks = track_get_num_blocks();
  UWORD num_blocks_passed = 0;
  UWORD score = 0;
  static UWORD vu_meter_vz; // work around compiler bug

  vu_meter_vz = 0;

  system_acquire_control();
  gfx_draw_track();
  gfx_enable_copper_blits(TRUE);

  mt_init(&custom, module_get_nonchip(), module_get_samples(), 0);
  mt_mastervol(&custom, kVolumeMax);

  ms_HoldRows = kNumVisibleSteps - kNumStepsDelay;
  ms_SuppressSample = 0xFF;
  mt_Enable = 1;

  while (running || fade_frames) {
    /* custom.color[0] = 0x000; */
    gfx_wait_vblank();
    /* custom.color[0] = 0xF00; */

    while (ms_StepCount > 0) {
      TrackStep* play_step = &steps[next_step_idx + kNumStepsDelay];

      if (play_step->active_lane) {
        ++ num_blocks_passed;
      }

      // Suppress the sample corresponding to the collectible block.
      // This is reset if the block is collected or the next step is reached.
      UBYTE next_sample = (play_step + 1)->sample;

      if (next_sample) {
        ms_SuppressSample = next_sample;
      }

      // Recalculate per-frame Z increment to match step speed.
      camera_z_inc = ms_camera_z_inc(kBlockGapDepth);
      camera_z = next_step_idx * kBlockGapDepth;

      ++ next_step_idx;
      -- ms_StepCount;

      if (next_step_idx == last_step_idx) {
        mt_Enable = 0;
        fade_frames = kNumFadeFrames;
        running = FALSE;
      }
    }

    UBYTE mousex = read_mousex();
    BYTE mouse_deltax = (BYTE)(mousex - last_mousex);

    if (mouse_deltax != 0) {
      last_mousex = mousex;
      player_x += mouse_deltax;
    }

    player_x = MAX(-kLaneWidth, MIN(kLaneWidth, player_x));

    static WORD bounds[4][2] = {
      0x3FFF, 0x3FFF,
      -kLaneWidth, (-kLaneWidth / 2),
      (-kLaneWidth / 2), (kLaneWidth / 2),
      (kLaneWidth / 2), kLaneWidth,
    };

    TrackStep* collect_step = &steps[next_step_idx + kNumStepsDelay];
    WORD* bound = (WORD*)&bounds[collect_step->active_lane];

    if ((collect_step->color < 12) && (player_x >= bound[0]) && (player_x <= bound[1])) {
      collect_step->color += 12;
      ms_SuppressSample = 0xFF;
      ++ score;
      vu_meter_vz = kFarZ;
    }

    if (fade_frames) {
      -- fade_frames;

      gfx_fade_action(running, ((fade_frames & 1) == 0) ? TRUE : FALSE);

      if (! running) {
        mt_mastervol(&custom, fade_frames * (kVolumeMax / kNumFadeFrames));
      }
    }

    ULONG vu_meter_z = vu_meter_vz + camera_z;
    vu_meter_vz = MAX(0, vu_meter_vz - 0x1000);

    UWORD score_frac = (score * 1000) / num_blocks;
    gfx_update_display(&steps[next_step_idx], player_x, camera_z, camera_z_inc, vu_meter_z, score_frac);

    camera_z += camera_z_inc;

    if (num_blocks_passed == num_blocks) {
      -- timeout_frames;
    }

    if (running && (key_state[kKeycodeEsc] || (timeout_frames == 0))) {
      running = FALSE;
      fade_frames = kNumFadeFrames;
    }
  }

  mt_Enable = 0;
  mt_end(&custom);

  gfx_enable_copper_blits(FALSE);
  gfx_clear_body();
  system_release_control();
}

static UBYTE read_mousex() {
  return custom.joy0dat & JOY0DAT_XALL;
}
