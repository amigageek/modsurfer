#pragma once

#include <exec/types.h>
#include <hardware/custom.h>

extern void mt_install_cia(volatile struct Custom* custom __asm("a6"),
                           APTR *AutoVecBase __asm("a0"),
                           UBYTE PALflag __asm("d0"));
extern void mt_remove_cia(volatile struct Custom* custom __asm("a6"));
extern void mt_init(volatile struct Custom* custom __asm("a6"),
                    APTR TrackerModule __asm("a0"),
                    APTR Samples __asm("a1"),
                    UBYTE InitialSongPos __asm("d0"));
extern void mt_end(volatile struct Custom* custom __asm("a6"));
extern void mt_mastervol(volatile struct Custom* custom __asm("a6"),
                         UWORD MasterVolume __asm("d0"));
extern void mt_music();
extern UWORD ms_camera_z_inc(UWORD block_gap_depth __asm("d0"));

extern volatile UBYTE mt_Enable;
extern volatile UBYTE ms_StepCount;
extern volatile UBYTE ms_HoldRows;
extern volatile UBYTE ms_SuppressSample;
