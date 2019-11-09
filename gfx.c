#include "gfx.h"
#include "blit.h"
#include "build/ball.h"
#include "build/images.h"
#include "custom.h"
#include "module.h"
#include "system.h"

#include <proto/exec.h>
#include <proto/graphics.h>
#include <stddef.h>

#define kDispWinX 0x81
#define kDispWinY 0x2C
#define kDispFetchX 0x81
#define kDispRes 0x8 // 8 = lores, 4 = hires
#define kDispFetchStart ((kDispFetchX / 0x2) - kDispRes - 8)
#define kDispFetchStop ((kDispFetchX / 0x2) - kDispRes + (0x8 * ((kDispWidth / 0x10) - 1)))
#define kDispFetchExtraWord 0x1 // extra word fetched for horizontal scrolling
#define kDrawHeight ((4 * kDispHeight) / 5)
#define kDrawTop (kDispHeight - kDrawHeight)
#define kDispCopListSizeW (404 + ((kDrawHeight + 1) * 20))
#define kHeaderTextTop (logo_height + ((kDispHdrHeight - logo_height - kFontHeight) / 2))
#define kHeaderTextGap 60
#define kHeaderTextPen 5
#define kHeaderScoreLeft ((kDispWidth - 20) - (6 * kFontSpacing))
#define kPtrSprEdge 0x10
#define kPtrSprOffX -6
#define kPtrSprOffY -1
#define kBallZ (kNearZ + (kNumStepsDelay * kBlockGapDepth))
#define kBlockWidth ((3 * kLaneWidth) / 5)
#define kStripeWidth 4
#define kBorderWidth 17
#define kHeaderPalette 0xB8C, 0x425, 0x94A
#define kFadeActionNumColors 52
#define kBallEdge 0x20
#define kBallAngleLimit (((((kBallNumAngles * 2) + 1) << 11) / 2) - 1)
#define kBallMouseRotateShift 7

// Defined in gfx.asm
extern void update_coplist(UWORD* colors __asm("a2"),
                           UWORD* cop_row __asm("a3"),
                           UWORD* z_incs __asm("a4"),
                           TrackStep* step_near __asm("a6"),
                           ULONG step_frac __asm("d3"),
                           ULONG shift_params __asm("d4"),
                           ULONG vu_meter_z __asm("a0"),
                           UWORD shift_err_inc __asm("a1"),
                           ULONG z __asm("d6"),
                           ULONG loop_counts __asm("d7"));

static Status make_copperlists();
static UWORD* make_copperlist_score(UWORD* cl);
static Status make_view();
static void make_z_incs();
static BOOL fade_common(UWORD* colors_lo,
                        UWORD* colors_hi,
                        UWORD num_colors,
                        BOOL fade_in);
static void update_score(UWORD* cop_list,
                         UWORD score_frac);
static void update_sprite(UWORD* cop_list, UWORD sprite_x, UWORD camera_z_inc);
static void update_sprite_colors(UWORD* cop_list,
                                 ULONG camera_z);

static struct {
  UWORD cop_list_back;
  UWORD cop_list_spr_color_idx;
  UWORD cop_list_menu_end_idx;
  UWORD cop_list_rows_start;
  UWORD cop_list_rows_end;
  UWORD cop_list_score_idx;
  struct cprlist cpr_list;
  struct View view;
  struct ViewPort viewport;
  UWORD z_incs[kDrawHeight];
  UWORD colors[kFadeActionNumColors];
} g;

// Prevent BSS section merging with .data_chip
#define __chip_bss __attribute__((section(".bsschip")))

static UWORD disp_planes[kDispDepth][kDispHeight + kDispColPad][kDispStride / kBytesPerWord] __chip_bss;
static UWORD cop_lists[2][kDispCopListSizeW] __chip_bss;
static UWORD null_spr[2] __chip_bss;

Status gfx_init() {
  Status status = StatusOK;

  ASSERT(make_copperlists());
  ASSERT(make_view());
  make_z_incs();

  system_load_view(&g.view);

cleanup:
  return status;
}

