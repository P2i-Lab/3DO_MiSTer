@ IRQ and FIQ (ARM60 datasheet): an interrupt is taken between two instructions;
@ R14_<mode> = address of the next instruction + 4, SPSR_<mode> = CPSR; IRQ enters
@ mode 0x12 with I=1, FIQ mode 0x11 with I=1 and F=1 and its own R8-R14; FIQ has
@ priority over IRQ; SUBS PC,R14,#4 returns. The sweeps take an interrupt at every
@ cycle of a workload that uses every kind of instruction and check that it gives
@ the same result as without interrupt.
@ maxcyc 6000000
	.set	USE_IRQ, 1
	.set	USE_FIQ, 1
	.include "common.inc"

	.equ	IO,        0xF0000000
	.equ	SVC_STACK, 0x8000
	.equ	IRQ_STACK, 0x7C00
	.equ	VARS,      0x6000		@ +0 reference checksum, +4 IRQ delay, +8 FIQ delay, +12 sweep index
	.equ	LOG,       0x6100		@ IRQ: +0 count, +4 CPSR, +8 SPSR, +12 LR; FIQ: +16.. the same; +32 order
	.equ	WBUF,      0x6200		@ workload buffer, 64 words
	.equ	SWEEP,     200			@ interrupt delays tried, in cycles

	@ CHECKR a, b: fail unless registers a and b are equal (clobbers r10, r12, flags)
	.macro	CHECKR a, b
	mov	r10, \a
	mov	r12, \b
	cmp	r10, r12
	bne	fail_now
	.endm

	@ DELAY rd, kind, index: interrupt delay for a sweep index (rd != index)
	@   kind 0: none, 1: index, 2: index + (index & 15) + 1, 3: index + ((index >> 2) & 7) + 1
	.macro	DELAY rd, kind, index
	mov	\rd, #0
	cmp	\kind, #1
	moveq	\rd, \index
	cmp	\kind, #2
	andeq	\rd, \index, #15
	addeq	\rd, \rd, \index
	addeq	\rd, \rd, #1
	cmp	\kind, #3
	moveq	\rd, \index, lsr #2
	andeq	\rd, \rd, #7
	addeq	\rd, \rd, \index
	addeq	\rd, \rd, #1
	.endm

reset_entry:
	ldr	sp, =SVC_STACK
	ldr	r0, =0x000000D2
	msr	cpsr_fc, r0			@ IRQ mode: its stack
	ldr	sp, =IRQ_STACK
	ldr	r0, =0x000000D1
	msr	cpsr_fc, r0			@ FIQ mode: marker values in the banked R8-R12
	ldr	r8, =0xF1F10008
	ldr	r9, =0xF1F10009
	ldr	r10, =0xF1F1000A
	ldr	r11, =0xF1F1000B
	ldr	r12, =0xF1F1000C
	ldr	r0, =0x000000D3
	msr	cpsr_fc, r0			@ SVC mode, I=1 F=1
	ldr	r0, =LOG
	mov	r1, #0
	mov	r2, #16
