#include "gfx.h"
#include "blit.h"
#include "build/ball.h"
#include "build/images.h"
#include "custom.h"
#include "menu.h"
#include "module.h"

#include <graphics/gfxmacros.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <stddef.h>

#define kDispWinX 0x81
#define kDispWinY 0x2C
#define kDispFetchX 0x81
#define kDispRes 0x8 // 8 = lores, 4 = hires
#define kDispFetchStart ((kDispFetchX / 0x2) - kDispRes - 8)
#define kDispFetchStop ((kDispFetchX / 0x2) - kDispRes + (0x8 * ((kDispWidth / 0x10) - 1)))
#define kDrawHeight 204// ((4 * kDispHeight) / 5)
#define kDrawTop (kDispHeight - kDrawHeight)
#define kDispCopSizeWords (384 + ((kDrawHeight + 1) * 20))
#define kHeaderTextTop (logo_height + ((kDispHdrHeight - logo_height - kFontHeight) / 2))
#define kHeaderTextGap 60
#define kHeaderTextPen 5
#define kPtrSprEdge 0x10
#define kPtrSprOffX -6
#define kPtrSprOffY -1
#define kPlayerZ (kNearZ + (kNumStepsDelay * kBlockGapDepth))
#define kBlockWidth ((3 * kLaneWidth) / 5)
#define kStripeWidth 4
#define kBorderWidth 15
#define kHeaderPalette 0xB8C, 0x425, 0x94A
#define kFadeActionNumColors 52
#define kBallEdge 0x20
#define kBallAngleLimit (((((kBallNumAngles * 2) + 1) << 11) / 2) - 1)

extern void update_coplist(UWORD* colors __asm("a2"),
                           UWORD* cop_row __asm("a3"),
                           WORD* z_incs __asm("a4"),
                           TrackStep* step_near __asm("a6"),
                           ULONG step_frac __asm("d3"),
                           ULONG shift_params __asm("d4"),
                           ULONG vu_meter_z __asm("a0"),
                           UWORD shift_err_inc __asm("a1"),
                           ULONG z __asm("d6"),
                           ULONG loop_counts __asm("d7"));

static void make_bitmap();
static Status make_screen();
static Status make_window();
static Status make_copperlists();
static UWORD* make_copperlist_score(UWORD* cl);
static Status make_view();
static void make_z_incs();
static void get_display_window(struct ViewPort* viewport, UWORD* diwstrt, UWORD* diwstop, UWORD* diwhigh);
static void update_sprite(UWORD* cop_list, UWORD player_x, UWORD camera_z_inc);
static void update_sprite_colors(UWORD* cop_list,
                                 ULONG camera_z);
static void update_score(UWORD* cop_list,
                         UWORD score_frac);
static BOOL fade_common(UWORD* colors_lo,
                        UWORD* colors_hi,
                        UWORD num_colors,
                        BOOL fade_in);

static struct {
  struct BitMap bitmap;
  struct Screen* screen;
  struct Window* window;
  struct cprlist cpr_list;
  UWORD cop_list_back;
  UWORD cop_list_spr_idx;
  UWORD cop_list_rows_start;
  UWORD cop_list_rows_end;
  UWORD cop_list_score_idx;
  struct View view;
  UWORD z_incs[kDrawHeight];
  UWORD colors[kFadeActionNumColors];
} g;

// Prevent BSS section merging with .data_chip
#define __chip_bss __attribute__((section(".bsschip")))

static UWORD disp_planes[kDispDepth][kDispHeight + kDispColPad][kDispStride / kBytesPerWord] __chip_bss;
static UWORD cop_lists[2][kDispCopSizeWords] __chip_bss;
static UWORD null_spr[2] __chip_bss;

Status gfx_init() {
  Status status = StatusOK;

  make_bitmap();
  ASSERT(make_screen());
  ASSERT(make_window());
  ASSERT(make_copperlists());
  ASSERT(make_view());
  make_z_incs();

cleanup:
  return status;
}

void gfx_fini() {
  if (g.window) {
    CloseWindow(g.window);
    g.window = NULL;
  }

  if (g.screen) {
    CloseScreen(g.screen);
    g.screen = NULL;
  }
}

struct Window* gfx_window() {
  return g.window;
}

UBYTE* gfx_display_planes() {
  return (UBYTE*)disp_planes;
}