static Status make_copperlists() {
  Status status = StatusOK;

  // Build the first copperlist; the second will be a copy.
  UWORD* cl = cop_lists[0];

  // Placeholder for sprite pointers, updated each frame.
  for (UWORD spr = 0; spr < 8; ++ spr) {
    *(cl ++) = CUSTOM_OFFSET(sprpt[spr]);
    *(cl ++) = WORD_HI(null_spr);
    *(cl ++) = CUSTOM_OFFSET(sprpt[spr]) + kBytesPerWord;
    *(cl ++) = WORD_LO(null_spr);
  }

  // Begin with colors 0-4 set to zero, for menu fade-in.
  for (UWORD i = 0; i < 5; ++ i) {
    *(cl ++) = CUSTOM_OFFSET(color[i]);
    *(cl ++) = 0;
  }

  // Header palette is shared between menu and game, colors 5-7.
  UWORD hdr_pal[] = {kHeaderPalette};

  for (UWORD i = 0; i < ARRAY_NELEMS(hdr_pal); ++ i) {
    *(cl ++) = CUSTOM_OFFSET(color[i + 5]);
    *(cl ++) = hdr_pal[i];
  }

  // Placeholder for sprite colors, updated each frame.
  g.cop_list_spr_color_idx = cl - cop_lists[0];

  for (UWORD i = 0; i < 16; ++ i) {
    *(cl ++) = CUSTOM_OFFSET(color[16 + i]);
    *(cl ++) = 0;
  }

  // Display parameters and bitplanes.
  *(cl ++) = CUSTOM_OFFSET(diwstrt);
  *(cl ++) = (kDispWinY << DIWSTRT_V0_SHF) | kDispWinX;
  *(cl ++) = CUSTOM_OFFSET(diwstop);
  *(cl ++) = ((kDispWinY + kDispHeight - 0x100) << DIWSTOP_V0_SHF) | (kDispWinX + kDispWidth - 0x100);
  *(cl ++) = CUSTOM_OFFSET(diwhigh);
  *(cl ++) = (1 << DIWHIGH_H10_SHF) | (1 << DIWHIGH_V8_SHF);

  *(cl ++) = CUSTOM_OFFSET(ddfstrt);
  *(cl ++) = kDispFetchStart;
  *(cl ++) = CUSTOM_OFFSET(ddfstop);
  *(cl ++) = kDispFetchStop;

  *(cl ++) = CUSTOM_OFFSET(bplcon4);
  *(cl ++) = 0x11;
  *(cl ++) = CUSTOM_OFFSET(fmode);
  *(cl ++) = 0;

  for (UWORD i = 0; i < kDispDepth; ++ i) {
    ULONG plane_start = (ULONG)&disp_planes[i][0][0] - (kDispFetchExtraWord * kBytesPerWord);

    *(cl ++) = CUSTOM_OFFSET(bplpt[i]);
    *(cl ++) = WORD_HI(plane_start);
    *(cl ++) = CUSTOM_OFFSET(bplpt[i]) + kBytesPerWord;
    *(cl ++) = WORD_LO(plane_start);
  }

  // Header uses constant scroll/modulus.
  *(cl ++) = CUSTOM_OFFSET(bpl1mod);
  *(cl ++) = (kDispRowPadW - kDispFetchExtraWord) * kBytesPerWord;
  *(cl ++) = CUSTOM_OFFSET(bpl2mod);
  *(cl ++) = (kDispRowPadW - kDispFetchExtraWord) * kBytesPerWord;

  *(cl ++) = CUSTOM_OFFSET(bplcon0);
  *(cl ++) = BPLCON0_COLOR;
  *(cl ++) = CUSTOM_OFFSET(bplcon1);
  *(cl ++) = 0;
  *(cl ++) = CUSTOM_OFFSET(bplcon3);
  *(cl ++) = 1 << BPLCON3_SPRES_Shf;

  // Enable bitplane DMA at beginning of the display window.
  // The display window we calculated may be larger than the bitmap.
  *(cl ++) = (kDispWinY << 8) | 0x1;
  *(cl ++) = 0xFFFE;
  *(cl ++) = CUSTOM_OFFSET(bplcon0);
  *(cl ++) = BPLCON0_COLOR | (kDispDepth << BPLCON0_BPU_SHF);

  // Delineates end of menu copperlist and continuation of game copperlist.
  g.cop_list_menu_end_idx = cl - cop_lists[0];

  *(cl ++) = 0x1;
  *(cl ++) = 0xFFFE;

  // Begin with colors 1-7 black below header until copper changes them.
  *(cl ++) = ((kDispWinY + kDispHdrHeight - 2) << 8) | 0x1;
  *(cl ++) = 0xFFFE;

  for (UWORD i = 1; i < 8; ++ i) {
    *(cl ++) = CUSTOM_OFFSET(color[i]);
    *(cl ++) = 0;
  }

  // One extra row to set modulus for first draw line.
  UWORD start_y = kDispWinY + kDrawTop - 1;
  UWORD stop_y = kDispWinY + kDispHeight;

  g.cop_list_rows_start = cl - cop_lists[0];

  for (UWORD disp_y = start_y; disp_y < stop_y; ++ disp_y) {
    // Extra wait for high bit rollover.
    if (disp_y == 0x100) {
      *(cl ++) = 0xFFDF;
      *(cl ++) = 0xFFFE;
    }

    *(cl ++) = (disp_y << 8) | 0x1;
    *(cl ++) = 0xFFFE;

    // CPU will update all these values during gfx_update_display.
    // BPLxMOD controls per-scanline scroll for next scanline.
    // BPLCON1 controls per-scanline scroll for this scanline.
    // Colors 1-6 show/hide parts of the track and stripes.
    *(cl ++) = CUSTOM_OFFSET(bpl1mod);
    *(cl ++) = 0;
    *(cl ++) = CUSTOM_OFFSET(bpl2mod);
    *(cl ++) = 0;
    *(cl ++) = CUSTOM_OFFSET(bplcon1);
    *(cl ++) = 0;
    *(cl ++) = CUSTOM_OFFSET(color[1]);
    *(cl ++) = 0;
    *(cl ++) = CUSTOM_OFFSET(color[2]);
    *(cl ++) = 0;
    *(cl ++) = CUSTOM_OFFSET(color[3]);
    *(cl ++) = 0;
    *(cl ++) = CUSTOM_OFFSET(color[4]);
    *(cl ++) = 0;
    *(cl ++) = CUSTOM_OFFSET(color[5]);
    *(cl ++) = 0;
    *(cl ++) = CUSTOM_OFFSET(color[6]);
    *(cl ++) = 0;
  }

  g.cop_list_rows_end = cl - cop_lists[0];

  // Disable bitplane DMA at the end of the display window.
  // This may be larger than the bitmap.
  *(cl ++) = ((kDispWinY + kDispHeight - 0x100) << 8) | 0x1;
  *(cl ++) = 0xFFFE;
  *(cl ++) = CUSTOM_OFFSET(bplcon0);
  *(cl ++) = BPLCON0_COLOR;

  // Insert commands to blit score text to screen.
  g.cop_list_score_idx = cl - cop_lists[0];
  cl = make_copperlist_score(cl);

  *(cl ++) = 0xFFFF;
  *(cl ++) = 0xFFFE;

  ASSERT((cl - cop_lists[0]) == kDispCopListSizeW);

  // Copy the front copperlist we built to the back copperlist.
  CopyMem(cop_lists[0], cop_lists[1], sizeof(cop_lists[0]));

  // Setup copperlist for menu initially.
  gfx_setup_copperlist(TRUE);

cleanup:
  return status;
}

