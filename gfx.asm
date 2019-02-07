; -*- tab-width: 8; indent-tabs-mode: t; -*-

	section	code
	public	_update_coplist

	;; a0: UWORD z_until_vu
	;; a1: UWORD shift_err_inc
	;; a2: UWORD* colors
	;; a3: UWORD* cop_row
	;; a4: WORD* z_incs
	;; a6: TrackStep* step
	;; d2: WORD prev_shift_w
	;; d3: UWORD step_frac, UWORD shift_err
	;; d4: WORD shift_inc, WORD shift_x
	;; d5: TrackStep step_data
	;; d6: ULONG z
	;; d7: UWORD loop_count_top, UWORD loop_count_bottom

	macro	scanline_loop
.prev_scanline_\@:
	moveq	#$0,d0
	move.w	(a4)+,d0	; z_inc = *(z_incs ++)
	add.l	d0,d6		; z += z_inc
	add.w	d0,d3		; step_frac += z_inc
	move.w	#$DB6,d0	; kBlockGapDepth
	cmp.w	d0,d3
	blt	.no_step_\@	; step_frac < kBlockGapDepth
	sub.w	d0,d3		; step_frac -= kBlockGapDepth
	move.w	(a6)+,d5	; step_data = *(++ step)
.no_step_\@:

	move.w	d4,d0		; shift_x
	asr.w	#$4,d0		; shift_w = shift_x >> 4
	move.w	d0,d1
	addq.w	#$7,d0		; (kDispRowPadW - 1 + shift_w)
	sub.w	d2,d0		; (kDispRowPadW - 1 + (shift_w - prev_shift_w))
	asl.w	#$1,d0		; (kDispRowPadW - 1 + (shift_w - prev_shift_w)) << 1
	move.w	d1,d2		; prev_shift_w = shift_w

	lea	-$28(a3),a3	; cop_row -= 1 scanline
	move.w	d0,$6(a3)	; BPL1MOD
	move.w	d0,$A(a3)	; BPL2MOD

	move.w	d4,d0
	and.w	#$F,d0		; shift_x & $F
	move.w	d0,d1
	lsl.w	#$4,d1		; (shift_x & $F) << 4
	or.w	d1,d0		; (shift_x & $F) | ((shift_x & $F) << 4)

	move.w	d0,$E(a3)	; BPLCON1

	move.w	d6,d1
	rol.w	#$6,d1
	and.w	#$1E,d1
	move.w	$38(a2,d1.w),d1

	moveq	#$0,d0		; stripe_color = background
	btst	#$C,d6
	beq	.no_stripe_\@	; (z & $1000) == 0
	move.w	d1,d0		; stripe_color = foreground
.no_stripe_\@:
	move.w	d0,$12(a3)	; COLOR1 = stripe_color

	moveq	#$0,d0
	move.w	d0,$16(a3)	; COLOR2
	move.w	d0,$1A(a3)	; COLOR3
	move.w	d0,$1E(a3)	; COLOR4

	cmp.l	a0,d6
	bgt	.set_border_\@	; z > vu_meter_z
	moveq	#6,d0		; offset to vu_meter color
.set_border_\@:
	move.w	$30(a2,d0.w),$22(a3)	; COLOR5 = vu_meter or dark

	move.w	d5,d1		; step_data
	rol.w	#$4,d1		; 12'X, step_data.active_lane, 2'X
	and.w	#$C,d1		; step_data.active_lane << 2
	beq	.no_lane_\@	; step_data.active_lane == 0

	move.b	d5,d0		; step_data
	move.w	(a2,d0.w),d0	; lane_color = colors[step_data.color]
	move.w	d0,$12(a3,d1.w)	; COLOR[1 + step_data.active_lane] = lane_color
.no_lane_\@:

	swap	d3		; shift_err to low word
	add.w	a1,d3		; shift_err += shift_err_inc
	cmp.w	#$2000,d3
	blt	.no_shift_\@ 	; shift_err < $2000 (half a pixel)
	sub.w	#$4000,d3	; shift_err -= $4000 (one pixel)
	move.l	d4,d0
	swap	d0		; shift_x_inc to low word
	add.w	d0,d4		; shift_x += shift_x_inc
.no_shift_\@:
	swap	d3		; shift_err_inc to low word

	dbf	d7,.prev_scanline_\@
	endm

_update_coplist:
	movem.l	d0-d7/a0-a6,-(sp)
	moveq	#$0,d2		; prev_shift_w = 0
	move.w	(a6)+,d5	; step_data = *(++ step)

	;; Copperlist is segmented around extra wait on scanline $100.
	scanline_loop		; Bottom segment of display
	subq.l	#$4,a3		; Step backwards over extra wait
	swap	d7		; Remaining scanline count - 1
	scanline_loop		; Top segment of display

	;; Set up initial shift in top scanline preceding draw area.
	move.w	#$16,d0		; (((3 * kDispRowPadW) / 2) - 1) << 1
	asl.w	#$1,d2		; prev_shift_w << 1
	sub.w	d2,d0		; ((3 * kDispRowPadW) / 2) - 1 - prev_shift_w) << 1
	move.w	d0,-$22(a3)	; BPL1MOD
	move.w	d0,-$1E(a3)	; BPL2MOD

	movem.l	(sp)+,d0-d7/a0-a6
	rts
