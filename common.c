#include "common.h"

#include <proto/dos.h>

static struct {
  UBYTE uppercase_lut[1 << kBitsPerByte];
} g;

VOID common_init() {
  for (UWORD i = 0; i < ARRAY_NELEMS(g.uppercase_lut); ++ i) {
    BOOL lowercase = (i >= 'a') && (i <= 'z');
    g.uppercase_lut[i] = (UBYTE)(lowercase ? (i - ('a' - 'A')) : i);
  }
}

VOID mem_clear(APTR base,
               ULONG size) {
  UBYTE* start = base;
  UBYTE* end = base + size;

  for (UBYTE* iter = start; iter != end; ++ iter) {
    *iter = 0;
  }
}

UWORD string_length(STRPTR str) {
  UWORD len;
  for (len = 0; str[len]; ++ len);
  return len;
}

VOID string_copy(STRPTR dst,
                 STRPTR src) {
  while (*(dst ++) = *(src ++));
}

VOID string_to_upper(STRPTR str) {
  for (UWORD i = 0; str[i]; ++ i) {
    str[i] = g.uppercase_lut[str[i]];
  }
}

VOID print_error(STRPTR msg) {
  if (DOSBase) {
    STRPTR out_strs[] = { "modsurfer: assert(", msg, ") failed\n" };
    BPTR out_handle = Output();

    for (UWORD i = 0; i < ARRAY_NELEMS(out_strs); ++ i) {
      Write(out_handle, out_strs[i], string_length(out_strs[i]));
    }
  }
}
