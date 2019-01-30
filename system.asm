; -*- tab-width: 8; indent-tabs-mode: t; -*-

	include	"ptplayer/cia.i"
	include	"ptplayer/custom.i"

INTREQR_PORTS_bit	= 3
CIAICR_SP_bit		= 3
CIACRA_SPMODE 		= 1<<6

	section	code
	public	_level2_int
	public	_key_state

_level2_int:
	;; Check if this is a PORTS interrupt
	movem.l	d0-d1/a0-a2,-(sp)
	lea	CUSTOM,a0
	btst.b	#INTREQR_PORTS_bit,INTREQR+1(a0)
	beq	.not_sp_int

	;; Check if interrupt was triggered due to CIAA serial port
	lea	CIAA,a1
	btst.b	#CIAICR_SP_bit,CIAICR(a1)
	beq	.not_sp_int

	;; Read keycode and update keyboard map
	;; Inverted keycode in bits [7:1], downstroke in bit 0
	moveq.l	#0,d0
	move.b	CIASDR(a1),d0
	not.b	d0
	lsr.b	#1,d0
	scc.b	d1
	lea	_key_state,a2
	move.b	d1,(a2,d0)

	;; Handshake with the keyboard controller
	;; Each scanline is 64us, wait 128-192us for safety
	or.b	#CIACRA_SPMODE,CIACRA(a1)
	moveq.l	#3-1,d1
.wait_one_scanline:
	move.b	VHPOSR(a0),d0
.wait_next_scanline:
	cmp.b	VHPOSR(a0),d0
	beq	.wait_next_scanline
	dbf	d1,.wait_one_scanline
	and.b	#~(CIACRA_SPMODE),CIACRA(a1)

.not_sp_int:
	;; Acknowledge PORTS interrupt
	;; Repeat twice to work around A4000 040/060 bug
	moveq.l	#(1<<INTREQR_PORTS_bit),d0
	move.w	d0,INTREQ(a0)
	move.w	d0,INTREQ(a0)

	movem.l	(sp)+,d0-d1/a0-a2
	rte

_key_state:
	ds.b	$80
