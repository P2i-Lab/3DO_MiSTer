@ Immediate shift encodings with a zero amount (issue #11 and related cases):
@ LSR #0 means LSR #32 and ASR #0 means ASR #32 (ROR #0 means RRX).
	.include "common.inc"

reset_entry:
	@ 1. EORS with ASR #32, negative operand (issue #11 reproduction)
	TEST	1
	ldr	r2, =0x299D63D1
	ldr	r4, =0xED64C665
	SET_C0
	eors	r0, r2, r4, asr #32	@ operand 0xFFFFFFFF, C = bit 31 of r4
	CHECK_NZCV 0b1010
	CHECK	r0, 0xD6629C2E

	@ 2. ASR #32, positive operand: operand 0, C=0
	TEST	2
	ldr	r4, =0x6D64C665
	SET_C1
	eors	r0, r2, r4, asr #32
	CHECK_NZCV 0b0000
	CHECK	r0, 0x299D63D1

	@ 3. ASR #31 (control)
	TEST	3
	ldr	r4, =0xED64C665
	SET_C0
	eors	r0, r2, r4, asr #31	@ operand 0xFFFFFFFF, C = bit 30 of r4
	CHECK_NZCV 0b1010
	CHECK	r0, 0xD6629C2E

	@ 4. LSR #32: result 0, C = bit 31
	TEST	4
	ldr	r4, =0x80000001
	SET_C0
	movs	r0, r4, lsr #32
	CHECK_NZCV 0b0110
	CHECK	r0, 0

	@ 5. LSR #32 with bit 31 clear: C=0
	TEST	5
	ldr	r4, =0x7FFFFFFF
	SET_C1
	movs	r0, r4, lsr #32
	CHECK_NZCV 0b0100
	CHECK	r0, 0

	@ 6. LSR #31 (control)
	TEST	6
	ldr	r4, =0xC0000000
	SET_C0
	movs	r0, r4, lsr #31		@ C = bit 30
	CHECK_NZCV 0b0010
	CHECK	r0, 1

	@ 7. RRX (ROR #0, control): C goes to bit 31, bit 0 goes to C
	TEST	7
	mov	r4, #3
	SET_C1
	movs	r0, r4, rrx
	CHECK_NZCV 0b1010
	CHECK	r0, 0x80000001

	@ 8. LDR with a scaled register offset LSR #32 uses offset 0
	TEST	8
	ldr	r1, =data_word
	mvn	r2, #0
	ldr	r0, [r1, r2, lsr #32]
	CHECK	r0, 0x12345678

	@ 9. LDR with a scaled register offset ASR #32 (positive index) uses offset 0
	TEST	9
	ldr	r2, =0x40000000
	ldr	r0, [r1, r2, asr #32]
	CHECK	r0, 0x12345678

	b	pass

swi_entry:
	ldr	r11, =0xDEAD0008
	b	fail_now

	.align	2
data_word:
	.word	0x12345678
	.ltorg
