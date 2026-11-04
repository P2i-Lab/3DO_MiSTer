@ Unaligned word loads (ARM60 datasheet, big-endian configuration): LDR and SWP rotate the
@ addressed word right by 8 * address[1:0], so that an offset of 0 or 2 puts the addressed
@ byte in bits 31-24 (a halfword lands in bits 31-16) and an offset of 1 or 3 puts it in
@ bits 15-8. LDM ignores address[1:0]; byte loads are not rotated.
	.include "common.inc"

reset_entry:
	ldr	r9, =data_word		@ 0x11223344: bytes 11 22 33 44 (big-endian)

	@ 1-4. LDR at offsets 0..3
	TEST	1
	ldr	r0, [r9]
	CHECK	r0, 0x11223344
	TEST	2
	ldr	r0, [r9, #1]
	CHECK	r0, 0x44112233
	TEST	3
	ldr	r0, [r9, #2]
	CHECK	r0, 0x33441122
	TEST	4
	ldr	r0, [r9, #3]
	CHECK	r0, 0x22334411

	@ 5. Halfword idiom from the datasheet: LDR at offset 2, then LSR #16
	TEST	5
	ldr	r0, [r9, #2]
	mov	r0, r0, lsr #16
	CHECK	r0, 0x3344

	@ 6. Register offset, pre-indexed with writeback
	TEST	6
	mov	r1, #1
	mov	r2, r9
	ldr	r0, [r2, r1]!
	CHECK	r0, 0x44112233
	CHECK	r2, (data_word + 1)

	@ 7. Post-indexed: the access uses the (unaligned) base, then the base is updated
	TEST	7
	ldr	r0, [r2], #-1
	CHECK	r0, 0x44112233
	CHECK	r2, data_word

	@ 8. Byte loads are not rotated (control)
	TEST	8
	ldrb	r0, [r9, #1]
	CHECK	r0, 0x22

	@ 9. SWP at an unaligned address: rotated load, word store to the aligned word
	TEST	9
	ldr	r9, =swap_word		@ 0x55667788
	ldr	r1, =0xA1B2C3D4
	add	r3, r9, #2
	swp	r0, r1, [r3]
	CHECK	r0, 0x77885566
	ldr	r0, [r9]
	CHECK	r0, 0xA1B2C3D4

	@ 10. LDM ignores address[1:0]: no rotation (control)
	TEST	10
	ldr	r9, =data_word
	add	r3, r9, #1
	ldmia	r3, {r0, r1}
	CHECK	r0, 0x11223344
	CHECK	r1, 0x99AABBCC

	b	pass

swi_entry:
	ldr	r11, =0xDEAD0008
	b	fail_now
	.ltorg

	.align	2
data_word:
	.word	0x11223344
	.word	0x99AABBCC
swap_word:
	.word	0x55667788
