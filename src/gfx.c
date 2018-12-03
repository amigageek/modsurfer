#include "gfx.h"
#include "blit.h"
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
#define kDispCopSizeWords (364 + ((kDrawHeight + 1) * 16))
#define kHeaderTextTop (logo_height + ((kDispHdrHeight - logo_height - kFontHeight) / 2))
#define kHeaderTextGap 60
#define kHeaderTextPen 5
#define kPtrSprEdge 0x10
#define kPtrSprOffX -6
#define kPtrSprOffY -1
#define kPlayerZ (kNearZ + (kNumStepsDelay * kBlockGapDepth))
#define kBlockWidth ((3 * kLaneWidth) / 5)
#define kStripeWidth 4
#define kHeaderPalette 0xB8C, 0x425, 0x94A
#define kFadeActionNumColors 8

static VOID make_bitmap();
static Status make_screen();
static Status make_window();
static Status make_copperlists();
static UWORD* make_copperlist_score(UWORD* cl);
static Status make_view();
static VOID make_sprites();
static VOID make_z_incs();
static VOID get_display_window(struct ViewPort* viewport, UWORD* diwstrt, UWORD* diwstop, UWORD* diwhigh);
static VOID update_sprites(UWORD player_x);
static VOID update_score(UWORD* cop_list,
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
  UWORD cop_list_spr_idx;
  UWORD cop_list_rows_end;
  UWORD cop_list_score_idx;
  struct View view;
  UWORD z_incs[kDrawHeight];
  UWORD colors[kFadeActionNumColors];
} g;

static UWORD disp_planes[kDispDepth][kDispHeight + kDispColPad][kDispStride / kBytesPerWord] __chip;
static UWORD cop_lists[2][kDispCopSizeWords] __chip;
static UWORD null_spr[2] __chip;
static UWORD ship_sprs[4][ship_height + 2][2] __chip;

Status gfx_init() {
  Status status = StatusOK;

  make_bitmap();
  ASSERT(make_screen());
  ASSERT(make_window());
  ASSERT(make_copperlists());
  ASSERT(make_view());
  make_sprites();
  make_z_incs();

cleanup:
  return status;
}

