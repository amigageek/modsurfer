#pragma once

#include "common.h"

#define kModTitleMaxLen 20
#define kSampleNameMaxLen 22
#define kNumPatternsMax 100
#define kNumSamplesMax 31
#define kSongMaxLen 0x80
#define kDivsPerPattern 0x40

typedef struct {
  UBYTE title[kModTitleMaxLen];

  struct {
    UBYTE name[kSampleNameMaxLen];
    UWORD length_w;
    UBYTE finetune;
    UBYTE volume;
    UWORD loop_start_w;
    UWORD loop_length_w;
  } sample_info[kNumSamplesMax];

  UBYTE pat_tbl_size;
  UBYTE unused;
  UBYTE pat_tbl[kSongMaxLen];
  ULONG tracker_id;
} ModuleHeader;

typedef struct {
  ULONG sample_hi:4;
  ULONG parameter:12;
  ULONG sample_lo:4;
  ULONG effect:12;
} PatternCommand;

typedef struct {
  PatternCommand commands[4];
} PatternDivision;

typedef struct {
  PatternDivision divisions[kDivsPerPattern];
} Pattern;

typedef struct {
  ModuleHeader header;
  Pattern patterns[];
} ModuleNonChip;

extern void module_open(STRPTR dir_path,
                        STRPTR file_name);
extern void module_close();
extern BOOL module_is_open();
extern Status module_load_header();  // StatusError, StatusInvalidMod
extern Status module_load_all();     // StatusError, StatusInvalidMod, StatusOutOfMemory
extern ModuleHeader* module_header();
extern UWORD module_num_patterns();
extern ModuleNonChip* module_nonchip();
extern APTR module_samples();
