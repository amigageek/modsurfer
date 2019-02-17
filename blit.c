#include "blit.h"
#include "custom.h"
#include "gfx.h"

// See Hardware Reference Manual for a description of all Blitter parameters.

void blit_copy(APTR src_base,
               UWORD src_stride_b,
               UWORD src_x,
               UWORD src_y,
               APTR dst_base,
               UWORD dst_stride_b,
               UWORD dst_x,
               UWORD dst_y,
               UWORD copy_w,
               UWORD copy_h,
               BOOL replace_bg,
               BOOL force_desc) {
  UWORD start_x[2] = {src_x, dst_x};
  UWORD start_x_word[2], end_x_word[2], num_words[2], word_offset[2];

  for (UWORD i = 0; i < 2; ++ i) {
    start_x_word[i] = start_x[i] >> 4;
    end_x_word[i] = ((start_x[i] + copy_w) + 0xF) >> 4;
    num_words[i] = end_x_word[i] - start_x_word[i];
    word_offset[i] = start_x[i] & 0xF;
  }

  UWORD width_words = MAX(num_words[0], num_words[1]);
  WORD shift = (WORD)word_offset[1] - (WORD)word_offset[0];
  UWORD src_mod_b = src_stride_b - (width_words * kBytesPerWord);
  UWORD dst_mod_b = dst_stride_b - (width_words * kBytesPerWord);

  BOOL desc = force_desc || (shift < 0);

  UWORD start_x_off = desc ? (width_words - 1) : 0;
  UWORD start_y_off = desc ? (copy_h - 1) : 0;

  ULONG src_start_b = (ULONG)src_base +
    ((src_y + start_y_off) * src_stride_b) +
    ((start_x_word[0] + start_x_off) * kBytesPerWord);
  ULONG dst_start_b = (ULONG)dst_base +
    ((dst_y + start_y_off) * dst_stride_b) +
    ((start_x_word[1] + start_x_off) * kBytesPerWord);

  UWORD left_word_mask = (UWORD)(0xFFFFU << (word_offset[0] + MAX(0, 0x10 - (word_offset[0] + copy_w)))) >> word_offset[0];
  UWORD right_word_mask;

  if (width_words == 1) {
    right_word_mask = left_word_mask;
  }
  else {
    right_word_mask = 0xFFFFU << MIN(0x10, ((start_x_word[0] + width_words) << 4) - (src_x + copy_w));
  }

  gfx_wait_blit();

  // A = Mask of bits inside copy region
  // B = Source data
  // C = Destination data (for region outside mask)
  // D = Destination data
  UWORD minterm = replace_bg ? 0xCA : 0xEA;

  custom.bltcon0 = (ABS(shift) << BLTCON0_ASH0_SHF) | BLTCON0_USEB | BLTCON0_USEC | BLTCON0_USED | minterm;
  custom.bltcon1 = (ABS(shift) << BLTCON1_BSH0_SHF) | (desc ? BLTCON1_DESC : 0);
  custom.bltbmod = src_mod_b;
  custom.bltcmod = dst_mod_b;
  custom.bltdmod = dst_mod_b;
  custom.bltafwm = (desc ? right_word_mask : left_word_mask);
  custom.bltalwm = (desc ? left_word_mask : right_word_mask);
  custom.bltadat = 0xFFFF;
  custom.bltbpt = (APTR)src_start_b;
  custom.bltcpt = (APTR)dst_start_b;
  custom.bltdpt = (APTR)dst_start_b;
  custom.bltsize = (copy_h << BLTSIZE_H0_SHF) | width_words;
}