static UWORD* make_copperlist_score(UWORD* cl) {
  APTR dst_row_base = gfx_display_planes() + (kHeaderTextTop * kDispStride);

  // Optionally terminate copperlist to disable copper blits.
  *(cl ++) = 0xFFFF;
  *(cl ++) = 0xFFFE;

  // One blit for each character of the score, up to 5: "XXX.X%"
  for (WORD char_idx = 4; char_idx >= 0; -- char_idx) {
    // No need to blit the period character as it doesn't change.
    if (char_idx == 3) {
      continue;
    }

    // Calculate word-alignment and sub-word shifts for blitter.
    UWORD dst_x = kHeaderScoreLeft + (char_idx * kFontSpacing);
    UWORD start_x_word = dst_x >> 4;
    UWORD end_x_word = ((dst_x + kFontWidth) + 0xF) >> 4;
    UWORD width_words = end_x_word - start_x_word;

    UWORD glyph_idx = (char_idx >= 2) ? 0x10 : 0;
    ULONG src_start = (ULONG)font_planes + (glyph_idx << 1);
    ULONG dst_start = (ULONG)dst_row_base + (start_x_word << 1);
    UWORD shift = dst_x & 0xF;
    UWORD right_word_mask = (width_words == 1 ? 0xF800 : 0);

    // One blit per bitplane.
    for (UWORD plane_idx = 0; plane_idx < kDispDepth; ++ plane_idx) {
      if (kHeaderTextPen & (1 << plane_idx)) {
        // Wait for blitter to finish before issuing commands.
        *(cl ++) = 1;
        *(cl ++) = 0;

        // B = Font data
        // C = Screen bitplane (for region outside mask)
        // D = Screen bitplane
        *(cl ++) = CUSTOM_OFFSET(bltcon0);
        *(cl ++) = (shift << BLTCON0_ASH0_SHF) | BLTCON0_USEB | BLTCON0_USEC | BLTCON0_USED | 0xCA;
        *(cl ++) = CUSTOM_OFFSET(bltcon1);
        *(cl ++) = shift << BLTCON1_BSH0_SHF;
        *(cl ++) = CUSTOM_OFFSET(bltbmod);
        *(cl ++) = (kFontNGlyphs * kBytesPerWord) - (width_words << 1);
        *(cl ++) = CUSTOM_OFFSET(bltcmod);
        *(cl ++) = kDispStride - (width_words << 1);
        *(cl ++) = CUSTOM_OFFSET(bltdmod);
        *(cl ++) = kDispStride - (width_words << 1);

        // Font is 5 pixels wide, mask is 5 bits.
        *(cl ++) = CUSTOM_OFFSET(bltafwm);
        *(cl ++) = 0xF800;
        *(cl ++) = CUSTOM_OFFSET(bltalwm);
        *(cl ++) = right_word_mask;
        *(cl ++) = CUSTOM_OFFSET(bltadat);
        *(cl ++) = 0xFFFF;

        *(cl ++) = CUSTOM_OFFSET(bltbpt);
        *(cl ++) = WORD_HI(src_start);
        *(cl ++) = CUSTOM_OFFSET(bltbpt) + kBytesPerWord;
        *(cl ++) = WORD_LO(src_start);
        *(cl ++) = CUSTOM_OFFSET(bltcpt);
        *(cl ++) = WORD_HI(dst_start);
        *(cl ++) = CUSTOM_OFFSET(bltcpt) + kBytesPerWord;
        *(cl ++) = WORD_LO(dst_start);
        *(cl ++) = CUSTOM_OFFSET(bltdpt);
        *(cl ++) = WORD_HI(dst_start);
        *(cl ++) = CUSTOM_OFFSET(bltdpt) + kBytesPerWord;
        *(cl ++) = WORD_LO(dst_start);

        // Start the blit.
        *(cl ++) = CUSTOM_OFFSET(bltsize);
        *(cl ++) = (kFontHeight << BLTSIZE_H0_SHF) | width_words;
      }

      dst_start += kDispSlice;
    }
  }

  return cl;
}

