#pragma once

#include "common.h"

#define kNumVisibleSteps 16
#define kNumPaddingSteps (kNumVisibleSteps + 0x40) // 32 frame fade at speed 1 BPM 255
#define kNumStepsDelay 1

typedef struct {
  UWORD active_lane:2;
  UWORD collected:1;
  UWORD sample:5;
  UWORD color:4;
  UWORD unused:4;
} TrackStep;

typedef struct {
  struct {
    UWORD pitch;
    UWORD count;
    BOOL in_pattern;
  } per_sample[0x20];

  UWORD selected_sample;
} TrackScore;

VOID track_init();
Status track_build();
VOID track_free();
TrackStep* track_get_steps();
ULONG track_get_length();
UWORD track_get_num_blocks();
TrackScore* track_get_scores();
