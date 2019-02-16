#include "game.h"
#include "custom.h"
#include "gfx.h"
#include "menu.h"
#include "module.h"
#include "ptplayer/ptplayer.h"
#include "system.h"
#include "track.h"

#define kNumFadeFrames 0x20
#define kNumTimeoutFrames 200
#define kVUMeterZDecay 0x1000
#define kVolumeMax 0x40
#define kNoSuppressSample 0xFF // Any unused sample number

static void game_play_loop();
static void ptplayer_start();
static void ptplayer_stop();
static void handle_steps();
static void handle_input();
static UBYTE read_mouse_x();
static void handle_collision();
static void handle_fade();
static void handle_gfx();
static void handle_timeout();

static struct {
  TrackStep* steps;
  UWORD next_step_idx;
  UWORD end_step_idx;
  UWORD num_blocks_total;
  UWORD num_blocks_left;
  ULONG camera_z;
  UWORD camera_z_inc;
  UWORD vu_meter_view_z;
  WORD ball_x;
  UBYTE prev_mouse_x;
  UWORD score;
  UWORD fade_frames;
  UWORD timeout_frames;
  BOOL running;
} g;

Status game_main_loop() {
  Status status = StatusOK;

  while (TRUE) {
    // Only redraw menu if we left it (started the game successfully).
    if (status == StatusOK) {
      ASSERT(menu_redraw());
      gfx_fade_menu(TRUE);
    }

    CATCH(menu_event_loop(), StatusQuit);

    if (status == StatusQuit) {
      status = StatusOK;
      break;
    }

    // Attempt to load the module and build the track. Either may fail.
    CATCH(module_load_all(), StatusInvalidMod | StatusOutOfMemory);

    if (status == StatusOK) {
      CATCH(track_build(), StatusOutOfMemory);
    }

    system_acquire_blitter();

    if (status == StatusOK) {
      // Exit the menu and start the game.
      gfx_fade_menu(FALSE);
      game_play_loop();

      track_free();
    }
    else {
      // Stay on menu and report the error.
      menu_redraw_button((status == StatusInvalidMod) ? "INVALID MOD FILE" : "NOT ENOUGH CHIP RAM");
    }

    system_release_blitter();
  }

cleanup:
  system_release_blitter();
  track_free();

  return status;
}

static void game_play_loop() {
  // Reset game state.
  g.steps = track_steps();
  g.next_step_idx = 0;
  g.end_step_idx = track_unpadded_length() - 1;
  g.num_blocks_total = track_num_blocks();
  g.num_blocks_left = g.num_blocks_total;
  g.camera_z = 0;
  g.camera_z_inc = 0;
  g.vu_meter_view_z = 0;
  g.ball_x = 0;
  g.prev_mouse_x = read_mouse_x();
  g.score = 0;
  g.fade_frames = kNumFadeFrames;
  g.timeout_frames = kNumTimeoutFrames;
  g.running = TRUE;

  // All colors faded to zero by this point.
  system_acquire_control();
  gfx_draw_track();

  // Enable copper blits after we're done with the blitter.
  gfx_wait_blit();
  system_allow_copper_blits(TRUE);

  ptplayer_start();

  while (g.running || g.fade_frames) {
    gfx_wait_vblank();

    handle_steps();
    handle_input();
    handle_collision();
    handle_fade();
    handle_gfx();
    handle_timeout();
  }

  ptplayer_stop();

  // Disable copper blits before using the blitter.
  system_allow_copper_blits(FALSE);

  // All colors faded to zero by this point.
  gfx_clear_body();
  system_release_control();
}

static void ptplayer_start() {
  mt_init(&custom, module_nonchip(), module_samples(), 0);
  mt_mastervol(&custom, kVolumeMax);

  // Delay ptplayer until first module row crosses all visible steps.
  // Offset slightly earlier to account for the player's Z position.
  ms_HoldRows = kNumVisibleSteps - kNumStepsDelay;

  ms_SuppressSample = kNoSuppressSample;
  mt_Enable = 1;
}

static void ptplayer_stop() {
  mt_Enable = 0;
  mt_end(&custom);
}