static void make_z_incs() {
  UWORD prev_z = kNearZ;

  for (UWORD i = 0; i < kDrawHeight; ++ i) {
    // World Z increment from near plane (bottom of screen) to next scanline.
    UWORD draw_y = (kDrawHeight - 1) - i;
    UWORD draw_y_frac = kNearZ + ((draw_y * (kFarZ - kNearZ)) / kDrawHeight);
    UWORD z = ((kNearZ - 1) << 0x10) / draw_y_frac;

    g.z_incs[i] = z - prev_z;
    prev_z = z;
  }
}

static Status make_view() {
  Status status = StatusOK;

  // View is always bound to the first copperlist.
  // The second copperlist is only used inside the game.
  g.cpr_list.start = &cop_lists[0][0];
  g.cpr_list.MaxCount = kDispCopListSizeW / 2;

  InitVPort(&g.viewport);
  g.viewport.DWidth = kDispWidth;
  g.viewport.DHeight = kDispHeight;

  InitView(&g.view);
  g.view.ViewPort = &g.viewport;
  g.view.LOFCprList = &g.cpr_list;

cleanup:
  return status;
}

void gfx_fini() {
  system_unload_view();
}

UBYTE* gfx_display_planes() {
  return (UBYTE*)disp_planes;
}

void gfx_setup_copperlist(BOOL for_menu) {
  UWORD* cl = cop_lists[0];

  if (for_menu) {
    // Set up pointer sprite and disable others.
    for (UWORD spr_idx = 0; spr_idx < 4; ++ spr_idx) {
      APTR spr_addr = (spr_idx == 0) ? pointer_planes : null_spr;

      cl[1 + (spr_idx * 4)] = WORD_HI(spr_addr);
      cl[3 + (spr_idx * 4)] = WORD_LO(spr_addr);
    }
  }

  // Insert copperlist terminator in menu mode, otherwise pass.
  cl[g.cop_list_menu_end_idx] = for_menu ? 0xFFFF : 0x1;
}

void gfx_draw_text(STRPTR text,
                   UWORD max_chars,
                   UWORD left,
                   UWORD top,
                   UWORD color,
                   BOOL replace_bg) {
  APTR dst_row_base = gfx_display_planes() + (top * kDispStride);

  for (UWORD char_idx = 0; text[char_idx] && max_chars; ++ char_idx, -- max_chars) {
    UWORD glyph_idx = MIN(MAX(0x20, text[char_idx]) - 0x20, kFontNGlyphs - 1);
    blit_char((UBYTE*)font_planes, glyph_idx, dst_row_base, left, color, replace_bg);

    left += kFontSpacing;
  }
}

void gfx_draw_logo() {
  // Two bitplane logo, shift colors [1,3] to [5,7].
  //
  // Display plane  Logo plane
  // 0              0
  // 1              1
  // 2              0 | 1

  UWORD logo_plane_stride = logo_width / kBitsPerByte;
  UWORD logo_row_stride = logo_plane_stride * logo_depth;

  for (UWORD i = 0; i < logo_depth; ++ i) {
    UBYTE* logo_plane = (UBYTE*)logo_planes + (i * logo_plane_stride);

    for (UWORD pass = 0; pass < 2; ++ pass) {
      UBYTE* disp_plane = (UBYTE*)&disp_planes[pass == 0 ? i : 2][0][0];

      blit_copy(logo_plane, logo_row_stride, 0, 0,
                disp_plane, kDispStride, 0, 0, logo_width, logo_height, pass == 0, FALSE);
    }
  }
}

