#pragma once

#include "common.h"

VOID blit_copy(APTR src_base,
               UWORD src_stride_b,
               UWORD src_x,
               UWORD src_y,
               APTR dst_base,
               UWORD dst_stride_b,
               UWORD dst_x,
               UWORD dst_y,
               UWORD copy_w,
               UWORD copy_h,
               BOOL force_desc);

VOID blit_rect(APTR dst_base,
               UWORD dst_stride_b,
               UWORD dst_x,
               UWORD dst_y,
               UWORD width,
               UWORD height,
               BOOL set_bits);

VOID blit_line(APTR dst_base,
               UWORD dst_stride_b,
               UWORD x0,
               UWORD y0,
               UWORD x1,
               UWORD y1);

VOID blit_fill(APTR dst_base,
               UWORD dst_stride_b,
               UWORD x,
               UWORD y,
               UWORD width,
               UWORD height);

VOID blit_char(APTR font_base,
               UWORD glyph_idx,
               APTR dst_row_base,
               UWORD dst_x,
               UWORD color,
               BOOL replace_bg);