void blit_rect(APTR dst_base,
               UWORD dst_stride_b,
               UWORD dst_x,
               UWORD dst_y,
               APTR mask_base,
               UWORD mask_stride_b,
               UWORD mask_x,
               UWORD mask_y,
               UWORD width,
               UWORD height,
               BOOL set_bits) {
  UWORD start_x_word = dst_x >> 4;
  UWORD end_x_word = ((dst_x + width) + 0xF) >> 4;
  UWORD width_words = end_x_word - start_x_word;
  UWORD word_offset = dst_x & 0xF;

  UWORD dst_mod_b = dst_stride_b - (width_words * kBytesPerWord);
  UWORD mask_mod_b = mask_stride_b - (width_words * kBytesPerWord);

  ULONG dst_start_b = (ULONG)dst_base + (dst_y * dst_stride_b) + (start_x_word * kBytesPerWord);
  ULONG mask_start_b = (ULONG)mask_base + (mask_y * mask_stride_b) + (start_x_word * kBytesPerWord);

  UWORD left_word_mask = (UWORD)(0xFFFFU << (word_offset + MAX(0, 0x10 - (word_offset + width)))) >> word_offset;
  UWORD right_word_mask;

  if (width_words == 1) {
    right_word_mask = left_word_mask;
  }
  else {
    right_word_mask = 0xFFFFU << MIN(0x10, ((start_x_word + width_words) << 4) - (dst_x + width));
  }

  UWORD minterm = 0xA;

  if (mask_base) {
    minterm |= set_bits ? 0xB0 : 0x80;
  }
  else {
    minterm |= set_bits ? 0xF0 : 0x00;
  }

  gfx_wait_blit();

  // A = Mask of bits inside copy region
  // B = Optional bitplane mask
  // C = Destination data (for region outside mask)
  // D = Destination data
  custom.bltcon0 = BLTCON0_USEC | BLTCON0_USED | (mask_base ? BLTCON0_USEB : 0) | minterm;
  custom.bltcon1 = 0;
  custom.bltbmod = mask_mod_b;
  custom.bltcmod = dst_mod_b;
  custom.bltdmod = dst_mod_b;
  custom.bltafwm = left_word_mask;
  custom.bltalwm = right_word_mask;
  custom.bltadat = 0xFFFF;
  custom.bltbpt = (APTR)mask_start_b;
  custom.bltcpt = (APTR)dst_start_b;
  custom.bltdpt = (APTR)dst_start_b;
  custom.bltsize = (height << BLTSIZE_H0_SHF) | width_words;
}

void blit_line(APTR dst_base,
               UWORD dst_stride_b,
               UWORD x0,
               UWORD y0,
               UWORD x1,
               UWORD y1) {
  UWORD dx = ABS(x1 - x0);
  UWORD dy = ABS(y1 - y0);
  UWORD dmax = MAX(dx, dy);
  UWORD dmin = MIN(dx, dy);
  ULONG dst_start = (ULONG)dst_base + (y0 * dst_stride_b) + ((x0 / 0x8) & ~0x1);
  UBYTE octant =
    ((((dx >= dy) && (x0 >= x1)) | ((dx < dy) && (y0 >= y1))) << 0) |
    ((((dx >= dy) && (y0 >= y1)) | ((dx < dy) && (x0 >= x1))) << 1) |
    ((dx >= dy) << 2);

  gfx_wait_blit();

  // A = Line parameters
  // C = Destination data (for region outside mask)
  // D = Destination data
  custom.bltcon0 = ((x0 & 0xF) << BLTCON0_ASH0_SHF) | BLTCON0_USEA
                 | BLTCON0_USEC | BLTCON0_USED | (0xCA << BLTCON0_LF0_SHF);
  custom.bltcon1 =
    ((x0 & 0xF) << BLTCON1_TEX0_SHF) |
    ((((4 * dmin) - (2 * dmax)) < 0 ? 1 : 0) << BLTCON1_SIGN_SHF) |
    (octant << BLTCON1_AUL_SHF) |
    (0 << BLTCON1_SING_SHF) |
    BLTCON1_LINE;
  custom.bltadat = 0x8000;
  custom.bltbdat = 0xFFFF;
  custom.bltafwm = 0xFFFF;
  custom.bltalwm = 0xFFFF;
  custom.bltamod = 4 * (dmin - dmax);
  custom.bltbmod = 4 * dmin;
  custom.bltcmod = dst_stride_b;
  custom.bltdmod = dst_stride_b;
  custom.bltapt = (APTR)((4 * dmin) - (2 * dmax));
  custom.bltcpt = (APTR)dst_start;
  custom.bltdpt = (APTR)dst_start;
  custom.bltsize = ((dmax + 1) << BLTSIZE_H0_SHF) | (0x2 << BLTSIZE_W0_SHF);
}

