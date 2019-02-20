#pragma once

#include <hardware/custom.h>

extern volatile struct Custom custom;

#define CUSTOM_OFFSET(X) offsetof(struct Custom, X)

#define BLTCON0_ASH0_SHF 0xC
#define BLTCON0_USEA     0x0800
#define BLTCON0_USEB     0x0400
#define BLTCON0_USEC     0x0200
#define BLTCON0_USED     0x0100
#define BLTCON0_LF0_SHF  0x0
#define BLTCON1_BSH0_SHF 0xC
#define BLTCON1_TEX0_SHF 0xC
#define BLTCON1_SIGN_SHF 0x6
#define BLTCON1_AUL_SHF  0x2
#define BLTCON1_SING_SHF 0x1
#define BLTCON1_IFE      0x0008
#define BLTCON1_DESC     0x0002
#define BLTCON1_LINE     0x0001
#define BLTSIZE_H0_SHF   0x6
#define BLTSIZE_W0_SHF   0x0
#define BPLCON0_BPU_SHF  0xC
#define BPLCON0_COLOR    0x0200
#define COPCON_CDANG     0x2
#define DIWHIGH_H10_SHF  0xD
#define DIWHIGH_V8_SHF   0x8
#define DIWSTOP_V0_SHF   0x8
#define DIWSTRT_V0_SHF   0x8
#define DMACON_SET       0x8000
#define DMACON_CLEARALL  0x7FFF
#define DMACON_BLITPRI   0x0400
#define DMACON_DMAEN     0x0200
#define DMACON_BPLEN     0x0100
#define DMACON_COPEN     0x0080
#define DMACON_BLTEN     0x0040
#define DMACON_SPREN     0x0020
#define DMACONR_BBUSY    0x4000
#define INTENA_SET       0x8000
#define INTENA_CLEARALL  0x7FFF
#define INTENA_PORTS     0x0008
#define INTREQ_SET       0x8000
#define INTREQ_CLEARALL  0x7FFF
#define JOYxDAT_XALL     0x00FF
#define JOYxDAT_Y1       0x0200
#define JOYxDAT_X1       0x0002
#define SPRxCTL_EV0_SHF  0x8
#define SPRxCTL_ATT_SHF  0x7
#define SPRxCTL_SV8_SHF  0x2
#define SPRxCTL_EV8_SHF  0x1
#define SPRxCTL_SH0_SHF  0x0
#define SPRxPOS_SV0_SHF  0x8
#define SPRxPOS_SH1_SHF  0x0
#define VHPOSR_VALL      0xFF00
#define VPOSR_V8         0x0001