void gfx_draw_title(STRPTR title) {
  UWORD title_length = string_length(title);

  UWORD text_width = (title_length * kFontSpacing) - 1;
  UWORD text_left = (kDispWidth / 2) - (text_width / 2);

  UWORD clear_width = kModTitleMaxLen * kFontSpacing - 1;
  UWORD clear_left = (kDispWidth / 2) - (clear_width / 2);

  for (UWORD plane_idx = 0; plane_idx < kDispDepth; ++ plane_idx) {
    if (kHeaderTextPen & (1 << plane_idx)) {
      UBYTE* plane = gfx_display_planes() + (plane_idx * kDispSlice);

      blit_rect(plane, kDispStride, clear_left, kHeaderTextTop,
                NULL, 0, 0, 0, clear_width, kFontHeight, FALSE);
    }
  }

  gfx_draw_text(title, title_length, text_left, kHeaderTextTop, kHeaderTextPen, TRUE);
}

void gfx_init_score() {
  gfx_draw_text("  0.0%", -1, kHeaderScoreLeft, kHeaderTextTop, kHeaderTextPen, TRUE);
}

#define kFadeMenuNumColors 5

void gfx_fade_menu(BOOL fade_in) {
  // Menu body (below header) uses colors 1-4, mouse pointer uses color 18.
  UWORD colors_lo[kFadeMenuNumColors] = {0x000};
  UWORD colors_hi[kFadeMenuNumColors] = {0x425, 0xB4C, 0xB8C, 0x5C5, 0xDB9};
  UWORD* colors = fade_in ? colors_lo : colors_hi;

  // Fade colors to/from black, one step every two frames.
  for (BOOL fading = TRUE; fading; ) {
    fading = fade_common(colors_lo, colors_hi, kFadeMenuNumColors, fade_in);

    for (UWORD i = 0; i < 4; ++ i) {
      cop_lists[0][35 + (i * 2)] = colors[i];
    }

    cop_lists[0][g.cop_list_spr_color_idx + 5] = colors[4];

    for (UWORD i = 0; i < 2; ++ i) {
      WaitTOF();
    }
  }
}

void gfx_fade_play(BOOL fade_in,
                   BOOL delay_fade) {
  static UWORD colors_lo[kFadeActionNumColors] = {0x000};
  static UWORD colors_hi[kFadeActionNumColors] = {
    // Active block colors, from lowest to highest pitch
    0x810, 0x910, 0xA20, 0xB20, 0xB30, 0xC40, 0xD50, 0xD60, 0xD70, 0xE80, 0xE90, 0xFA0,
    // Inactive block colors, from lowest to highest pitch
    0x300, 0x300, 0x310, 0x310, 0x310, 0x410, 0x420, 0x420, 0x420, 0x430, 0x430, 0x530,
    // Track edge background/foreground
    0x303, 0x704,
    // Ball pattern
    0xA00, 0xDBB,
    // Track stripe gradient
    0x505, 0x606, 0x606, 0x707, 0x707, 0x808, 0x909, 0x909,
    0xA0A, 0x909, 0x808, 0x808, 0x707, 0x707, 0x606, 0x505,
    // Background gradient
    0x000, 0x001, 0x002, 0x003, 0x004, 0x005, 0x006, 0x007,
  };

  // Delay allows copperlist to be updated without fade.
  // This ensures previous fade is reflected in the second copperlist.
  if (! delay_fade) {
    fade_common(fade_in ? g.colors : colors_lo,
                fade_in ? colors_hi : g.colors,
                kFadeActionNumColors, fade_in);
  }

  // Update background gradient in the copperlist.
  // This is not otheriwse updated by gfx_update_display.
  UWORD* cop_rows = &cop_lists[g.cop_list_back][g.cop_list_rows_start];

  UWORD start_y = kDispWinY + kDrawTop;
  UWORD stop_y = kDispWinY + kDispHeight;
  UWORD cop_rows_offset = 19;

  for (UWORD i = 0; i < kDrawHeight; ++ i) {
    UWORD disp_y = i + kDispWinY + kDrawTop;

    cop_rows_offset += 20 + (disp_y == 0x100 ? 2 : 0);
    cop_rows[cop_rows_offset] = g.colors[44 + ((kDrawTop + i) >> 5)];
  }
}

