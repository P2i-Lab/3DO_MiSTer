@ General regression checks for the ARM6 core: shifter, ALU with carry and overflow,
@ multiply, loads/stores (big-endian byte lanes), block transfers, SWP, branches,
@ conditions and PC reads.
	.include "common.inc"

@ SHIFT_CASE id, operand, result, nzcv: MOVS r0, <operand> must give <result> and the
@ flags <nzcv> whatever the initial carry (checked with C=0 as <id>, then with C=1 as <id>+0x100)
	.macro	SHIFT_CASE id, op, result, nzcv
	TEST	\id
	SET_C0
	movs	r0, \op
	mrs	r8, cpsr
	CHECK	r0, \result
	and	r8, r8, #0xF0000000
	CHECK	r8, (\nzcv << 28)
	TEST	(\id + 0x100)
	SET_C1
	movs	r0, \op
	mrs	r8, cpsr
	CHECK	r0, \result
	and	r8, r8, #0xF0000000
	CHECK	r8, (\nzcv << 28)
	.endm

reset_entry:
	ldr	r1, =0x87654321
	SHIFT_CASE  1, "r1, lsl #1", 0x0ECA8642, 0b0010
	SHIFT_CASE  2, "r1, lsl #4", 0x76543210, 0b0000
	SHIFT_CASE  3, "r1, lsr #1", 0x43B2A190, 0b0010
	SHIFT_CASE  4, "r1, lsr #4", 0x08765432, 0b0000
	SHIFT_CASE  5, "r1, asr #1", 0xC3B2A190, 0b1010
	SHIFT_CASE  6, "r1, asr #4", 0xF8765432, 0b1000
	SHIFT_CASE  7, "r1, ror #1", 0xC3B2A190, 0b1010
	SHIFT_CASE  8, "r1, ror #4", 0x18765432, 0b0000
	SHIFT_CASE  9, "r1, ror #8", 0x21876543, 0b0000
	b	1f
	.ltorg
1:
	mov	r3, #4
	SHIFT_CASE 10, "r1, lsl r3", 0x76543210, 0b0000
	SHIFT_CASE 11, "r1, lsr r3", 0x08765432, 0b0000
	SHIFT_CASE 12, "r1, asr r3", 0xF8765432, 0b1000
	SHIFT_CASE 13, "r1, ror r3", 0x18765432, 0b0000
	mov	r3, #32
	SHIFT_CASE 14, "r1, lsl r3", 0x00000000, 0b0110
	SHIFT_CASE 15, "r1, lsr r3", 0x00000000, 0b0110
	SHIFT_CASE 16, "r1, asr r3", 0xFFFFFFFF, 0b1010
	SHIFT_CASE 17, "r1, ror r3", 0x87654321, 0b1010
	b	1f
	.ltorg
1:
	mov	r3, #33
	SHIFT_CASE 18, "r1, lsl r3", 0x00000000, 0b0100
	SHIFT_CASE 19, "r1, lsr r3", 0x00000000, 0b0100
	mov	r3, #40
	SHIFT_CASE 20, "r1, asr r3", 0xFFFFFFFF, 0b1010
	mov	r3, #36
	SHIFT_CASE 21, "r1, ror r3", 0x18765432, 0b0000
	b	1f
	.ltorg
