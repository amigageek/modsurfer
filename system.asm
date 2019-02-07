; -*- tab-width: 8; indent-tabs-mode: t; -*-

	include	"ptplayer/cia.i"
	include	"ptplayer/custom.i"

CIAICR_SP_BIT		= 3
CIACRA_SPMODE 		= 1<<6
INTREQ_PORTS		= 1<<3
INTREQR_PORTS_BIT	= 3

	section	code
	public	_level2_int
	public	_keyboard_state

_level2_int:
	movem.l	d0-d1/a0-a2,-(sp)

	;; Check if this is a PORTS interrupt.
	lea	CUSTOM,a0
	btst	#INTREQR_PORTS_BIT,INTREQR+1(a0)
	beq	.not_serial_int

	;; Check if interrupt was triggered by CIAA serial port.
	lea	CIAA,a1
	btst	#CIAICR_SP_BIT,CIAICR(a1)
	beq	.not_serial_int

	;; Read keycode and update keyboard map ($FF = pressed, $00 = not).
	;; Inverted keycode in bits [7:1], downstroke in bit 0.
	moveq	#0,d0
	move.b	CIASDR(a1),d0
	not.b	d0
	lsr.b	#1,d0
	scc	d1
	lea	_keyboard_state,a2
	move.b	d1,(a2,d0)

	;; Handshake with the keyboard controller.
	;; Each scanline is 64us, wait 128-192us for safety.
	or.b	#CIACRA_SPMODE,CIACRA(a1)
	moveq	#3-1,d1
.wait_one_scanline:
	move.b	VHPOSR(a0),d0
.wait_next_scanline:
	cmp.b	VHPOSR(a0),d0
	beq	.wait_next_scanline
	dbf	d1,.wait_one_scanline
	and.b	#~(CIACRA_SPMODE),CIACRA(a1)

.not_serial_int:
	;; Acknowledge PORTS interrupt.
	;; Repeat twice to work around A4000 040/060 bug.
	moveq	#INTREQ_PORTS,d0
	move.w	d0,INTREQ(a0)
	move.w	d0,INTREQ(a0)

	movem.l	(sp)+,d0-d1/a0-a2
	rte

_keyboard_state:
	ds.b	$80
