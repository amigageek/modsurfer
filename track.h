#pragma once

#include "common.h"

#define kNumVisibleSteps 16
#define kNumPaddingSteps (kNumVisibleSteps + 0x40) // 32 frame fade at speed 1 BPM 255
#define kNumStepsDelay 1

typedef struct {
  UWORD active_lane:2;
  UWORD sample:5;
  UWORD unused1:3;
  UWORD color:5;
  UWORD unused2:1;
} TrackStep;

VOID track_init();
Status track_build();
VOID track_free();
TrackStep* track_get_steps();
ULONG track_get_length();
UWORD track_get_num_blocks();