1:	str	r1, [r0], #4
	subs	r2, r2, #1
	bne	1b

	@ 1. An IRQ waits while I=1, then is taken right after I is cleared:
	@    CPSR, SPSR_irq and R14_irq in the handler, every instruction run once
	TEST	1
	ldr	r1, =IO
	mov	r0, #1
	str	r0, [r1, #0x10]			@ IRQ line asserted
	mov	r4, #0
	.rept	8
	add	r4, r4, #1			@ masked: not taken
	.endr
	ldr	r0, =LOG
	ldr	r0, [r0]
	CHECK	r0, 0
	ldr	r0, =0xA0000053			@ N=1 C=1, SVC, I=0 F=1
	msr	cpsr_fc, r0
slide1:
	.rept	16
	add	r4, r4, #1
	.endr
	mrs	r5, cpsr
	CHECK	r4, 24
	CHECK	r5, 0xA0000053
	ldr	r1, =LOG
	ldr	r0, [r1]
	CHECK	r0, 1
	ldr	r0, [r1, #4]
	CHECK	r0, 0xA00000D2			@ IRQ mode, I=1, F=1 kept, flags kept
	ldr	r0, [r1, #8]
	CHECK	r0, 0xA0000053			@ SPSR_irq
	ldr	r0, [r1, #12]
	sub	r0, r0, #4			@ R14_irq - 4: first instruction run after the return
	ldr	r2, =slide1
	sub	r0, r0, r2
	cmp	r0, #4
	movls	r0, #0				@ taken after the MSR or after the next instruction
	CHECK	r0, 0

	@ 2. Same for FIQ: banked R8-R12, I and F set
	TEST	2
	ldr	r0, =0x500000D3			@ Z=1 V=1, I=1 F=1
	msr	cpsr_fc, r0
	ldr	r8, =0x08080808
	ldr	r9, =0x09090909
	ldr	r10, =0x0A0A0A0A
	ldr	r11, =0x0B0B0B0B
	ldr	r12, =0x0C0C0C0C
	ldr	r1, =IO
	mov	r0, #1
	str	r0, [r1, #0x14]			@ FIQ line asserted
	ldr	r0, =0x50000093			@ I=1 F=0
	msr	cpsr_fc, r0
slide2:
	.rept	16
	add	r4, r4, #1
	.endr
	mrs	r5, cpsr
	ldr	r1, =LOG
	ldr	r0, [r1, #16]
	ldr	r2, [r1, #20]
	ldr	r3, [r1, #24]
	ldr	r6, [r1, #28]
	ldr	r7, [r1, #32]
	CHECK	r4, 40
	CHECK	r5, 0x50000093
	CHECK	r0, 1				@ FIQ count
	CHECK	r2, 0x500000D1			@ FIQ mode, I=1, F=1
	CHECK	r3, 0x50000093			@ SPSR_fiq
	CHECK	r7, 0x00000012			@ handler order so far: IRQ (test 1), FIQ
	sub	r6, r6, #4
	ldr	r2, =slide2
	sub	r6, r6, r2
	cmp	r6, #4
	movls	r6, #0
	CHECK	r6, 0
	CHECK	r8, 0x08080808			@ the handler clobbered the FIQ bank only
	CHECK	r9, 0x09090909
	CHECK	r11, 0x0B0B0B0B
	ldr	r0, =0x000000D1
	msr	cpsr_fc, r0			@ look at the FIQ bank
	mov	r1, r8
	mov	r2, r12
	ldr	r0, =0x000000D3
	msr	cpsr_fc, r0
	CHECK	r1, IO				@ values left by the FIQ handler
	CHECK	r2, 0xFEEDFACE

	@ 3. IRQ and FIQ pending together: FIQ first, then IRQ (masked by the FIQ entry)
	TEST	3
	ldr	r1, =LOG
	mov	r0, #0
	str	r0, [r1, #32]
	ldr	r1, =IO
	mov	r0, #1
	str	r0, [r1, #0x10]
	str	r0, [r1, #0x14]
	nop
	nop
	ldr	r0, =0x00000013			@ I=0 F=0
	msr	cpsr_fc, r0
	.rept	16
	nop
	.endr
	ldr	r1, =LOG
	ldr	r0, [r1, #32]
	CHECK	r0, 0x00000021			@ FIQ (2), then IRQ (1)

	@ 4. IRQ at every cycle of the workload
	TEST	4
	ldr	r0, =0x00000013
	msr	cpsr_fc, r0
	mov	r4, #1				@ IRQ delay = sweep index
	mov	r5, #0				@ no FIQ
	bl	sweep

	@ 5. FIQ at every cycle of the workload
	TEST	5
	mov	r4, #0
	mov	r5, #1
	bl	sweep

	@ 6. IRQ and FIQ together, FIQ 1 to 16 cycles later (nested in the IRQ handler or not)
	TEST	6
	mov	r4, #1
	mov	r5, #2
	bl	sweep

	@ 7. IRQ and FIQ together, IRQ 1 to 8 cycles later
	TEST	7
	mov	r4, #3
	mov	r5, #1
	bl	sweep
	b	pass

@ sweep: run the workload once without interrupt, then SWEEP times with interrupts
@ whose delays are given by DELAY with the kinds in r4 (IRQ) and r5 (FIQ).
@ Fails with id = 0x100000 + index * 0x100 + test.
sweep:
	stmfd	sp!, {r11, lr}
	ldr	r1, =VARS
	mov	r0, #0
	str	r0, [r1, #4]
	str	r0, [r1, #8]
	stmfd	sp!, {r4, r5}
	bl	workload			@ reference
	ldmfd	sp!, {r4, r5}
	ldr	r1, =VARS
	str	r0, [r1]
	mov	r6, #1
sweep_loop:
	ldr	r1, =VARS
	str	r6, [r1, #12]
	DELAY	r0, r4, r6
	str	r0, [r1, #4]			@ IRQ delay
	DELAY	r0, r5, r6
	str	r0, [r1, #8]			@ FIQ delay
	ldr	r1, =LOG
	ldr	r7, [r1]			@ IRQ count before
	ldr	r8, [r1, #16]			@ FIQ count before
	cmp	r4, #0
	addne	r7, r7, #1			@ expected after
	cmp	r5, #0
	addne	r8, r8, #1
	stmfd	sp!, {r4-r8}
	bl	workload
	ldmfd	sp!, {r4-r8}
	ldr	r1, =LOG			@ wait for interrupts whose delay exceeds the workload
	mov	r2, #4096
1:	ldr	r3, [r1]
	ldr	r9, [r1, #16]
	cmp	r3, r7
	cmpeq	r9, r8
	beq	2f
	subs	r2, r2, #1
	bne	1b
2:	ldr	r11, [sp]			@ test id saved at the entry
	add	r11, r11, r6, lsl #8
	add	r11, r11, #0x100000
	ldr	r1, =VARS
	ldr	r1, [r1]
	CHECKR	r0, r1				@ same checksum as without interrupt
	CHECKR	r3, r7				@ IRQ taken once
	CHECKR	r9, r8				@ FIQ taken once
	ldr	r11, [sp]
	add	r6, r6, #1
	cmp	r6, #SWEEP
	ble	sweep_loop
	ldmfd	sp!, {r11, pc}

@ workload: arms the interrupts (delays in VARS), then runs a deterministic mix of
@ instructions on R0-R12, the flags and WBUF. Returns a checksum in R0.
workload:
	stmfd	sp!, {lr}
	ldr	r12, =WBUF
	ldr	r0, =0x13579BDF
	mov	r1, #64
1:	str	r0, [r12], #4			@ WBUF[i] = pattern
	add	r0, r0, r0, ror #7
	subs	r1, r1, #1
	bne	1b
	ldr	r12, =WBUF + 0x80
	ldr	r0, =0x01234567
	ldr	r1, =0x89ABCDEF
	ldr	r2, =0xDEADBEEF
	ldr	r3, =0x0F1E2D3C
	mov	r4, #0
	mvn	r5, #0
	mov	r6, #0x80000000
	mvn	r7, #0x80000000
	mov	r8, #0x55
	mov	r9, #0xAA
	mov	r10, #0x5A00
	mov	r11, #0xA5
	msr	cpsr_f, #0x90000000		@ N=1 V=1
	stmfd	sp!, {r0, r1}			@ arm the interrupts (delays in VARS, 0: none)
	ldr	r0, =VARS
	ldr	r1, [r0, #4]
	mov	r0, #IO
	str	r1, [r0, #0x10]
	ldr	r0, =VARS
	ldr	r1, [r0, #8]
	mov	r0, #IO
	str	r1, [r0, #0x14]
	ldmfd	sp!, {r0, r1}

	@ data processing, flags, conditions, shifts
	adds	r8, r6, r6			@ 0, C=1 V=1 Z=1
	adcs	r9, r7, r4			@ 0x80000000, N=1 V=1
	sbcs	r10, r1, r0, ror #7
	rsc	r11, r10, r9, asr #3
	movs	r8, r9, lsl #1
	addcs	r8, r8, #0x100
	eor	r9, r9, r2, lsr #16
	bic	r10, r10, r3
	mov	r4, #13
	orr	r11, r11, r1, ror r4
	teq	r11, r10
	rsbne	r4, r4, #0x40
	tst	r8, #0x80000000
	mvneq	r5, r5, lsr #1
	cmn	r5, r6
	movmi	r6, r6, asr #4
	@ multiply
	mul	r7, r9, r4
	mla	r8, r10, r11, r7
	muls	r9, r8, r4
	@ single transfers
	str	r7, [r12, #-4]!
	ldr	r0, [r12, #8]
	ldrb	r1, [r12, #5]
	strb	r1, [r12, #0x13]
	ldr	r2, [r12], #12
	str	r2, [r12, #-0x20]
	and	r3, r4, #7
	ldr	r3, [r12, r3, lsl #2]
	sub	r12, r12, #8
	@ block transfers
	stmdb	r12!, {r0-r3, r5}
	ldmia	r12!, {r6-r10}
	stmib	r12, {r4, r8, r11}
	ldmda	r12, {r0, r1}
	@ swap
	swp	r2, r9, [r12]
	swpb	r3, r10, [r12]
	@ branch with link
	bl	wl_sub
	b	wl_after
wl_sub:
	add	r11, r11, r3
	mov	pc, lr
wl_after:
	@ PSR transfers
	mrs	r4, cpsr
	msr	cpsr_f, #0x60000000		@ Z=1 C=1
	adcs	r5, r5, r4, lsr #24
	movvc	r5, r5, ror #3
	@ loads and moves to PC
	adr	r0, wl_p1
	str	r0, [r12, #0x20]
	ldr	pc, [r12, #0x20]
	add	r1, r1, #1			@ skipped
wl_p1:
	adr	r0, wl_p2
	str	r0, [r12, #0x24]
	add	r0, r12, #0x24
	ldmia	r0, {pc}
	add	r1, r1, #2			@ skipped
wl_p2:
	adr	r0, wl_p3
	mov	pc, r0
	add	r1, r1, #4			@ skipped
wl_p3:
	@ checksum of the flags, R1-R12 and WBUF
	mrs	r0, cpsr
	mov	r0, r0, lsr #28
	eor	r0, r0, r1
	add	r0, r0, r2, ror #1
	eor	r0, r0, r3, ror #2
	add	r0, r0, r4, ror #3
	eor	r0, r0, r5, ror #4
	add	r0, r0, r6, ror #5
	eor	r0, r0, r7, ror #6
	add	r0, r0, r8, ror #7
	eor	r0, r0, r9, ror #8
	add	r0, r0, r10, ror #9
	eor	r0, r0, r11, ror #10
	add	r0, r0, r12, ror #11
	ldr	r1, =WBUF
	mov	r2, #64
2:	ldr	r3, [r1], #4
	add	r0, r3, r0, ror #31
	subs	r2, r2, #1
	bne	2b
	ldmfd	sp!, {pc}

irq_entry:
	stmfd	sp!, {r0-r3}
	ldr	r0, =LOG
	ldr	r1, [r0]
	add	r1, r1, #1
	str	r1, [r0]			@ count
	mrs	r1, cpsr
	str	r1, [r0, #4]
	mrs	r1, spsr
	str	r1, [r0, #8]
	str	lr, [r0, #12]
	ldr	r1, [r0, #32]
	mov	r1, r1, lsl #4
	orr	r1, r1, #1
	str	r1, [r0, #32]			@ order: IRQ = 1
	mov	r0, #IO
	mov	r1, #0
	str	r1, [r0, #0x10]			@ release the line
	msr	cpsr_f, #0xF0000000		@ the return restores the flags
	ldmfd	sp!, {r0-r3}
	subs	pc, lr, #4

fiq_entry:					@ R8-R12 are banked: no save
	ldr	r8, =LOG
	ldr	r9, [r8, #16]
	add	r9, r9, #1
	str	r9, [r8, #16]
	mrs	r9, cpsr
	str	r9, [r8, #20]
	mrs	r9, spsr
	str	r9, [r8, #24]
	str	lr, [r8, #28]
	ldr	r9, [r8, #32]
	mov	r9, r9, lsl #4
	orr	r9, r9, #2
	str	r9, [r8, #32]			@ order: FIQ = 2
	mov	r8, #IO
	mov	r9, #0
	str	r9, [r8, #0x14]			@ release the line
	ldr	r10, =0xFEEDFACE
	mov	r11, r10
	mov	r12, r10
	msr	cpsr_f, #0xF0000000
	subs	pc, lr, #4

swi_entry:
	ldr	r11, =0xDEAD0008
	b	fail_now
	.ltorg
