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
#define WORD_LO(ADDR) (UWORD)(ULONG)(ADDR)
#define WORD_HI(ADDR) (UWORD)((ULONG)(ADDR) >> kBitsPerWord)

// If FALSE, NULL, or StatusError then exit function with StatusError.
// If console I/O is not blocked (Forbid, OwnBlitter) print the assertion.
#define ASSERT(EXPR)                       \
  if (! (EXPR)) {                          \
    print_error(#EXPR);                    \
    status = StatusError;                  \
    goto cleanup;                          \
  }

// If FALSE, NULL, or StatusError then exit function with given status.
#define CHECK(EXPR, STATUS)                \
  if (! (EXPR)) {                          \
    status = STATUS;                       \
    goto cleanup;                          \
  }

// If StatusError then see ASSERT(EXPR).
// If StatusOK or matches given status mask then continue.
// Otherwise exit function with status returned by EXPR.
#define CATCH(EXPR, STATUSMASK)            \
  ASSERT(status = EXPR);                   \
                                           \
  if (status & ~(StatusOK | STATUSMASK)) { \
    goto cleanup;                          \
  }

typedef enum {
  StatusError       = 0, // Must be zero, unifies !(status) handling with FALSE, NULL
  StatusOK          = (1 << 0),
  StatusOutOfMemory = (1 << 1),
  StatusInvalidPath = (1 << 2),
  StatusInvalidMod  = (1 << 3),
  StatusQuit        = (1 << 4),
  StatusPlay        = (1 << 5),
  StatusTrackEnd    = (1 << 6),
} Status;

extern void common_init();
extern void memory_clear(APTR base,
                         ULONG size);
extern UWORD string_length(STRPTR str);
extern void string_copy(STRPTR dst,
                        STRPTR src);
extern void string_to_upper(STRPTR str);
extern BOOL string_has_suffix(STRPTR name,
                              UBYTE* suffix,
                              UWORD suffix_len);
extern BOOL string_has_prefix(STRPTR name,
                              UBYTE* prefix,
                              UWORD prefix_len);
extern void string_append_path(STRPTR base,
                               STRPTR subdir);
extern void print_error(STRPTR str);