static BOOL fade_common(UWORD* colors_lo,
                        UWORD* colors_hi,
                        UWORD num_colors,
                        BOOL fade_in) {
  BOOL fading = FALSE;

  for (UWORD i = 0; i < num_colors; ++ i) {
    if (colors_lo[i] != colors_hi[i]) {
      // RGB components are incremented until reaching the target.
      UWORD delta = colors_hi[i] - colors_lo[i];
      UWORD incr =
        ((delta & 0x111) >> 0) |
        ((delta & 0x222) >> 1) |
        ((delta & 0x444) >> 2) |
        ((delta & 0x888) >> 3);

      if (fade_in) {
        colors_lo[i] += incr;
      }
      else {
        colors_hi[i] -= incr;
      }

      fading = TRUE;
    }
  }

  return fading;
}

void gfx_clear_body() {
  for (UWORD i = 0; i < kDispDepth; ++ i) {
    blit_rect(&disp_planes[i][0][0], kDispStride, 0, kDrawTop,
              NULL, 0, 0, 0, kDispStride * kBitsPerByte, kDrawHeight, FALSE);
  }
}

#define kNumTrackLines (ARRAY_NELEMS(near_sx))
#define kDrawCenterX ((kDispStride * kBitsPerByte) / 2)

void gfx_draw_track() {
  gfx_clear_body();

  // Screen space X near coordinates for lines making up the track.
  WORD near_sx[] = {
    (-kLaneWidth / 2) - (kStripeWidth / 2),      // Left lane stripe left
    (-kLaneWidth / 2) + (kStripeWidth / 2),      // Left lane stripe right
    ( kLaneWidth / 2) - (kStripeWidth / 2),      // Right lane stripe left
    ( kLaneWidth / 2) + (kStripeWidth / 2),      // Right lane stripe right
    (-(3 * kLaneWidth) / 2) - kBorderWidth,      // Left border left
    (-(3 * kLaneWidth) / 2),                     // Left border right
    ( (3 * kLaneWidth) / 2),                     // Right lane left
    ( (3 * kLaneWidth) / 2) + kBorderWidth,      // Right lane right
    -kBlockWidth / 2 - kLaneWidth,               // Left lane blocks left
     kBlockWidth / 2 - kLaneWidth,               // Left lane blocks right
    -kBlockWidth / 2,                            // Middle lane blocks left
     kBlockWidth / 2,                            // Middle lane blocks right
    -kBlockWidth / 2 + kLaneWidth,               // Right lane blocks left
     kBlockWidth / 2 + kLaneWidth,               // Right lane blocks right
    -kDrawCenterX,                               // Left screen edge
     kDrawCenterX - 1,                           // Right screen edge
    (-(3 * kLaneWidth) / 2) - kBorderWidth - 1,  // Left border edge
    ( (3 * kLaneWidth) / 2) + kBorderWidth + 1,  // Right border edge
  };

  // Project near coordinates to far plane.
  WORD far_sx[kNumTrackLines];

  for (UWORD i = 0; i < kNumTrackLines - 2; ++ i) {
    far_sx[i] = near_sx[i];

    if (i < (kNumTrackLines - 4)) {
      far_sx[i] /= kFarNearRatio;
    }
  }

  // Outer border edges cannot be projected accurately, just offset from inner.
  far_sx[kNumTrackLines - 2] = far_sx[4] - 1;
  far_sx[kNumTrackLines - 1] = far_sx[7] + 1;

  // 1 = stripe, 2 = left blocks, 3 = middle blocks, 4 = right blocks, 5 = border, 6 = background
  UWORD colors[] = {1, 1, 1, 1, 5, 5, 5, 5, 2, 2, 3, 3, 4, 4, 6, 6, 6, 6};

  for (UWORD plane_idx = 0; plane_idx < kDispDepth; ++ plane_idx) {
    UWORD* plane = &disp_planes[plane_idx][0][0];

    // Draw lines bordering filled regions in the corresponding color.
    for (UWORD i = 0; i < ARRAY_NELEMS(near_sx); ++ i) {
      if (colors[i] & (1 << plane_idx)) {
        blit_line(plane, kDispStride, far_sx[i] + kDrawCenterX,
                  kDrawTop, near_sx[i] + kDrawCenterX, kDispHeight - 1);
      }
    }

    // Fill between the lines in each plane.
    blit_fill(plane, kDispStride, 0, kDrawTop, kDispStride * kBitsPerByte, kDrawHeight);
  }
}

void gfx_update_pointer(UWORD pointer_x,
                        UWORD pointer_y) {
  UWORD hstart = (kDispWinX & ~1) + (pointer_x + kPtrSprOffX);
  UWORD vstart = kDispWinY + (pointer_y + kPtrSprOffY);
  UWORD vstop = vstart + kPtrSprEdge;

  UWORD ctl_word0 = ((vstart & 0xFF) << SPRxPOS_SV0_SHF)
                  | (((hstart >> 1) & 0xFF) << SPRxPOS_SH1_SHF);
  UWORD ctl_word1 = ((vstop & 0xFF) << SPRxCTL_EV0_SHF)
                  | ((vstart >> 8) << SPRxCTL_SV8_SHF)
                  | ((vstop >> 8) << SPRxCTL_EV8_SHF)
                  | ((hstart & 0x1) << SPRxCTL_SH0_SHF);;

  *(ULONG *)pointer_planes = (ctl_word0 << kBitsPerWord) | ctl_word1;
}