1:
	@ 30. 64-bit add with ADDS/ADC
	TEST	30
	mvn	r0, #0
	mov	r1, #1
	mov	r2, #1
	mov	r3, #0
	adds	r4, r0, r2		@ 0, C=1
	adc	r5, r1, r3		@ 1 + 0 + 1
	CHECK	r4, 0
	CHECK	r5, 2

	@ 31. 64-bit subtract with SUBS/SBC
	TEST	31
	mov	r0, #0
	mov	r1, #1
	mov	r2, #1
	mov	r3, #0
	subs	r4, r0, r2		@ 0xFFFFFFFF, C=0 (borrow)
	sbc	r5, r1, r3		@ 1 - 0 - 1
	CHECK	r4, 0xFFFFFFFF
	CHECK	r5, 0

	@ 32. RSC
	TEST	32
	mov	r0, #5
	SET_C1
	rsc	r2, r0, #10		@ 10 - 5 - (1 - C)
	CHECK	r2, 5
	SET_C0
	rsc	r2, r0, #10
	CHECK	r2, 4

	@ 33. Signed overflow
	TEST	33
	ldr	r0, =0x7FFFFFFF
	adds	r1, r0, #1		@ N=1 Z=0 C=0 V=1
	CHECK_NZCV 0b1001
	CHECK	r1, 0x80000000

	@ 34. MUL / MLA, including a full-length multiplier (Rs = -1)
	TEST	34
	ldr	r0, =12345
	ldr	r1, =6789
	mul	r2, r0, r1
	CHECK	r2, 83810205
	mov	r3, #100
	mla	r4, r0, r1, r3
	CHECK	r4, 83810305
	mvn	r0, #0
	mov	r1, #7
	mul	r2, r1, r0		@ 7 * -1 (Rs = 0xFFFFFFFF)
	CHECK	r2, 0xFFFFFFF9

	@ 35. Byte and word stores/loads (big-endian byte lanes, like the 3DO)
	TEST	35
	ldr	r9, =scratch
	ldr	r0, =0x11223344
	str	r0, [r9]
	mov	r1, #0xAA
	strb	r1, [r9, #1]
	ldr	r2, [r9]
	CHECK	r2, 0x11AA3344
	ldrb	r3, [r9, #2]
	CHECK	r3, 0x33
	ldrb	r3, [r9]
	CHECK	r3, 0x11

	@ 36. Pre-indexed and post-indexed addressing with writeback
	TEST	36
	ldr	r9, =scratch
	mov	r0, #0x55
	str	r0, [r9, #8]!
	CHECK	r9, (scratch + 8)
	ldr	r2, [r9], #4
	CHECK	r2, 0x55
	CHECK	r9, (scratch + 12)

	@ 37. STMIA/LDMIA with writeback, STMDB/LDMIA stack style
	TEST	37
	ldr	r9, =scratch
	mov	r0, #1
	mov	r1, #2
	mov	r2, #3
	stmia	r9!, {r0-r2}
	CHECK	r9, (scratch + 12)
	mov	r0, #0
	mov	r1, #0
	mov	r2, #0
	ldr	r9, =scratch
	ldmia	r9, {r0-r2}
	CHECK	r0, 1
	CHECK	r1, 2
	CHECK	r2, 3
	ldr	r9, =scratch_end
	mov	r0, #7
	mov	r1, #8
	stmdb	r9!, {r0, r1}
	mov	r0, #0
	mov	r1, #0
	ldmia	r9!, {r0, r1}
	CHECK	r0, 7
	CHECK	r1, 8
	CHECK	r9, scratch_end

	@ 38. SWP
	TEST	38
	ldr	r9, =scratch
	ldr	r0, =0xCAFEBABE
	str	r0, [r9]
	ldr	r1, =0x12345678
	swp	r2, r1, [r9]
	CHECK	r2, 0xCAFEBABE
	ldr	r3, [r9]
	CHECK	r3, 0x12345678

	@ 39. BL and return
	TEST	39
	mov	r0, #0
	bl	inc_r0
	bl	inc_r0
	CHECK	r0, 2

	@ 40. Conditional execution
	TEST	40
	mov	r0, #5
	cmp	r0, #5
	moveq	r1, #1
	movne	r1, #2
	CHECK	r1, 1
	cmp	r0, #6
	movlt	r2, #3
	movge	r2, #4
	CHECK	r2, 3

	@ 41. Reading PC gives the address of the instruction + 8
	TEST	41
pc_here:
	mov	r0, pc
	CHECK	r0, (pc_here + 8)

	@ 42. LDM loading PC
	TEST	42
	ldr	r9, =scratch
	ldr	r0, =ldm_target
	str	r0, [r9]
	ldmia	r9, {pc}
	ldr	r11, =0xDEAD0042
	b	fail_now
ldm_target:
	b	pass

inc_r0:
	add	r0, r0, #1
	mov	pc, lr

swi_entry:
	ldr	r11, =0xDEAD0008
	b	fail_now
	.ltorg

	.align	2
scratch:
	.space	64
scratch_end:
