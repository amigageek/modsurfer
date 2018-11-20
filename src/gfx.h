#pragma once

#include "common.h"
#include "track.h"

#include <intuition/intuition.h>

#define kDispWidth 0x140
#define kDispHeight 0x100
#define kDispDepth 3
#define kDispRowPadW 6 // padded for horizontal scrolling
#define kDispColPad 1  // ^-
#define kDispStride ((kDispWidth / kBitsPerByte) + (kDispRowPadW * kBytesPerWord))
#define kDispSlice (kDispStride * (kDispHeight + kDispColPad))
#define kDispHdrHeight 52
#define kFontWidth 5
#define kFontHeight 5
#define kFontSpacing (kFontWidth + 1)
#define kFontNGlyphs 0x60
#define kFarNearRatio 7
#define kFarZ 0xFFFF
#define kNearZ (kFarZ / kFarNearRatio)
#define kBlockGapDepth ((kFarZ - kNearZ + 1) / kNumVisibleSteps)
#define kLaneWidth 123

extern Status gfx_init();
extern VOID gfx_fini();
extern struct Window* gfx_window();
extern UBYTE* gfx_display_planes();
extern UBYTE* gfx_font_plane();
extern struct View* gfx_view();
extern VOID gfx_draw_text(STRPTR text,
                          UWORD max_chars,
                          UWORD left,
                          UWORD top,
                          UWORD color,
                          BOOL replace_bg);
extern VOID gfx_draw_logo();
extern VOID gfx_draw_title(STRPTR title);
extern VOID gfx_draw_score(ULONG score);
extern VOID gfx_fade_menu(BOOL fade_in);
extern BOOL gfx_fade_action(BOOL fade_in);
extern VOID gfx_clear_body();
extern VOID gfx_draw_track();
extern VOID gfx_update_display(TrackStep *step_near,
                               WORD player_x,
                               ULONG camera_z);
extern VOID gfx_wait_vblank();