void gfx_update_display(TrackStep *step_near,
                        WORD ball_x,
                        ULONG camera_z,
                        UWORD camera_z_inc,
                        ULONG vu_meter_z,
                        UWORD score_frac) {
  // Work on the back (non-displayed) copperlist.
  UWORD* cop_list = cop_lists[g.cop_list_back];
  g.cop_list_back ^= 1;

  // Project ball world X position into screen space.
  WORD ball_sx = (ball_x * (kNumVisibleSteps - kNumStepsDelay)) / kNumVisibleSteps;

  // Camera will move a percentage of the ball position.
  WORD camera_x = (ball_sx * 74) / 100;

  // Sprite will move the remainder of the ball position.
  WORD sprite_x = (kDispWidth / 2) + (ball_sx - camera_x);

  update_score(cop_list, score_frac);
  update_sprite(cop_list, sprite_x, camera_z_inc);
  update_sprite_colors(cop_list, camera_z);

  // Scanlines shift by the camera screen X position.
  // From near plane (no projection) to far plane (projected by near:far ratio).
  WORD shift_start_x = - camera_x;
  WORD shift_end_x = - camera_x / kFarNearRatio;

  // Calculate per-scanline shift increment as a fixed-point fraction.
  // When error exceeds 1 the scanline will shift one integral position.
  WORD slope = ((shift_end_x - shift_start_x) << 0xE) / kDrawHeight;
  UWORD shift_err_inc = ABS(slope);
  WORD shift_x_inc = (slope < 0) ? -1 : ((slope > 0) ? 1 : 0);
  ULONG shift_params = (shift_start_x & 0xFFFF) | (shift_x_inc << 0x10);

  // Update routine begins at near plane (bottom scanline) and advances to far (top).
  TrackStep* step = step_near;
  ULONG z_start = camera_z + kNearZ;
  UWORD* cop_row_end = &cop_list[g.cop_list_rows_end];

  // Z increment added at each scanline, step advances when exceeds kBlockGapDepth.
  UWORD z_since_step = camera_z % kBlockGapDepth;

  // Bottom segment of display, then extra wait at 0x100, then top segment.
  ULONG loop_counts =
    ((0xFF - (kDispWinY + kDispHeight - kDrawHeight)) << 0x10) |
    (kDispWinY + kDispHeight - 1 - 0x100);

  update_coplist(g.colors, cop_row_end, g.z_incs, step_near, z_since_step, shift_params,
                 vu_meter_z, shift_err_inc, z_start, loop_counts);

  // Bind the new copperlist for next frame.
  custom.cop1lc = (ULONG)cop_list;
}

static void update_score(UWORD* cop_list,
                         UWORD score_frac) {
  UWORD* cl = &cop_list[g.cop_list_score_idx] + 21;

  // Four digits for percentage: XXX.X%
  for (WORD char_idx = 0; char_idx < 4; ++ char_idx) {
    UWORD glyph_idx = 0;

    // Draw digits until nothing left.
    // Always draw at least 0.0%.
    if (score_frac || char_idx < 2) {
      glyph_idx = 0x10 + (score_frac % 10);
      score_frac /= 10;
    }

    ULONG src_start = (ULONG)font_planes + (glyph_idx << 1);

    for (UWORD plane_idx = 0; plane_idx < kDispDepth; ++ plane_idx) {
      if (kHeaderTextPen & (1 << plane_idx)) {
        // Update blitter source address in copperlist.
        cl[0] = WORD_HI(src_start);
        cl[2] = WORD_LO(src_start);

        cl += 32;
      }
    }
  }
}