static void handle_steps() {
  // If ptplayer has asynchronously advanced one or more steps then process them.
  while (ms_StepCount > 0) {
    TrackStep* play_step = &g.steps[g.next_step_idx + kNumStepsDelay];

    if (play_step->active_lane) {
      -- g.num_blocks_left;
    }

    // Suppress the sample corresponding to the next block.
    // This will reset if the block is touched or the next step is reached.
    UBYTE next_sample = (play_step + 1)->sample;

    if (next_sample) {
      ms_SuppressSample = next_sample;
    }

    // Recalculate per-frame Z increment to match step speed.
    // Synchronize Z position with ptplayer.
    g.camera_z_inc = ms_camera_z_inc(kBlockGapDepth);
    g.camera_z = g.next_step_idx * kBlockGapDepth;

    ++ g.next_step_idx;
    -- ms_StepCount;

    // After last step stop ptplayer (to prevent looping) and begin fade out.
    if (g.next_step_idx == g.end_step_idx) {
      mt_Enable = 0;
      g.fade_frames = kNumFadeFrames;
      g.running = FALSE;
    }
  }
}

static void handle_input() {
  // Move ball left/right with mouse movement.
  UBYTE mouse_x = read_mouse_x();
  BYTE mouse_delta_x = mouse_x - g.prev_mouse_x;
  g.prev_mouse_x = mouse_x;

  g.ball_x = MAX(-kLaneWidth, MIN(kLaneWidth, g.ball_x + mouse_delta_x));

  // Begin fade out if the escape key is pressed.
  if (g.running && keyboard_state[kKeycodeEsc]) {
    g.fade_frames = kNumFadeFrames;
    g.running = FALSE;
  }
}

static UBYTE read_mouse_x() {
  return custom.joy0dat & JOY0DAT_XALL;
}

static void handle_collision() {
  TrackStep* ball_step = &g.steps[g.next_step_idx + kNumStepsDelay];

  // Check for a block which has not been hit in this row.
  if (ball_step->active_lane && (ball_step->color < kNumBlockColors)) {
    // Left/right lane edges in world X coordinates.
    static WORD bounds[4][2] = {
      {0, 0}, // FIXME: m68k-amigaos-gcc bug? keep this with offset below
      {(-kLaneWidth    ), (-kLaneWidth / 2)},
      {(-kLaneWidth / 2), ( kLaneWidth / 2)},
      {( kLaneWidth / 2), ( kLaneWidth    )},
    };

    WORD* bound = &bounds[ball_step->active_lane][0]; // active_lane in [1..3]

    if ((g.ball_x >= bound[0]) && (g.ball_x <= bound[1])) {
      // Block hit, increase score.
      ++ g.score;

      // Make block darker now that it's been hit.
      ball_step->color += kNumBlockColors;

      // Don't suppress the sample associated with the block.
      ms_SuppressSample = kNoSuppressSample;

      // VU meter back to maximum.
      g.vu_meter_view_z = kFarZ;
    }
  }
}

static void handle_fade() {
  // Combination of (fade_frames, running) determines fade in or out.
  if (g.fade_frames) {
    -- g.fade_frames;

    // Fade all visible colors in/out every other frame.
    // Update colors every frame since there are two copperlists.
    gfx_fade_action(g.running, ((g.fade_frames & 1) == 0) ? TRUE : FALSE);

    // Fade volume to zero during fade out. No fade in.
    if (! g.running) {
      mt_mastervol(&custom, g.fade_frames * (kVolumeMax / kNumFadeFrames));
    }
  }
}

static void handle_gfx() {
  // Calculate world Z for VU meter top and apply per-frame decay.
  ULONG vu_meter_z = g.vu_meter_view_z + g.camera_z;
  g.vu_meter_view_z = MAX(0, g.vu_meter_view_z - kVUMeterZDecay);

  // Score is measured as 1/1000ths of total possible.
  UWORD score_frac = (g.score * 1000) / g.num_blocks_total;

  gfx_update_display(&g.steps[g.next_step_idx], g.ball_x, g.camera_z,
                     g.camera_z_inc, vu_meter_z, score_frac);

  g.camera_z += g.camera_z_inc;
}

static void handle_timeout() {
  if (g.num_blocks_left == 0) {
    // Some MODs do not terminate, or have long periods of silence.
    // Begin a timeout after the last block in the track.
    -- g.timeout_frames;
  }

  if (g.running && (g.timeout_frames == 0)) {
    // After timeout begin fade out.
    g.running = FALSE;
    g.fade_frames = kNumFadeFrames;
  }
}
