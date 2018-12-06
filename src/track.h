#pragma once

#include "common.h"

#define kNumVisibleSteps 16
#define kNumPaddingSteps (kNumVisibleSteps + 0x40) // 32 frame fade at speed 1 BPM 255
#define kNumStepsDelay 1

typedef struct {
  UBYTE active_lane:2;
  UBYTE collected:1;
  UBYTE sample:5;
} TrackStep;

typedef struct {
  struct {
    UWORD pitch;
    UWORD count;
    BOOL in_pattern;
  } per_sample[0x20];

  UWORD selected_sample;
} TrackScore;

Status track_build();
VOID track_free();
TrackStep* track_get_steps();
ULONG track_get_length();
UWORD track_get_num_blocks();
TrackScore* track_get_scores();
