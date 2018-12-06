#pragma once

extern VOID mt_install_cia(APTR custom __asm("a6"),
                           APTR *AutoVecBase __asm("a0"),
                           UBYTE PALflag __asm("d0"));
extern VOID mt_remove_cia(APTR custom __asm("a6"));
extern VOID mt_init(APTR custom __asm("a6"),
                    APTR TrackerModule __asm("a0"),
                    APTR Samples __asm("a1"),
                    UBYTE InitialSongPos __asm("d0"));
extern VOID mt_end(APTR custom __asm("a6"));
extern VOID mt_mastervol(APTR custom __asm("a6"),
                         UWORD MasterVolume __asm("d0"));
extern VOID mt_music();
extern UWORD ms_camera_z_inc(UWORD block_gap_depth __asm("d0"));

extern volatile UBYTE mt_Enable;
extern volatile UBYTE ms_StepCount;
extern volatile UBYTE ms_HoldRows;
extern volatile UBYTE ms_SuppressSample;
