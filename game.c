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
#define kBallDXMax 90 // Larger number = sharper movement

static void game_play_loop();
static void ptplayer_start();
static void ptplayer_stop();
static void handle_steps();
static void handle_input();
static UBYTE read_mouse_x();
static BYTE read_joy_dx();
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
  BYTE ball_dx_smoothed[(2 * kBallDXMax) + 1];
  UBYTE prev_mouse_x;
  UWORD score;
  UWORD fade_frames;
  UWORD timeout_frames;
  BOOL running;
} g;

void game_init() {
  // Index into table is desired ball X step in pixels.
  // Result is smoothed/capped per-frame step in pixels.
  for (UWORD i = 0; i < ARRAY_NELEMS(g.ball_dx_smoothed); ++ i) {
    WORD ball_dx = i - kBallDXMax;
    BYTE smooth_dx = (ball_dx + 1) / 2;
    g.ball_dx_smoothed[i] = MAX(-kBallDXMax, MIN(kBallDXMax, smooth_dx));
  }
}

Status game_main_loop() {
  Status status = StatusOK;

  while (TRUE) {
    // Only redraw menu if we left it (started the game successfully).
    if (status == StatusOK) {
      ASSERT(menu_redraw());
      gfx_setup_copperlist(TRUE);
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

    if (status == StatusOK) {
      // Exit the menu and start the game.
      gfx_fade_menu(FALSE);
      gfx_setup_copperlist(FALSE);
      game_play_loop();

      track_free();
    }
    else {
      // Stay on menu and report the error.
      menu_redraw_button((status == StatusInvalidMod) ? "INVALID MOD FILE" : "NOT ENOUGH CHIP RAM");
    }
  }

cleanup:
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
  gfx_allow_copper_blits(TRUE);

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
  gfx_allow_copper_blits(FALSE);
  gfx_wait_vblank();

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
  static BOOL mouse_active = TRUE;

  // Move ball left/right with mouse movement.
  UBYTE mouse_x = read_mouse_x();
  BYTE mouse_dx = mouse_x - g.prev_mouse_x;

  if (mouse_dx != 0) {
    g.prev_mouse_x = mouse_x;
    g.ball_x += mouse_dx;

    mouse_active = TRUE;
  }

  // Move ball left/right with keyboard or joystick movement.
  WORD target_x = g.ball_x;
  BYTE joy_dx = read_joy_dx();

  BOOL key_left = keyboard_state[kKeycodeA];
  BOOL key_right = keyboard_state[kKeycodeD];

  if (key_left || key_right || joy_dx) {
    mouse_active = FALSE;

    if (key_left || (joy_dx < 0)) {
      target_x = -kLaneWidth;
    }
    else if (key_right || (joy_dx > 0)) {
      target_x = kLaneWidth;
    }
  }
  else if (! mouse_active) {
    // Return to center if nothing pressed in keyboard/joystick mode.
    target_x = 0;
  }

  // Calculate frame motion to begin moving ball towards target X.
  // This is slower closer to the end of motion to smooth movement.
  WORD ball_dx = MAX(-kBallDXMax, MIN(kBallDXMax, target_x - g.ball_x));

  if (ball_dx != 0) {
    g.ball_x += g.ball_dx_smoothed[ball_dx + kBallDXMax];
  }

  // Clamp ball X position to within bounds.
  g.ball_x = MAX(-kLaneWidth, MIN(kLaneWidth, g.ball_x));

  // Begin fade out if the escape key is pressed.
  if (g.running && keyboard_state[kKeycodeEsc]) {
    g.fade_frames = kNumFadeFrames;
    g.running = FALSE;
  }
}

static UBYTE read_mouse_x() {
  return custom.joy0dat & JOYxDAT_XALL;
}

static BYTE read_joy_dx() {
  UWORD joy1dat = custom.joy1dat;
  return (joy1dat & JOYxDAT_Y1) ? -1 : ((joy1dat & JOYxDAT_X1) ? 1 : 0);
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
    gfx_fade_play(g.running, ((g.fade_frames & 1) == 0) ? TRUE : FALSE);

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
