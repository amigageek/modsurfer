#pragma once

#include <exec/types.h>

#define kBitsPerByte 0x8
#define kBitsPerWord 0x10
#define kBytesPerWord 0x2
#define kUWordMax 0xFFFF

#define MIN(X, Y) ((X) < (Y) ? (X) : (Y))
#define MAX(X, Y) ((X) > (Y) ? (X) : (Y))
#define ABS(X) ((X) < 0 ? (-(X)) : (X))
#define ARRAY_NELEMS(X) (sizeof(X) / sizeof((X)[0]))
#define LO16(ADDR) ((UWORD)(ULONG)(ADDR))
#define HI16(ADDR) ((UWORD)((ULONG)(ADDR) >> 0x10))

#define CHECK(X)          \
  if (! (X)) {            \
    status = StatusError; \
    goto cleanup;         \
  }

#define ASSERT(X)         \
  if (! (X)) {            \
    print_error(#X);      \
    status = StatusError; \
    goto cleanup;         \
  }

typedef UWORD Status;

#define StatusError 0
#define StatusOK 1
#define StatusQuit 2
#define StatusPlay 3

extern VOID common_init();
extern UWORD string_length(STRPTR str);
extern VOID string_copy(STRPTR dst,
                        STRPTR src);
extern VOID string_to_upper(STRPTR str);
extern VOID print_error(STRPTR msg);