UBYTE* gfx_font_plane() {
  return (UBYTE*)font_planes;
}

struct View* gfx_view() {
  return &g.view;
}

void gfx_draw_text(STRPTR text,
                   UWORD max_chars,
                   UWORD left,
                   UWORD top,
                   UWORD color,
                   BOOL replace_bg) {
  APTR dst_row_base = gfx_display_planes() + (top * kDispStride);

  for (UWORD char_idx = 0; text[char_idx] && max_chars; ++ char_idx, -- max_chars) {
    UWORD glyph_idx = MIN(text[char_idx] - 0x20, kFontNGlyphs - 1);
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
  UWORD title_length;
  for (title_length = 0; title_length < 20 && title[title_length]; ++ title_length) {}

  UWORD text_right = (kDispWidth - kHeaderTextGap) / 2;
  UWORD text_width = title_length * kFontSpacing - 1;
  UWORD text_left = text_right - text_width;

  UWORD max_width = kModTitleMaxLen * kFontSpacing - 1;
  UWORD clear_left = text_right - max_width;
  UWORD clear_width = text_left - clear_left;

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
  UWORD left = (kDispWidth + kHeaderTextGap) / 2;
  gfx_draw_text("  0.0%", -1, left, kHeaderTextTop, kHeaderTextPen, TRUE);
}

#define kFadeMenuNumColors 5

void gfx_fade_menu(BOOL fade_in) {
  static UWORD color_indices[kFadeMenuNumColors] = {1, 2, 3, 4, 18};

  UWORD colors_lo[kFadeMenuNumColors] = {0x000};
  UWORD colors_hi[kFadeMenuNumColors] = {0x425, 0xB4C, 0xB8C, 0x5C5, 0xDB9};
  UWORD* colors = fade_in ? colors_lo : colors_hi;

  for (BOOL fading = TRUE; fading; ) {
    fading = fade_common(colors_lo, colors_hi, kFadeMenuNumColors, fade_in);

    for (UWORD i = 0; i < kFadeMenuNumColors; ++ i) {
      UWORD color = colors[i];
      SetRGB4(&g.screen->ViewPort, color_indices[i], color >> 8, (color >> 4) & 0xF, color & 0xF);
    }

    for (UWORD i = 0; i < 2; ++ i) {
      gfx_wait_vblank();
    }
  }
}

void gfx_fade_action(BOOL fade_in,
                     BOOL delay_fade) {
  static UWORD colors_lo[kFadeActionNumColors] = {0x000};
  static UWORD colors_hi[kFadeActionNumColors] = {
    0x810, 0x910, 0xA20, 0xB20, 0xB30, 0xC40, 0xD50, 0xD60, 0xD70, 0xE80, 0xE90, 0xFA0,
    0x300, 0x300, 0x310, 0x310, 0x310, 0x410, 0x420, 0x420, 0x420, 0x430, 0x430, 0x530,
    0x303, 0x909, 0xDAA, 0x704,
    0x505, 0x606, 0x606, 0x707, 0x707, 0x808, 0x909, 0x909,
    0xA0A, 0x909, 0x808, 0x808, 0x707, 0x707, 0x606, 0x505,
    0x000, 0x001, 0x002, 0x003, 0x004, 0x005, 0x006, 0x007,
  };

  if (! delay_fade) {
    fade_common(fade_in ? g.colors : colors_lo,
                fade_in ? colors_hi : g.colors,
                kFadeActionNumColors, fade_in);
  }

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

  WORD near_sx[] = {
    (-kLaneWidth / 2) - (kStripeWidth / 2),
    (-kLaneWidth / 2) + (kStripeWidth / 2),
    ( kLaneWidth / 2) - (kStripeWidth / 2),
    ( kLaneWidth / 2) + (kStripeWidth / 2),
    (-(3 * kLaneWidth) / 2) - kBorderWidth,
    (-(3 * kLaneWidth) / 2),
    ( (3 * kLaneWidth) / 2),
    ( (3 * kLaneWidth) / 2) + kBorderWidth,
    -kBlockWidth / 2 - kLaneWidth,
     kBlockWidth / 2 - kLaneWidth,
    -kBlockWidth / 2,
     kBlockWidth / 2,
    -kBlockWidth / 2 + kLaneWidth,
     kBlockWidth / 2 + kLaneWidth,
    -kDrawCenterX,
    kDrawCenterX - 1,
    (-(3 * kLaneWidth) / 2) - kBorderWidth - 1,
    ( (3 * kLaneWidth) / 2) + kBorderWidth + 1,
  };

  WORD far_sx[kNumTrackLines];

  for (UWORD i = 0; i < kNumTrackLines - 2; ++ i) {
    far_sx[i] = near_sx[i];

    if (i < (kNumTrackLines - 4)) {
      far_sx[i] /= kFarNearRatio;
    }
  }

  far_sx[kNumTrackLines - 2] = far_sx[4] - 1;
  far_sx[kNumTrackLines - 1] = far_sx[7] + 1;

  UWORD colors[] = {1, 1, 1, 1, 5, 5, 5, 5, 2, 2, 3, 3, 4, 4, 6, 6, 6, 6};

  for (UWORD plane_idx = 0; plane_idx < kDispDepth; ++ plane_idx) {
    UWORD* plane = &disp_planes[plane_idx][0][0];

    for (UWORD i = 0; i < ARRAY_NELEMS(near_sx); ++ i) {
      if (colors[i] & (1 << plane_idx)) {
        blit_line(plane, kDispStride, far_sx[i] + kDrawCenterX,
                  kDrawTop, near_sx[i] + kDrawCenterX, kDispHeight - 1);
      }
    }

    blit_fill(plane, kDispStride, 0, kDrawTop, kDispStride * kBitsPerByte, kDrawHeight);
  }
}

void gfx_update_display(TrackStep *step_near,
                        WORD player_x,
                        ULONG camera_z,
                        UWORD camera_z_inc,
                        ULONG vu_meter_z,
                        UWORD score_frac) {
  UWORD* cop_list = cop_lists[g.cop_list_back];
  g.cop_list_back ^= 1;

  update_score(cop_list, score_frac);

  WORD player_sx = (player_x * (kNumVisibleSteps - kNumStepsDelay)) / kNumVisibleSteps;
  WORD camera_x = (player_sx * 74) / 100;

  update_sprite(cop_list, (kDispWidth / 2) + (player_sx - camera_x), camera_z_inc);
  update_sprite_colors(cop_list, camera_z);

  WORD shift_start_x = - camera_x;
  WORD shift_end_x = - camera_x / kFarNearRatio;
  WORD shift_x = shift_start_x;
  WORD shift_err = 0;
  WORD slope = ((shift_end_x - shift_start_x) << 0xE) / kDrawHeight;
  UWORD shift_err_inc = ABS(slope);
  WORD shift_x_inc = (slope < 0) ? -1 : ((slope > 0) ? 1 : 0);
  WORD prev_shift_w = 0;
  ULONG z = camera_z + kNearZ;

  TrackStep* step = step_near;
  UWORD step_frac = camera_z % kBlockGapDepth; // FIXME: overflow?
  UWORD active_color_idx = step->color;

  UWORD* z_inc_ptr = g.z_incs;
  UWORD* cop_row = &cop_list[g.cop_list_rows_end];

  ULONG loop_counts =
    ((0xFF - (kDispWinY + kDispHeight - kDrawHeight)) << 0x10) |
    (kDispWinY + kDispHeight - 1 - 0x100);

  ULONG shift_params = (shift_start_x & 0xFFFF) | (shift_x_inc << 0x10);
  update_coplist(g.colors, cop_row, g.z_incs, step_near, step_frac, shift_params,
                 vu_meter_z, shift_err_inc, z, loop_counts);

  custom.cop1lc = (ULONG)cop_list;
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

static void make_bitmap() {
  g.bitmap = (struct BitMap) {
    .BytesPerRow = kDispStride,
    .Rows = kDispHeight,
    .Flags = 0,
    .Depth = kDispDepth,
    .pad = 0,
  };

  UBYTE* planes = gfx_display_planes();

  for (UWORD i = 0; i < kDispDepth; ++ i) {
    g.bitmap.Planes[i] = planes + (i * kDispSlice);
  }
}

static Status make_screen() {
  Status status = StatusOK;

  struct NewScreen new_screen = {
    .LeftEdge = 0,
    .TopEdge = 0,
    .Width = kDispWidth,
    .Height = kDispHeight,
    .Depth = kDispDepth,
    .DetailPen = 0,
    .BlockPen = 0,
    .ViewModes = 0,
    .Type = CUSTOMSCREEN | CUSTOMBITMAP | SCREENQUIET,
    .Font = NULL,
    .DefaultTitle = NULL,
    .Gadgets = NULL,
    .CustomBitMap = &g.bitmap,
  };

  ASSERT(g.screen = OpenScreen(&new_screen));

  ShowTitle(g.screen, FALSE);

  UWORD palette[1 << kDispDepth] = {0, 0, 0, 0, 0, kHeaderPalette};
  LoadRGB4(&g.screen->ViewPort, palette, ARRAY_NELEMS(palette));
  SetRGB4(&g.screen->ViewPort, 17, 0, 0, 0);

cleanup:
  return status;
}

static Status make_window() {
  Status status = StatusOK;

  struct NewWindow new_window = {
    .LeftEdge = 0,
    .TopEdge = 0,
    .Width = kDispWidth,
    .Height = kDispHeight,
    .DetailPen = 0,
    .BlockPen = 0,
    .IDCMPFlags = IDCMP_VANILLAKEY | IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE | IDCMP_DISKREMOVED,
    .Flags = WFLG_SUPER_BITMAP | WFLG_BACKDROP | WFLG_BORDERLESS
           | WFLG_ACTIVATE | WFLG_RMBTRAP | WFLG_REPORTMOUSE,
    .FirstGadget = NULL,
    .CheckMark = NULL,
    .Title = NULL,
    .Screen = g.screen,
    .BitMap = &g.bitmap,
    .MinWidth = kDispWidth,
    .MinHeight = kDispHeight,
    .MaxWidth = kDispWidth,
    .MaxHeight = kDispHeight,
    .Type = CUSTOMSCREEN,
  };

  ASSERT(g.window = OpenWindow(&new_window));

  SetPointer(g.window, pointer_planes, kPtrSprEdge, kPtrSprEdge, kPtrSprOffX, kPtrSprOffY);

cleanup:
  return status;
}

static Status make_copperlists() {
  Status status = StatusOK;

  UWORD diwstrt = (kDispWinY << DIWSTRT_V0_SHF) | kDispWinX;
  UWORD diwstop = ((kDispWinY + kDispHeight - 0x100) << DIWSTOP_V0_SHF) | (kDispWinX + kDispWidth - 0x100);
  UWORD diwhigh = 0x2100;

  get_display_window(&g.screen->ViewPort, &diwstrt, &diwstop, &diwhigh);

  UWORD* cl = cop_lists[0];

  for (UWORD spr = 0; spr < 8; ++ spr) {
    *(cl ++) = CUSTOM_OFFSET(sprpt[spr]);
    *(cl ++) = WORD_HI(null_spr);
    *(cl ++) = CUSTOM_OFFSET(sprpt[spr]) + kBytesPerWord;
    *(cl ++) = WORD_LO(null_spr);
  }

  UWORD hdr_pal[] = {kHeaderPalette};

  for (UWORD i = 0; i < ARRAY_NELEMS(hdr_pal); ++ i) {
    *(cl ++) = CUSTOM_OFFSET(color[i + 5]);
    *(cl ++) = hdr_pal[i];
  }

  g.cop_list_spr_idx = cl - cop_lists[0];

  for (UWORD i = 0; i < 16; ++ i) {
    *(cl ++) = CUSTOM_OFFSET(color[16 + i]);
    *(cl ++) = 0;
  }

  *(cl ++) = CUSTOM_OFFSET(diwstrt);
  *(cl ++) = diwstrt;
  *(cl ++) = CUSTOM_OFFSET(diwstop);
  *(cl ++) = diwstop;
  *(cl ++) = CUSTOM_OFFSET(diwhigh);
  *(cl ++) = diwhigh;

  *(cl ++) = CUSTOM_OFFSET(ddfstrt);
  *(cl ++) = kDispFetchStart;
  *(cl ++) = CUSTOM_OFFSET(ddfstop);
  *(cl ++) = kDispFetchStop;

  for (UWORD i = 0; i < kDispDepth; ++ i) {
    ULONG plane_start = (ULONG)&disp_planes[i][0][0] - kBytesPerWord;

    *(cl ++) = CUSTOM_OFFSET(bplpt[i]);
    *(cl ++) = WORD_HI(plane_start);
    *(cl ++) = CUSTOM_OFFSET(bplpt[i]) + kBytesPerWord;
    *(cl ++) = WORD_LO(plane_start);
  }

  *(cl ++) = CUSTOM_OFFSET(bpl1mod);
  *(cl ++) = (kDispRowPadW * 2) - 2; // FIXME: macro
  *(cl ++) = CUSTOM_OFFSET(bpl2mod);
  *(cl ++) = (kDispRowPadW * 2) - 2;

  *(cl ++) = CUSTOM_OFFSET(bplcon0);
  *(cl ++) = BPLCON0_COLOR;
  *(cl ++) = CUSTOM_OFFSET(bplcon1);
  *(cl ++) = 0;

  *(cl ++) = (kDispWinY << 8) | 0x1;
  *(cl ++) = 0xFFFE;
  *(cl ++) = CUSTOM_OFFSET(bplcon0);
  *(cl ++) = BPLCON0_COLOR | (kDispDepth << BPLCON0_BPU_SHF);

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
    if (disp_y == 0x100) {
      *(cl ++) = 0xFFDF;
      *(cl ++) = 0xFFFE;
    }

    *(cl ++) = (disp_y << 8) | 0x1;
    *(cl ++) = 0xFFFE;

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

  *(cl ++) = ((kDispWinY + kDispHeight - 0x100) << 8) | 0x1;
  *(cl ++) = 0xFFFE;
  *(cl ++) = CUSTOM_OFFSET(bplcon0);
  *(cl ++) = BPLCON0_COLOR;

  g.cop_list_score_idx = cl - cop_lists[0];

  cl = make_copperlist_score(cl);

  *(cl ++) = 0xFFFF;
  *(cl ++) = 0xFFFE;

  ASSERT((cl - cop_lists[0]) == kDispCopSizeWords);

  CopyMem(cop_lists[0], cop_lists[1], sizeof(cop_lists[0]));

cleanup:
  return status;
}

static UWORD* make_copperlist_score(UWORD* cl) {
  APTR dst_row_base = gfx_display_planes() + (kHeaderTextTop * kDispStride);
  UWORD left = (kDispWidth + kHeaderTextGap) / 2;

  for (WORD char_idx = 4; char_idx >= 0; -- char_idx) {
    if (char_idx == 3) {
      continue;
    }

    UWORD dst_x = left + (char_idx * kFontSpacing);
    UWORD start_x_word = dst_x >> 4;
    UWORD end_x_word = ((dst_x + kFontWidth) + 0xF) >> 4;
    UWORD width_words = end_x_word - start_x_word;

    UWORD glyph_idx = (char_idx >= 2) ? 0x10 : 0;
    ULONG src_start = (ULONG)font_planes + (glyph_idx << 1);
    ULONG dst_start = (ULONG)dst_row_base + (start_x_word << 1);
    UWORD shift = dst_x & 0xF;
    UWORD right_word_mask = (width_words == 1 ? 0xF800 : 0);

    for (UWORD plane_idx = 0; plane_idx < kDispDepth; ++ plane_idx) {
      if (kHeaderTextPen & (1 << plane_idx)) {
        *(cl ++) = 1;
        *(cl ++) = 0;
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
        *(cl ++) = CUSTOM_OFFSET(bltsize);
        *(cl ++) = (kFontHeight << BLTSIZE_H0_SHF) | width_words;
      }

      dst_start += kDispSlice;
    }
  }

  return cl;
}

static Status make_view() {
  Status status = StatusOK;

  g.cpr_list.start = &cop_lists[0][0];
  g.cpr_list.MaxCount = kDispCopSizeWords / 2;

  InitView(&g.view);
  g.view.ViewPort = &g.screen->ViewPort;
  g.view.LOFCprList = &g.cpr_list;

cleanup:
  return status;
}

static void make_z_incs() {
  UWORD prev_z = kNearZ;

  for (UWORD i = 0; i < kDrawHeight; ++ i) {
    UWORD draw_y = (kDrawHeight - 1) - i;
    UWORD draw_y_frac = kNearZ + ((draw_y * (kFarZ - kNearZ)) / kDrawHeight);
    UWORD z = ((kNearZ - 1) << 0x10) / draw_y_frac;

    g.z_incs[i] = z - prev_z;
    prev_z = z;
  }
}

static void get_display_window(struct ViewPort* viewport,
                               UWORD* diwstrt,
                               UWORD* diwstop,
                               UWORD* diwhigh) {
  struct CopList* cop_list = viewport->DspIns;

  for (UWORD i = 0; i < cop_list->Count; ++ i) {
    struct CopIns* cop_ins = &cop_list->CopIns[i];

    if (cop_ins->OpCode == 0) {
      UWORD addr = cop_ins->u3.u4.u1.DestAddr & 0xFFF;
      UWORD data = cop_ins->u3.u4.u2.DestData;

      switch (addr) {
      case CUSTOM_OFFSET(diwstrt):
        *diwstrt = data;
        break;

      case CUSTOM_OFFSET(diwstop):
        *diwstop = data;
        break;

      case CUSTOM_OFFSET(diwhigh):
        *diwhigh = data;
        break;
      }
    }
  }
}

static void update_sprite(UWORD* cop_list,
                          UWORD player_x,
                          UWORD camera_z_inc) {
  // FIXME: y_frac naming (not fraction of draw height)
  UWORD y_frac = ((kNearZ - 1) << 0x10) / kPlayerZ;
  UWORD y = ((y_frac - kNearZ) * kDrawHeight) / (0x10000 - kNearZ) + (kDispHeight - kDrawHeight) - 8;

  UWORD hstart_left = (kDispWinX & ~1) + player_x - 8;
  UWORD vstart = kDispWinY + y + (kBallEdge / 2);
  UWORD vstop = vstart + kBallEdge;

  UWORD spr_ctl_0 = (vstart & 0xFF) << SPRxPOS_SV0_SHF;
  UWORD spr_ctl_1 = ((vstop & 0xFF) << SPRxCTL_EV0_SHF) | ((vstart >> 8) << SPRxCTL_SV8_SHF) | ((vstop >> 8) << SPRxCTL_EV8_SHF);

  static UWORD last_player_x = 0;
  WORD player_dx = player_x - last_player_x;
  last_player_x = player_x;

  static WORD ball_angle = 0;
  WORD ball_angle_bound = (player_dx < 0 ? -kBallAngleLimit : (player_dx > 0 ? kBallAngleLimit : 0));
  UWORD ball_angle_inc = camera_z_inc << 1;

  if ((player_dx < 0) || (player_dx == 0 && ball_angle > 0)) {
    ball_angle = MAX(ball_angle_bound, ball_angle - ball_angle_inc);
  }
  else {
    ball_angle = MIN(ball_angle_bound, ball_angle + ball_angle_inc);
  }

  UWORD spr_frame = (ball_angle + (kBallAngleLimit + 1)) >> 11;

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
  static UWORD color_mask = 0x7F; // 7 off, 7 on

  UWORD color1 = g.colors[25];
  UWORD color2 = g.colors[26];

  UWORD* cop_list_colors = &cop_list[g.cop_list_spr_idx + 3];

  for (UWORD color_idx = 0; color_idx < kColorCycleNum; ++ color_idx) {
    cop_list_colors[color_idx << 1] = (color_mask & (1 << color_idx)) ? color1 : color2;
  }

  static UWORD last_cycle_count = 0;

  if (camera_z == 0) {
    last_cycle_count = 0;
  }

  UWORD cycle_count = camera_z >> kColorCycleZShift;
  UWORD cycle_shift = MAX(0, MIN(3, cycle_count - last_cycle_count));
  last_cycle_count = cycle_count;

  color_mask = ((color_mask << cycle_shift) | (color_mask >> (kColorCycleNum - cycle_shift)));
  color_mask &= (1 << kColorCycleNum) - 1;
}

static void update_score(UWORD* cop_list,
                         UWORD score_frac) {
  UWORD* cl = &cop_list[g.cop_list_score_idx] + 19;

  for (WORD char_idx = 0; char_idx < 4; ++ char_idx) {
    UWORD glyph_idx = 0;

    if (score_frac || char_idx < 2) {
      glyph_idx = 0x10 + (score_frac % 10);
      score_frac /= 10;
    }

    ULONG src_start = (ULONG)font_planes + (glyph_idx << 1);

    for (UWORD plane_idx = 0; plane_idx < kDispDepth; ++ plane_idx) {
      if (kHeaderTextPen & (1 << plane_idx)) {
        cl[0] = WORD_HI(src_start);
        cl[2] = WORD_LO(src_start);

        cl += 32;
      }
    }
  }
}

void gfx_enable_copper_blits(BOOL enable) {
  custom.copcon = enable ? COPCON_CDANG : 0;
}
