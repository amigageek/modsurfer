#pragma once

#include "common.h"

#define kNumVisibleSteps 16
#define kNumPaddingSteps (kNumVisibleSteps + 0x40) // 32 frame fade out at speed 1 BPM 255
#define kNumStepsDelay 1
#define kNumBlockColors 12

typedef struct {
  UWORD active_lane:2;
  UWORD sample:5;
  UWORD unused1:3;
  UWORD color:5;
  UWORD unused2:1;
} TrackStep;

void track_init();
Status track_build();  // StatusError, StatusOutOfMemory
void track_free();
TrackStep* track_steps();
UWORD track_unpadded_length();
UWORD track_num_blocks();
