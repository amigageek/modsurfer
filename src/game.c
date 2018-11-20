#include "game.h"
#include "custom.h"
#include "gfx.h"
#include "menu.h"
#include "module.h"
#include "ptplayer/ptplayer.h"
#include "system.h"
#include "track.h"

#include <proto/graphics.h>

#define kKeycodeEsc 0x45

extern volatile UBYTE key_state[0x80];

static VOID game_action_loop();
static UBYTE read_mousex();

Status game_loop() {
  Status status = StatusOK;

  while (TRUE) {
    ASSERT(menu_redraw());
    gfx_fade_menu(TRUE);

    Status menu_status = menu_event_loop();
    ASSERT(menu_status);

    if (menu_status == StatusQuit) {
      break;
    }

    OwnBlitter(); // FIXME track_build might take time

    if (module_load_all()) {
      if (track_build()) {
        gfx_fade_menu(FALSE);
        game_action_loop();
        track_free();
      }
      else {
        gfx_draw_title("NOT ENOUGH MEMORY");
      }
    }
    else {
      gfx_draw_title("NOT ENOUGH MEMORY");
    }

    DisownBlitter();
  }

cleanup:
  return status;
}
#include <unistd.h> // FIXME
VOID game_action_loop() {
  ULONG elapsed_us = 0;
  ULONG next_step_at_us = 0;
  UWORD beats_per_min = 125;
  UWORD ticks_per_div = 6; // 0 (stop) to 32
  UWORD next_step_idx = 0;
  WORD player_x = 0;
  ULONG camera_z = 0;
  UWORD camera_z_inc = 0;
  UBYTE active_lane = 0;
  TrackStep* steps = track_get_steps();
  UWORD num_steps = track_get_length();
  UBYTE last_mousex = read_mousex();
  BOOL running = TRUE;
  BOOL fading = TRUE;
  UBYTE* song_pos = (UBYTE*)((ULONG)&mt_Enable - 4);
  ModuleNonChip* nonchip = module_get_nonchip();
  UBYTE curr_pat_idx = (UBYTE)-1;
  TrackScore* scores = track_get_scores();

  for (UWORD i = 0; i < ARRAY_NELEMS(key_state); ++ i) {
    key_state[i] = 0;
  }

  system_acquire_control();

  //sleep(1);
  gfx_draw_track();
  //  sleep(1);

  mt_init(&custom, module_get_nonchip(), module_get_samples(), 0);
  mt_mastervol(&custom, 16);

  ms_HoldRows = kNumVisibleSteps - kNumStepsDelay;
  ms_SuppressSample = 0xFF;
  mt_Enable = 1;

  while (running || fading) {
    if (next_step_idx == num_steps) { // FIXME
      running = FALSE;
      fading = TRUE;
    }

    while ((mt_Enable == 1 && ms_StepCount > 0) ||
           (mt_Enable == 0 && elapsed_us >= next_step_at_us)) { // FIXME: remove
      TrackStep* step = &steps[next_step_idx + kNumStepsDelay];

      if (step->speed > 0) {
        if (step->speed <= 0x20) {
          ticks_per_div = step->speed;
        }
        else {
          beats_per_min = step->speed;
        }
      }

      // Suppress the sample corresponding to the collectible block.
      // This is reset if the block is collected or the next step is reached.
      UBYTE next_sample = (step + 1)->sample;

      if (next_sample) {
        ms_SuppressSample = next_sample;
      }

      // Pre/post-scale to keep division result within 16 bits.
      ULONG next_step_us = ((5000000 / 2) / (UWORD)(beats_per_min * 2)) * ticks_per_div * 2;
      next_step_at_us += next_step_us;

      // Recalculate per-frame Z increment to match step speed.
      camera_z_inc = (kBlockGapDepth * beats_per_min) / (UWORD)(125 * ticks_per_div);

      camera_z = next_step_idx * kBlockGapDepth;
      ++ next_step_idx;
      -- ms_StepCount;

    }

    UBYTE mousex = read_mousex();
    BYTE mouse_deltax = (BYTE)(mousex - last_mousex);

    if (mouse_deltax != 0) {
      last_mousex = mousex;
      player_x += mouse_deltax;
    }

    if (key_state[kKeycodeEsc]) {
      running = FALSE;
      fading = TRUE;
    }

    player_x = MAX(-kLaneWidth, MIN(kLaneWidth, player_x));

    static WORD bounds[4][2] = {
      0x3FFF, 0x3FFF,
      -kLaneWidth, (-kLaneWidth / 2),
      (-kLaneWidth / 2), (kLaneWidth / 2),
      (kLaneWidth / 2), kLaneWidth,
    };

    TrackStep* player_step = &steps[next_step_idx + kNumStepsDelay];
    WORD* bound = (WORD*)&bounds[player_step->active_lane];

    if (player_x >= bound[0] && player_x <= bound[1]) {
      player_step->collected = TRUE;
      ms_SuppressSample = 0xFF;
    }

    if (fading) {
      fading = gfx_fade_action(running);
    }

    gfx_update_display(&steps[next_step_idx], player_x, camera_z);
    camera_z += camera_z_inc;

    elapsed_us += 20000; // 16667
    gfx_wait_vblank();
  }

  mt_Enable = 0;
  mt_end(&custom);

  system_release_control();
}

static UBYTE read_mousex() {
  return custom.joy0dat & JOY0DAT_XALL;
}

#if 0 // FIXME
      UBYTE pat_idx = nonchip->header.pat_tbl[*song_pos];

      if (pat_idx != curr_pat_idx) {
        curr_pat_idx = pat_idx;

        for (UWORD i = 0; i < kDispDepth; ++ i) {
          blit_rect(&disp_bpls[i][0][0], 0, 0, kDispStrideB, kDispWidth, 40, 0);
        }

        TrackScore* score = &scores[pat_idx];

        UBYTE num_str[10];
        int_to_str(curr_pat_idx, num_str, 2, 10);
        blit_text(&disp_bpls[0][0][0], kDispStrideB, kDispSliceB, num_str, 10, 0, 5);

        UWORD text_x = 30;

        for (UWORD samp_idx = 1; samp_idx < 31; ++ samp_idx) {
          if (score->per_sample[samp_idx].in_pattern) {
            UWORD color = (score->selected_sample == samp_idx) ? 6 : 5;

            int_to_str(samp_idx, num_str, 2, 10);
            blit_text(&disp_bpls[0][0][0], kDispStrideB, kDispSliceB, num_str, text_x, 0, color);

            int_to_str(score->per_sample[samp_idx].count >> 8, num_str, 2, 16);
            blit_text(&disp_bpls[0][0][0], kDispStrideB, kDispSliceB, num_str, text_x, 16, color);

            int_to_str(score->per_sample[samp_idx].pitch >> 8, num_str, 2, 16);
            blit_text(&disp_bpls[0][0][0], kDispStrideB, kDispSliceB, num_str, text_x, 8, color);

            text_x += 20;
          }
        }
      }
#endif
