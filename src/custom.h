#pragma once

#include <hardware/custom.h>

extern struct Custom custom;

#define mCustomOffset(X) offsetof(struct Custom, X)

#define DMACONR_BBUSY    0x4000
#define VPOSR_V8         0x0001
#define VHPOSR_VALL      0xFF00
#define JOY0DAT_XALL     0x00FF
#define BLTCON0_ASH0_Shf 0xC
#define BLTCON0_USEA     0x0800
#define BLTCON0_USEB     0x0400
#define BLTCON0_USEC     0x0200
#define BLTCON0_USED     0x0100
#define BLTCON0_LF0_Shf  0x0
#define BLTCON1_BSH0_Shf 0xC
#define BLTCON1_TEX0_Shf 0xC
#define BLTCON1_SIGN_Shf 0x6
#define BLTCON1_AUL_Shf  0x2
#define BLTCON1_SING_Shf 0x1
#define BLTCON1_IFE      0x0008
#define BLTCON1_DESC     0x0002
#define BLTCON1_LINE     0x0001
#define BLTSIZE_H0_Shf   0x6
#define BLTSIZE_W0_Shf   0x0
#define DIWSTRT_V0_Shf   0x8
#define DIWSTOP_V0_Shf   0x8
#define DMACON_SPREN     0x0020
#define DMACON_BLTEN     0x0040
#define DMACON_COPEN     0x0080
#define DMACON_BPLEN     0x0100
#define DMACON_DMAEN     0x0200
#define DMACON_BLITPRI   0x0400
#define DMACON_CLEARALL  0x7FFF
#define DMACON_SET       0x8000
#define INTENA_PORTS     0x0008
#define INTENA_VERTB     0x0020
#define INTENA_CLEARALL  0x7FFF
#define INTENA_SET       0x8000
#define INTREQ_CLEARALL  0x7FFF
#define INTREQ_SET       0x8000
#define BPLCON0_COLOR    0x0200
#define BPLCON0_BPU_Shf  0xC
#define SPRxPOS_SV0_Shf  0x8
#define SPRxPOS_SH1_Shf  0x0
#define SPRxCTL_EV0_Shf  0x8
#define SPRxCTL_ATT_Shf  0x7
#define SPRxCTL_SV8_Shf  0x2
#define SPRxCTL_EV8_Shf  0x1
#define SPRxCTL_SH0_Shf  0x0