void blit_fill(APTR dst_base,
               UWORD dst_stride_b,
               UWORD x,
               UWORD y,
               UWORD width,
               UWORD height) {
  UWORD start_x_word = x / 0x10;
  UWORD end_x_word = (x + width) / 0x10;
  UWORD width_words = end_x_word - start_x_word;
  UWORD word_offset = x & 0xF;
  UWORD mod_b = dst_stride_b - (width_words * kBytesPerWord);

  ULONG dst_start_b = (ULONG)dst_base +
    ((y + height - 1) * dst_stride_b) +
    ((start_x_word + width_words - 1) * kBytesPerWord);

  UWORD left_word_mask = (UWORD)(0xFFFFU << (word_offset + MAX(0, 0x10 - (word_offset + width)))) >> word_offset;
  UWORD right_word_mask;

  if (width_words == 1) {
    right_word_mask = left_word_mask;
  }
  else {
    right_word_mask = 0xFFFFU << MIN(0x10, ((start_x_word + width_words) << 4) - (x + width));
  }

  gfx_wait_blit();

  // A = Mask of bits inside copy region
  // B = Source data
  // C = Destination data (for region outside mask)
  // D = Destination data
  custom.bltcon0 = BLTCON0_USEA | BLTCON0_USED | 0xF0;
  custom.bltcon1 = BLTCON1_IFE | BLTCON1_DESC;
  custom.bltamod = mod_b;
  custom.bltdmod = mod_b;
  custom.bltafwm = right_word_mask;
  custom.bltalwm = left_word_mask;
  custom.bltapt = (APTR)dst_start_b;
  custom.bltdpt = (APTR)dst_start_b;
  custom.bltsize = (height << BLTSIZE_H0_SHF) | width_words;
}

void blit_char(APTR font_base,
               UWORD glyph_idx,
               APTR dst_row_base,
               UWORD dst_x,
               UWORD color,
               BOOL replace_bg) {
  UWORD start_x_word = dst_x >> 4;
  UWORD end_x_word = ((dst_x + kFontWidth) + 0xF) >> 4;
  UWORD width_words = end_x_word - start_x_word;

  ULONG src_start = (ULONG)font_base + (glyph_idx << 1);
  ULONG dst_start = (ULONG)dst_row_base + (start_x_word << 1);
  UWORD shift = dst_x & 0xF;
  UWORD right_word_mask = (width_words == 1 ? 0xF800 : 0);

  UWORD minterm = replace_bg ? 0xCA : 0xEA;

  for (UWORD plane_idx = 0; plane_idx < kDispDepth; ++ plane_idx) {
    if (color & (1 << plane_idx)) {
      gfx_wait_blit();

      custom.bltcon0 = (shift << BLTCON0_ASH0_SHF) | BLTCON0_USEB | BLTCON0_USEC | BLTCON0_USED | minterm;
      custom.bltcon1 = shift << BLTCON1_BSH0_SHF;
      custom.bltbmod = (kFontNGlyphs * kBytesPerWord) - (width_words << 1);
      custom.bltcmod = kDispStride - (width_words << 1);
      custom.bltdmod = kDispStride - (width_words << 1);
      custom.bltafwm = 0xF800;
      custom.bltalwm = right_word_mask;
      custom.bltadat = 0xFFFF;
      custom.bltbpt = (APTR)src_start;
      custom.bltcpt = (APTR)dst_start;
      custom.bltdpt = (APTR)dst_start;
      custom.bltsize = (kFontHeight << BLTSIZE_H0_SHF) | width_words;
    }

    dst_start += kDispSlice;
  }
}
