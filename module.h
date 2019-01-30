#pragma once

#include "common.h"

#define kModTitleMaxLen 20
#define kSampleNameMaxLen 22
#define kNumPatternsMax 100
#define kNumSamples 31
#define kDivsPerPat 64

typedef struct {
  UBYTE title[kModTitleMaxLen];

  struct {
    UBYTE name[kSampleNameMaxLen];
    UWORD length_w;
    UBYTE finetune;
    UBYTE volume;
    UWORD loop_start_w;
    UWORD loop_length_w;
  } sample_info[kNumSamples];

  UBYTE pat_tbl_size;
  UBYTE unused;
  UBYTE pat_tbl[128];
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
  PatternDivision divisions[kDivsPerPat];
} Pattern;

typedef struct {
  ModuleHeader header;
  Pattern patterns[];
} ModuleNonChip;

extern VOID module_open(STRPTR dir_path,
                        STRPTR file_name);
extern VOID module_close();
extern BOOL module_is_open();
extern Status module_load_header();
extern Status module_load_all();
extern ModuleHeader* module_header();
extern UWORD module_get_num_patterns();
extern ModuleNonChip* module_get_nonchip();
extern APTR module_get_samples();
extern ULONG module_get_samples_size();