static void update_sprite(UWORD* cop_list,
                          UWORD sprite_x,
                          UWORD camera_z_inc) {
  // Ball rotates with left/right movement, back upright without movement.
  // Rate of rotation is proportional to camera Z speed.
  static UWORD last_sprite_x = 0;
  WORD sprite_dx = sprite_x - last_sprite_x;
  last_sprite_x = sprite_x;

  static WORD sprite_angle = 0;
  WORD sprite_angle_bound = (sprite_dx < 0 ? -kBallAngleLimit : (sprite_dx > 0 ? kBallAngleLimit : 0));
  UWORD sprite_angle_inc = (camera_z_inc << 1) + (ABS(sprite_dx) << kBallMouseRotateShift);

  if ((sprite_dx < 0) || (sprite_dx == 0 && sprite_angle > 0)) {
    sprite_angle = MAX(sprite_angle_bound, sprite_angle - sprite_angle_inc);
  }
  else {
    sprite_angle = MIN(sprite_angle_bound, sprite_angle + sprite_angle_inc);
  }

  // Project ball Z position to calculate screen Y position.
  UWORD z_frac = ((kNearZ - 1) << 0x10) / kBallZ;
  UWORD y = ((z_frac - kNearZ) * kDrawHeight) / (0x10000 - kNearZ)
          + (kDispHeight - kDrawHeight) - (kBallEdge / 2);

  // Convert screen coordinates to display coordinates, then control words.
  UWORD hstart_left = (kDispWinX & ~1) + sprite_x - 8;
  UWORD vstart = kDispWinY + y + (kBallEdge / 2);
  UWORD vstop = vstart + kBallEdge;

  UWORD spr_ctl_0 = (vstart & 0xFF) << SPRxPOS_SV0_SHF;
  UWORD spr_ctl_1 = ((vstop & 0xFF) << SPRxCTL_EV0_SHF) | ((vstart >> 8) << SPRxCTL_SV8_SHF) | ((vstop >> 8) << SPRxCTL_EV8_SHF);

  // Update sprite control words and pointers in the copperlist.
  UWORD spr_frame = (sprite_angle + (kBallAngleLimit + 1)) >> 11;

  for (UWORD spr_idx = 0; spr_idx < 4; ++ spr_idx) {
    UWORD* spr = &ball_sprs[spr_frame][spr_idx][0][0];
    UWORD hstart = hstart_left + ((spr_idx & 2) ? 8 : -8);

    spr[0] = spr_ctl_0 | (((hstart >> 1) & 0xFF) << SPRxPOS_SH1_SHF);
    spr[1] = spr_ctl_1 | ((hstart & 0x1) << SPRxCTL_SH0_SHF) | ((spr_idx & 1) << SPRxCTL_ATT_SHF);

    cop_list[(spr_idx * 4) + 1] = WORD_HI(spr);
    cop_list[(spr_idx * 4) + 3] = WORD_LO(spr);
  }
}

#define kColorCycleZShift 9
#define kColorCycleNum 14

static void update_sprite_colors(UWORD* cop_list,
                                 ULONG camera_z) {
  // 14 bits representing the current color pattern.
  // Initially this is %00000001111111.
  // As color cycling advances these bits are rotated to the right.
  static UWORD color_mask = 0x7F;

  UWORD color1 = g.colors[26]; // Ball pattern color 1
  UWORD color2 = g.colors[27]; // Ball pattern color 2

  // Update sprite colors in copperlist by mapping bit pattern to colors 1 and 2.
  UWORD* cop_list_colors = &cop_list[g.cop_list_spr_color_idx + 3];

  for (UWORD color_idx = 0; color_idx < kColorCycleNum; ++ color_idx) {
    cop_list_colors[color_idx << 1] = (color_mask & (1 << color_idx)) ? color1 : color2;
  }

  // Color cycling advances at the rate of camera movement.
  // Colors can move at most 3 positions per frame (beyond this aliases).
  UWORD cycle_count = camera_z >> kColorCycleZShift;

  static UWORD last_cycle_count = 0;

  if (camera_z == 0) {
    last_cycle_count = 0;
  }

  UWORD cycle_shift = MAX(0, MIN(3, cycle_count - last_cycle_count));
  last_cycle_count = cycle_count;

  // Rotate the bitmask by the number of calculated positions.
  color_mask = ((color_mask << cycle_shift) | (color_mask >> (kColorCycleNum - cycle_shift)));
  color_mask &= (1 << kColorCycleNum) - 1;
}

void gfx_wait_vblank() {
  ULONG mask = (VPOSR_V8 << 0x10) | VHPOSR_VALL;
  ULONG compare = ((kDispWinY | 0x100) + 1) << 0x8;
  ULONG vpos_vhpos;

  do {
    vpos_vhpos = *(volatile ULONG*)&custom.vposr;
  } while ((vpos_vhpos & mask) >= compare);

  do {
    vpos_vhpos = *(volatile ULONG*)&custom.vposr;
  } while ((vpos_vhpos & mask) < compare);
}

void gfx_wait_blit() {
  // Give blitter priority while we're waiting.
  // Also takes care of dummy DMACON access to work around Agnus bug.
  custom.dmacon = DMACON_SET | DMACON_BLITPRI;
  while (custom.dmaconr & DMACONR_BBUSY);
  custom.dmacon = DMACON_BLITPRI;
}

void gfx_allow_copper_blits(BOOL allow) {
  for (UWORD i = 0; i < 2; ++ i) {
    cop_lists[i][g.cop_list_score_idx] = (allow ? 0x1 : 0xFFFF);
  }
}
