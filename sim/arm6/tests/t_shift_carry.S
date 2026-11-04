@ Shifter carry-out with a zero shift amount (issue #12 and related cases):
@ the operand is unchanged and the carry-out is the current C flag.
	.include "common.inc"

reset_entry:
	@ 1. MOVS with an unrotated immediate keeps C=1 (issue #12 reproduction)
	TEST	1
	mov	r0, #0
	cmp	r0, r0			@ C=1
	movs	r0, #9
	adc	r1, r0, #0		@ 9 + 0 + C
	CHECK_NZCV 0b0010
	CHECK	r0, 9
	CHECK	r1, 10

	@ 2. MOVS with an unrotated immediate keeps C=0
	TEST	2
	SET_C0
	movs	r0, #9
	adc	r1, r0, #0
	CHECK_NZCV 0b0000
	CHECK	r1, 9

	@ 3. Register operand (LSL #0) keeps C=1
	TEST	3
	mov	r2, #0x80000000
	SET_C1
	movs	r0, r2
	CHECK_NZCV 0b1010
	CHECK	r0, 0x80000000

	@ 4. Register operand (LSL #0) keeps C=0
	TEST	4
	mov	r2, #1
	SET_C0
	movs	r0, r2
	CHECK_NZCV 0b0000

	@ 5-8. Shift by register with Rs[7:0]=0 keeps the operand and C
	@      (only the bottom byte of Rs counts)
	ldr	r2, =0x80000001
	mov	r3, #0x100
	TEST	5
	SET_C1
	movs	r0, r2, lsl r3
	CHECK_NZCV 0b1010
	CHECK	r0, 0x80000001
	TEST	6
	SET_C1
	movs	r0, r2, lsr r3
	CHECK_NZCV 0b1010
	CHECK	r0, 0x80000001
	TEST	7
	SET_C1
	movs	r0, r2, asr r3
	CHECK_NZCV 0b1010
	CHECK	r0, 0x80000001
	TEST	8
	SET_C1
	movs	r0, r2, ror r3
	CHECK_NZCV 0b1010
	CHECK	r0, 0x80000001

	@ 9-10. Rotated immediates set C from bit 31 of the result (control)
	TEST	9
	SET_C0
	movs	r0, #0x80000000
	CHECK_NZCV 0b1010
	TEST	10
	SET_C1
	movs	r0, #0x40000000
	CHECK_NZCV 0b0000

	@ 11. TST with an unrotated immediate keeps C
	TEST	11
	mov	r0, #3
	SET_C1
	tst	r0, #1
	CHECK_NZCV 0b0010

	@ 12. ANDS with a register operand keeps C
	TEST	12
	mov	r1, #6
	mov	r2, #3
	SET_C1
	ands	r0, r1, r2
	CHECK_NZCV 0b0010
	CHECK	r0, 2

	b	pass

swi_entry:
	ldr	r11, =0xDEAD0008
	b	fail_now
	.ltorg
