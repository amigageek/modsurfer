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

  while (TRUE) {
    ASSERT(menu_redraw());
    gfx_fade_menu(TRUE);

    Status menu_status = menu_event_loop();
    ASSERT(menu_status);

    if (menu_status == StatusQuit) {
      break;
    }

    if (module_load_all()) {
      if (track_build()) {
        OwnBlitter();
        gfx_fade_menu(FALSE);
        game_action_loop();
        track_free();
      }
      else {
        OwnBlitter();
        gfx_draw_title("NOT ENOUGH MEMORY");
      }
    }
    else {
      OwnBlitter();
      gfx_draw_title("NOT ENOUGH MEMORY");
    }

    DisownBlitter();
  }

cleanup:
  return status;
}

#define kVolumeMax 0x40
#define kNumFadeFrames 0x20
#define kKeycodeEsc 0x45

VOID game_action_loop() {
  UWORD beats_per_min = kDefaultBeatsPerMin;
  UWORD ticks_per_div = kDefaultTicksPerDiv;
  UWORD next_step_idx = 0;
  WORD player_x = 0;
  ULONG camera_z = 0;
  UWORD camera_z_inc = 0;
  TrackStep* steps = track_get_steps();
  UWORD last_step_idx = track_get_length() - kNumPaddingSteps - 1;
  UBYTE last_mousex = read_mousex();
  BOOL running = TRUE;
  UWORD fade_frames = kNumFadeFrames;
  UWORD num_blocks = track_get_num_blocks();
  UWORD score = 0;

  gfx_draw_track();
  system_acquire_control();

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

      if (play_step->speed > 0) {
        if (play_step->speed <= 0x20) {
          ticks_per_div = play_step->speed;
        }
        else {
          beats_per_min = play_step->speed;
        }
      }

      // Suppress the sample corresponding to the collectible block.
      // This is reset if the block is collected or the next step is reached.
      UBYTE next_sample = (play_step + 1)->sample;

      if (next_sample) {
        ms_SuppressSample = next_sample;
      }

      // Recalculate per-frame Z increment to match step speed.
      camera_z_inc = (kBlockGapDepth * beats_per_min) / (UWORD)(125 * ticks_per_div);
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

    if ((! collect_step->collected) && player_x >= bound[0] && player_x <= bound[1]) {
      collect_step->collected = TRUE;
      ms_SuppressSample = 0xFF;
      ++ score;
    }

    if (fade_frames) {
      -- fade_frames;

      if (fade_frames & 1) {
        gfx_fade_action(running);
      }

      if (! running) {
        mt_mastervol(&custom, fade_frames * (kVolumeMax / kNumFadeFrames));
      }
    }

    UWORD score_frac = (score * 1000) / num_blocks;
    gfx_update_display(&steps[next_step_idx], player_x, camera_z, score_frac);

    camera_z += camera_z_inc;

    if (running && key_state[kKeycodeEsc]) {
      running = FALSE;
      fade_frames = kNumFadeFrames;
    }
  }

  mt_Enable = 0;
  mt_end(&custom);

  system_release_control();
}

static UBYTE read_mousex() {
  return custom.joy0dat & JOY0DAT_XALL;
}

#if 0 // FIXME
  UBYTE* song_pos = (UBYTE*)((ULONG)&mt_Enable - 4);
  ModuleNonChip* nonchip = module_get_nonchip();
  UBYTE curr_pat_idx = (UBYTE)-1;
  TrackScore* scores = track_get_scores();

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
