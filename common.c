#include "common.h"
#include "system.h"

static struct {
  UBYTE uppercase_lut[1 << kBitsPerByte];
} g;

void common_init() {
  for (UWORD i = 0; i < ARRAY_NELEMS(g.uppercase_lut); ++ i) {
    BOOL lowercase = (i >= 'a') && (i <= 'z');
    g.uppercase_lut[i] = (UBYTE)(lowercase ? (i - ('a' - 'A')) : i);
  }
}

void memory_clear(APTR base,
                  ULONG size) {
  UBYTE* start = base;
  UBYTE* end = base + size;

  for (UBYTE* byte = start; byte != end; ++ byte) {
    *byte = 0;
  }
}

UWORD string_length(STRPTR str) {
  UWORD len;
  for (len = 0; str[len]; ++ len);
  return len;
}

void string_copy(STRPTR dst,
                 STRPTR src) {
  while (*(dst ++) = *(src ++));
}

void string_to_upper(STRPTR str) {
  for (UWORD i = 0; str[i]; ++ i) {
    str[i] = g.uppercase_lut[str[i]];
  }
}

BOOL string_has_suffix(STRPTR name,
                       UBYTE* suffix,
                       UWORD suffix_len) {
  UWORD name_len = string_length(name);

  if (name_len <= suffix_len) {
    return FALSE;
  }

  STRPTR suffix_start = name + (name_len - suffix_len);

  for (UWORD i = 0; i < suffix_len; ++ i) {
    if (suffix_start[i] != suffix[i]) {
      return FALSE;
    }
  }

  return TRUE;
}

BOOL string_has_prefix(STRPTR name,
                       UBYTE* prefix,
                       UWORD prefix_len) {
  UWORD name_len = string_length(name);

  if (name_len <= prefix_len) {
    return FALSE;
  }

  for (UWORD i = 0; i < prefix_len; ++ i) {
    if (name[i] != prefix[i]) {
      return FALSE;
    }
  }

  return TRUE;
}

void string_append_path(STRPTR base,
                        STRPTR subdir) {
  UWORD base_len = string_length(base);

  if (subdir[0] == '/') {
    UWORD trim_to = base_len - 1;

    while ((base[trim_to - 1] != '/') && (base[trim_to - 1] != ':') && (trim_to > 0)) {
      -- trim_to;
    }

    base[trim_to] = '\0';
  }
  else {
    UWORD base_len = string_length(base);
    UWORD subdir_len = string_length(subdir);

    STRPTR separator = base[0] ? "/" : ":";
    string_copy(base + base_len, subdir);
    string_copy(base + base_len + subdir_len, separator);
  }
}

void print_error(STRPTR str) {
  system_print_error(str);
}