VOID gfx_fini() {
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

VOID gfx_draw_text(STRPTR text,
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

VOID gfx_draw_logo() {
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

VOID gfx_draw_title(STRPTR title) {
  UWORD text_right = (kDispWidth - kHeaderTextGap) / 2;
  UWORD text_width = string_length(title) * kFontSpacing - 1;
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

  gfx_draw_text(title, -1, text_left, kHeaderTextTop, kHeaderTextPen, TRUE);
}

VOID gfx_init_score() {
  UWORD left = (kDispWidth + kHeaderTextGap) / 2;
  gfx_draw_text("  0.0%", -1, left, kHeaderTextTop, kHeaderTextPen, TRUE);
}

#define kFadeMenuNumColors 5

VOID gfx_fade_menu(BOOL fade_in) {
  static UWORD color_indices[kFadeMenuNumColors] = { 1, 2, 3, 4, 18 };

  UWORD colors_lo[kFadeMenuNumColors] = { 0x000 };
  UWORD colors_hi[kFadeMenuNumColors] = { 0x425, 0xB4C, 0xB8C, 0x5C5, 0xDB9 };
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

BOOL gfx_fade_action(BOOL fade_in) {
  UWORD colors_lo[kFadeActionNumColors] = { 0x000 };
  UWORD colors_hi[kFadeActionNumColors] = { 0x606, 0x402, 0x804, 0x415, 0x739, 0x94B, 0xB5F, 0xD9F };

  BOOL fading = fade_common(fade_in ? g.colors : colors_lo,
                            fade_in ? colors_hi : g.colors,
                            kFadeActionNumColors, fade_in);

  for (UWORD cop_list_idx = 0; cop_list_idx < 2; ++ cop_list_idx) {
    for (UWORD i = 0; i < 5; ++ i) {
      cop_lists[cop_list_idx][g.cop_list_spr_idx + 3 + (i * 2)] = g.colors[3 + i];
    }
  }

  return fading;
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

VOID gfx_clear_body() {
  for (UWORD i = 0; i < kDispDepth; ++ i) {
    blit_rect(&disp_planes[i][0][0], kDispStride, 0, kDrawTop,
              NULL, 0, 0, 0, kDispWidth, kDrawHeight, FALSE);
  }
}

VOID gfx_draw_track() {
  gfx_clear_body();

  WORD near_sx[] = {
    (-kLaneWidth / 2) - (kStripeWidth / 2),
    (-kLaneWidth / 2) + (kStripeWidth / 2),
    ( kLaneWidth / 2) - (kStripeWidth / 2),
    ( kLaneWidth / 2) + (kStripeWidth / 2),
    -kBlockWidth / 2 - kLaneWidth,
     kBlockWidth / 2 - kLaneWidth,
    -kBlockWidth / 2,
     kBlockWidth / 2,
    -kBlockWidth / 2 + kLaneWidth,
     kBlockWidth / 2 + kLaneWidth,
  };

  WORD far_sx[ARRAY_NELEMS(near_sx)];

  for (UWORD i = 0; i < ARRAY_NELEMS(near_sx); ++ i) {
    far_sx[i] = near_sx[i] / kFarNearRatio;
  }

  UWORD colors[] = { 1, 1, 1, 1, 2, 2, 3, 3, 4, 4 };

  for (UWORD plane_idx = 0; plane_idx < kDispDepth; ++ plane_idx) {
    UWORD* plane = &disp_planes[plane_idx][0][0];

    for (UWORD i = 0; i < ARRAY_NELEMS(near_sx); ++ i) {
      if (colors[i] & (1 << plane_idx)) {
        blit_line(plane, kDispStride, far_sx[i] + (kDispWidth / 2),
                  kDrawTop, near_sx[i] + (kDispWidth / 2), kDispHeight - 1);
      }
    }

    blit_fill(plane, kDispStride, 0, kDrawTop, kDispWidth, kDrawHeight);
  }
}

VOID gfx_update_display(TrackStep *step_near,
                        WORD player_x,
                        ULONG camera_z,
                        UWORD score_frac) {
  static UWORD back_view_idx = 1;
  UWORD* cop_list = cop_lists[back_view_idx];
  back_view_idx ^= 1;

  update_score(cop_list, score_frac);

  WORD player_sx = (player_x * (kNumVisibleSteps - kNumStepsDelay)) / kNumVisibleSteps;
  WORD camera_x = (player_sx * 74) / 100;
  update_sprites((kDispWidth / 2) + (player_sx - camera_x));

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

  UWORD* z_inc_ptr = g.z_incs;
  UWORD* cop_row = &cop_list[g.cop_list_rows_end];

  for (WORD draw_y = kDrawHeight - 1; draw_y >= 0; -- draw_y) {
    UWORD z_inc = *(z_inc_ptr ++);
    z += z_inc;
    step_frac += z_inc;

    if (step_frac >= kBlockGapDepth) {
      step_frac -= kBlockGapDepth;
      ++ step;
    }

    WORD shift_w = shift_x >> 4;
    UWORD bplmod = (kDispRowPadW - 1 + (shift_w - prev_shift_w)) << 1;
    prev_shift_w = shift_w;

    cop_row -= 16;
    cop_row[3] = bplmod;
    cop_row[5] = bplmod;
    cop_row[7] = (shift_x & 0xF) | ((shift_x & 0xF) << 4);
    cop_row[9] = (z & 0x1000) ? g.colors[0] : 0x000;

    UWORD lane_color = (step->collected ? g.colors[1] : g.colors[2]);
    cop_row[11] = (step->active_lane == 1) ? lane_color : 0x000;
    cop_row[13] = (step->active_lane == 2) ? lane_color : 0x000;
    cop_row[15] = (step->active_lane == 3) ? lane_color : 0x000;

    UWORD disp_y = draw_y + (kDispHeight - kDrawHeight);

    if (disp_y == (0x100 - kDispWinY)) {
      cop_row -= 2;
    }

    shift_err += shift_err_inc;

    if (shift_err >= 0x2000) { // FIXME: while
      shift_x += shift_x_inc;
      shift_err -= 0x4000;
    }
  }

  UWORD bplmod = (kDispRowPadW - 1 - prev_shift_w) << 1;

  cop_row -= 16;
  cop_row[3] = bplmod;
  cop_row[5] = bplmod;

  custom.cop1lc = (ULONG)cop_list;
}

VOID gfx_wait_vblank() {
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

VOID gfx_wait_blit() {
  // Give blitter priority while we're waiting.
  // Also takes care of dummy DMACON access to work around Agnus bug.
  custom.dmacon = DMACON_SET | DMACON_BLITPRI;
  while (custom.dmaconr & DMACONR_BBUSY);
  custom.dmacon = DMACON_BLITPRI;
}

static VOID make_bitmap() {
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

  CHECK(g.screen = OpenScreen(&new_screen));

  ShowTitle(g.screen, FALSE);

  UWORD palette[1 << kDispDepth] = { 0, 0, 0, 0, 0, kHeaderPalette };
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

  CHECK(g.window = OpenWindow(&new_window));

  SetPointer(g.window, pointer_planes, kPtrSprEdge, kPtrSprEdge, kPtrSprOffX, kPtrSprOffY);

cleanup:
  return status;
}

static Status make_copperlists() {
  Status status = StatusOK;

  UWORD diwstrt = (kDispWinY << DIWSTRT_V0_Shf) | kDispWinX;
  UWORD diwstop = ((kDispWinY + kDispHeight - 0x100) << DIWSTOP_V0_Shf) | (kDispWinX + kDispWidth - 0x100);
  UWORD diwhigh = 0x2100;

  get_display_window(&g.screen->ViewPort, &diwstrt, &diwstop, &diwhigh);

  UWORD* cl = cop_lists[0];

  for (UWORD spr = 0; spr < 8; ++ spr) {
    ULONG spr_addr = (ULONG)(spr < 4 ? &ship_sprs[spr][0][0] : null_spr);

    *(cl ++) = mCustomOffset(sprpt[spr]);
    *(cl ++) = HI16(spr_addr);
    *(cl ++) = mCustomOffset(sprpt[spr]) + kBytesPerWord;
    *(cl ++) = LO16(spr_addr);
  }

  UWORD hdr_pal[] = { kHeaderPalette };

  for (UWORD i = 0; i < ARRAY_NELEMS(hdr_pal); ++ i) {
    *(cl ++) = mCustomOffset(color[i + 5]);
    *(cl ++) = hdr_pal[i];
  }

  g.cop_list_spr_idx = cl - cop_lists[0];

  for (UWORD i = 0; i < 6; ++ i) {
    *(cl ++) = mCustomOffset(color[16 + i]);
    *(cl ++) = 0;
  }

  *(cl ++) = mCustomOffset(diwstrt);
  *(cl ++) = diwstrt;
  *(cl ++) = mCustomOffset(diwstop);
  *(cl ++) = diwstop;
  *(cl ++) = mCustomOffset(diwhigh);
  *(cl ++) = diwhigh;

  *(cl ++) = mCustomOffset(ddfstrt);
  *(cl ++) = kDispFetchStart;
  *(cl ++) = mCustomOffset(ddfstop);
  *(cl ++) = kDispFetchStop;

  for (UWORD i = 0; i < kDispDepth; ++ i) {
    ULONG plane_start = (ULONG)&disp_planes[i][0][0] - kBytesPerWord;

    *(cl ++) = mCustomOffset(bplpt[i]);
    *(cl ++) = HI16(plane_start);
    *(cl ++) = mCustomOffset(bplpt[i]) + kBytesPerWord;
    *(cl ++) = LO16(plane_start);
  }

  *(cl ++) = mCustomOffset(bpl1mod);
  *(cl ++) = (kDispRowPadW * 2) - 2; // FIXME: macro
  *(cl ++) = mCustomOffset(bpl2mod);
  *(cl ++) = (kDispRowPadW * 2) - 2;

  *(cl ++) = mCustomOffset(bplcon0);
  *(cl ++) = BPLCON0_COLOR;
  *(cl ++) = mCustomOffset(bplcon1);
  *(cl ++) = 0;

  *(cl ++) = (kDispWinY << 8) | 0x1;
  *(cl ++) = 0xFFFE;
  *(cl ++) = mCustomOffset(bplcon0);
  *(cl ++) = BPLCON0_COLOR | (kDispDepth << BPLCON0_BPU_Shf);

  *(cl ++) = ((kDispWinY + kDispHdrHeight - 2) << 8) | 0x1;
  *(cl ++) = 0xFFFE;

  for (UWORD i = 1; i < 8; ++ i) {
    *(cl ++) = mCustomOffset(color[i]);
    *(cl ++) = 0;
  }

  // One extra row to set modulus for first draw line.
  UWORD start_y = kDispWinY + kDrawTop - 1;
  UWORD stop_y = kDispWinY + kDispHeight;

  for (UWORD disp_y = start_y; disp_y < stop_y; ++ disp_y) {
    if (disp_y == 0x100) {
      *(cl ++) = 0xFFDF;
      *(cl ++) = 0xFFFE;
    }

    *(cl ++) = (disp_y << 8) | 0x1;
    *(cl ++) = 0xFFFE;

    *(cl ++) = mCustomOffset(bpl1mod);
    *(cl ++) = 0;
    *(cl ++) = mCustomOffset(bpl2mod);
    *(cl ++) = 0;
    *(cl ++) = mCustomOffset(bplcon1);
    *(cl ++) = 0;
    *(cl ++) = mCustomOffset(color[1]);
    *(cl ++) = 0;
    *(cl ++) = mCustomOffset(color[2]);
    *(cl ++) = 0;
    *(cl ++) = mCustomOffset(color[3]);
    *(cl ++) = 0;
    *(cl ++) = mCustomOffset(color[4]);
    *(cl ++) = 0;
  }

  g.cop_list_rows_end = cl - cop_lists[0];

  *(cl ++) = ((kDispWinY + kDispHeight - 0x100) << 8) | 0x1;
  *(cl ++) = 0xFFFE;
  *(cl ++) = mCustomOffset(bplcon0);
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
        *(cl ++) = mCustomOffset(bltcon0);
        *(cl ++) = (shift << BLTCON0_ASH0_Shf) | BLTCON0_USEB | BLTCON0_USEC | BLTCON0_USED | 0xCA;
        *(cl ++) = mCustomOffset(bltcon1);
        *(cl ++) = shift << BLTCON1_BSH0_Shf;
        *(cl ++) = mCustomOffset(bltbmod);
        *(cl ++) = (kFontNGlyphs * kBytesPerWord) - (width_words << 1);
        *(cl ++) = mCustomOffset(bltcmod);
        *(cl ++) = kDispStride - (width_words << 1);
        *(cl ++) = mCustomOffset(bltdmod);
        *(cl ++) = kDispStride - (width_words << 1);
        *(cl ++) = mCustomOffset(bltafwm);
        *(cl ++) = 0xF800;
        *(cl ++) = mCustomOffset(bltalwm);
        *(cl ++) = right_word_mask;
        *(cl ++) = mCustomOffset(bltadat);
        *(cl ++) = 0xFFFF;
        *(cl ++) = mCustomOffset(bltbpt);
        *(cl ++) = HI16(src_start);
        *(cl ++) = mCustomOffset(bltbpt) + kBytesPerWord;
        *(cl ++) = LO16(src_start);
        *(cl ++) = mCustomOffset(bltcpt);
        *(cl ++) = HI16(dst_start);
        *(cl ++) = mCustomOffset(bltcpt) + kBytesPerWord;
        *(cl ++) = LO16(dst_start);
        *(cl ++) = mCustomOffset(bltdpt);
        *(cl ++) = HI16(dst_start);
        *(cl ++) = mCustomOffset(bltdpt) + kBytesPerWord;
        *(cl ++) = LO16(dst_start);
        *(cl ++) = mCustomOffset(bltsize);
        *(cl ++) = (kFontHeight << BLTSIZE_H0_Shf) | width_words;
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

static VOID make_sprites() {
  UWORD* spr_data = ship_planes;

  for (UWORD y = 0; y < ship_height; ++ y) {
    ship_sprs[0][1 + y][0] = *(spr_data ++);
    ship_sprs[2][1 + y][0] = *(spr_data ++);
    ship_sprs[0][1 + y][1] = *(spr_data ++);
    ship_sprs[2][1 + y][1] = *(spr_data ++);
    ship_sprs[1][1 + y][0] = *(spr_data ++);
    ship_sprs[3][1 + y][0] = *(spr_data ++);
  }
}

static VOID make_z_incs() {
  UWORD prev_z = kNearZ;

  for (UWORD i = 0; i < kDrawHeight; ++ i) {
    UWORD draw_y = (kDrawHeight - 1) - i;
    UWORD draw_y_frac = kNearZ + ((draw_y * (kFarZ - kNearZ)) / kDrawHeight);
    UWORD z = ((kNearZ - 1) << 0x10) / draw_y_frac;

    g.z_incs[i] = z - prev_z;
    prev_z = z;
  }
}

static VOID get_display_window(struct ViewPort* viewport,
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
      case mCustomOffset(diwstrt):
        *diwstrt = data;
        break;

      case mCustomOffset(diwstop):
        *diwstop = data;
        break;

      case mCustomOffset(diwhigh):
        *diwhigh = data;
        break;
      }
    }
  }
}

static VOID update_sprites(UWORD player_x) {
  // FIXME: y_frac naming (not fraction of draw height)
  UWORD y_frac = ((kNearZ - 1) << 0x10) / kPlayerZ;
  UWORD y = ((y_frac - kNearZ) * kDrawHeight) / (0x10000 - kNearZ) + (kDispHeight - kDrawHeight) - 8;

  UWORD hstart_left = (kDispWinX & ~1) + player_x - 8;
  UWORD vstart = kDispWinY + y + ship_height;
  UWORD vstop = vstart + ship_height;

  UWORD spr_ctl_0 = (vstart & 0xFF) << SPRxPOS_SV0_Shf;
  UWORD spr_ctl_1 = ((vstop & 0xFF) << SPRxCTL_EV0_Shf) | ((vstart >> 8) << SPRxCTL_SV8_Shf) | ((vstop >> 8) << SPRxCTL_EV8_Shf);

  for (UWORD spr_idx = 0; spr_idx < 4; ++ spr_idx) {
    UWORD* spr = &ship_sprs[spr_idx][0][0];
    UWORD hstart = hstart_left + ((spr_idx & 2) ? 8 : -8);

    spr[0] = spr_ctl_0 | (((hstart >> 1) & 0xFF) << SPRxPOS_SH1_Shf);
    spr[1] = spr_ctl_1 | ((hstart & 0x1) << SPRxCTL_SH0_Shf) | ((spr_idx & 1) << SPRxCTL_ATT_Shf);
  }
}

static VOID update_score(UWORD* cop_list,
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
        cl[0] = HI16(src_start);
        cl[2] = LO16(src_start);

        cl += 32;
      }
    }
  }
}
