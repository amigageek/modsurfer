#include "module.h"

#include <proto/dos.h>
#include <proto/exec.h>

#define mTrackerId(a, b, c, d) (((a) << 0x18) | ((b) << 0x10) | ((c) << 0x8) | (d))

static Status read_header(BPTR file, LONG file_size);
static Status read_nonchip(BPTR file, LONG file_size);
static Status read_samples(BPTR file, LONG file_size);

static struct {
  UBYTE file_path[0x100];
  ModuleHeader header;
  ModuleNonChip* nonchip;
  UWORD num_patterns;
  ULONG nonchip_size;
  APTR samples;
  ULONG samples_size;
} g;

VOID module_open(STRPTR dir_path,
                 STRPTR file_name) {
  string_copy(g.file_path, dir_path);
  string_copy(g.file_path + string_length(g.file_path), file_name);
}

VOID module_close() {
  string_copy(g.file_path, "");

  if (g.samples) {
    FreeMem(g.samples, g.samples_size);
    g.samples = NULL;
  }

  if (g.nonchip) {
    FreeMem(g.nonchip, g.nonchip_size);
    g.nonchip = NULL;
  }
}

BOOL module_is_open() {
  return (g.file_path[0] ? TRUE : FALSE);
}

Status module_load_header() {
  Status status = StatusOK;
  BPTR file = 0;

  CHECK(file = Open(g.file_path, MODE_OLDFILE));

  LONG file_size = 0;
  Seek(file, 0, OFFSET_END);
  CHECK((file_size = Seek(file, 0, OFFSET_BEGINNING)) > 0);

  CHECK(read_header(file, file_size));

cleanup:
  if (file) {
    Close(file);
  }

  return status;
}

Status module_load_all() {
  Status status = StatusOK;
  BPTR file = 0;

  CHECK(file = Open(g.file_path, MODE_OLDFILE));

  LONG file_size = 0;
  Seek(file, 0, OFFSET_END);
  CHECK((file_size = Seek(file, 0, OFFSET_BEGINNING)) > 0);

  if (! g.nonchip) {
    CHECK(read_nonchip(file, file_size));
  }

  if (! g.samples) {
    CHECK(read_samples(file, file_size));
  }

cleanup:
  if (file) {
    Close(file);
  }

  return status;
}

ModuleHeader* module_header() {
  return &g.header;
}

UWORD module_get_num_patterns() {
  return g.num_patterns;
}

ModuleNonChip* module_get_nonchip() {
  return g.nonchip;
}

APTR module_get_samples() {
  return g.samples;
}

ULONG module_get_samples_size() {
  return g.samples_size;
}

static Status read_header(BPTR file,
                          LONG file_size) {
  Status status = StatusOK;

  CHECK(file_size >= sizeof(ModuleHeader));
  Seek(file, 0, OFFSET_BEGINNING);
  CHECK(Read(file, &g.header, sizeof(ModuleHeader)) == sizeof(ModuleHeader));

  switch(g.header.tracker_id) {
  case mTrackerId('M', '.', 'K', '.'):
  case mTrackerId('M', '!', 'K', '!'):
  case mTrackerId('F', 'L', 'T', '4'):
    break;
  default:
    CHECK("Unsupported module format" && FALSE);
  }

cleanup:
  return status;
}

static Status read_nonchip(BPTR file,
                           LONG file_size) {
  Status status = StatusOK;

  // Calculate the number of patterns.
  g.num_patterns = 1;
  CHECK(g.header.pat_tbl_size <= 128);

  for (UWORD i = 0; i < g.header.pat_tbl_size; ++ i) {
    g.num_patterns = MAX(g.num_patterns, 1 + g.header.pat_tbl[i]);
  }

  CHECK(g.num_patterns <= kNumPatternsMax);

  // Load the module header and pattern data into contiguous memory.
  g.nonchip_size = sizeof(ModuleHeader) + (g.num_patterns * sizeof(Pattern));
  CHECK(g.nonchip = (ModuleNonChip*)AllocMem(g.nonchip_size, 0));

  CHECK(file_size >= g.nonchip_size);
  Seek(file, 0, OFFSET_BEGINNING);
  CHECK(Read(file, g.nonchip, g.nonchip_size) == g.nonchip_size);

cleanup:
  return status;
}

static Status read_samples(BPTR file,
                           LONG file_size) {
  Status status = StatusOK;

  // Calculate the total sample data size.
  g.samples_size = 0;

  for (UWORD i = 0; i < kNumSamples; ++ i) {
    g.samples_size += 2 * g.header.sample_info[i].length_w;
  }

  // Load sample data into chip memory.
  CHECK(g.samples = AllocMem(g.samples_size, MEMF_CHIP));

  ULONG read_size = file_size - g.nonchip_size;
  Seek(file, g.nonchip_size, OFFSET_BEGINNING);
  CHECK(Read(file, g.samples, read_size) == read_size);

  // File size may be slightly truncated in some mods.
  if (read_size < g.samples_size) {
    mem_clear(g.samples + read_size, g.samples_size - read_size);
  }

cleanup:
  return status;
}
