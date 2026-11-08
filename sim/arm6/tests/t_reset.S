@ Reset through nRESET (ARM60 datasheet, "Reset"): when nRESET goes high again, the
@ ARM60 copies the PC and the CPSR into R14_svc and SPSR_svc, enters Supervisor mode
@ with I=1 and F=1 (the flags are kept) and fetches from address 0. The 3DO's CLIO
@ soft reset uses it: the boot ROM runs from address 0 with IRQ and FIQ disabled,
@ even if an interrupt line is active. The RAM keeps its contents, so a stage
@ number in RAM tells the reset entry which check to run.
@ Uses only MSR CPSR_fc with a register (no MSR #imm, no SPSR write).
@ maxcyc 100000
	.set	USE_IRQ, 1
	.set	USE_FIQ, 1
	.include "common.inc"

	.equ	IO,        0xF0000000
	.equ	SVC_STACK, 0x8000
	.equ	IRQ_STACK, 0x7C00
	.equ	USR_STACK, 0x7800
	.equ	VARS,      0x6000		@ +0 stage, +4 FIQ count, +8 IRQ count

reset_entry:
	mrs	r4, cpsr			@ state left by the reset, read before anything changes it
	mrs	r5, spsr
	mov	r6, lr
	ldr	r0, =VARS
	ldr	r1, [r0]
	cmp	r1, #1
	beq	after1
	cmp	r1, #2
	beq	after2
	cmp	r1, #3
	beq	after3

	@ power-on start
	mov	r1, #0
	str	r1, [r0, #4]
	str	r1, [r0, #8]
	ldr	r0, =0x000000D2
	msr	cpsr_fc, r0			@ IRQ mode: its stack
	ldr	sp, =IRQ_STACK
	ldr	r0, =0x000000D3
	msr	cpsr_fc, r0
	ldr	sp, =SVC_STACK

	@ 1. Reset in SVC mode with I=0, F=0, N=1, C=1 (no interrupt line active)
	TEST	1
	ldr	r0, =VARS
	mov	r1, #1
	str	r1, [r0]
	ldr	r1, =IO
	mov	r2, #24
	str	r2, [r1, #0x18]			@ nRESET low 24 cycles later
	ldr	r0, =0xA0000013			@ N=1 C=1, SVC, I=0 F=0
	msr	cpsr_fc, r0
slide1:
	.rept	64
	nop
	.endr
	b	no_reset

after1:
	CHECK	r4, 0xA00000D3			@ SVC, I=1 F=1, flags kept
	CHECK	r5, 0xA0000013			@ SPSR_svc = CPSR before the reset
	ldr	r0, =slide1
	sub	r6, r6, r0
	cmp	r6, #(64 * 4 + 8)
	movls	r6, #0				@ R14_svc = a PC inside the slide
	CHECK	r6, 0
	CHECK	sp, SVC_STACK			@ R13_svc kept

	@ 2. Reset in SVC mode with I=0, F=0 while the FIQ and IRQ lines become active:
	@    after the reset neither is taken until the program clears F, then I
	TEST	2
	ldr	r0, =VARS
	mov	r1, #2
	str	r1, [r0]
	ldr	r0, =0x00000013			@ SVC, I=0 F=0
	ldr	r1, =IO
	mov	r2, #24
	msr	cpsr_fc, r0
	str	r2, [r1, #0x18]			@ nRESET low from cycle +24 to +32
	str	r2, [r1, #0x14]			@ FIQ line active from about +26
	str	r2, [r1, #0x10]			@ IRQ line active from about +28
slide2:
	.rept	64
	nop
	.endr
	b	no_reset

after2:
	CHECK	r4, 0x000000D3			@ no FIQ/IRQ entry before the first instruction
	CHECK	r5, 0x00000013
	ldr	r0, =VARS
	ldr	r1, [r0, #4]
	CHECK	r1, 0				@ FIQ not taken
	ldr	r1, [r0, #8]
	CHECK	r1, 0				@ IRQ not taken
	TEST	3
	ldr	r0, =0x00000093			@ F=0: the pending FIQ is taken now
	msr	cpsr_fc, r0
	nop
	nop
	nop
	nop
	ldr	r0, =VARS
	ldr	r1, [r0, #4]
	CHECK	r1, 1
	ldr	r1, [r0, #8]
	CHECK	r1, 0
	ldr	r0, =0x00000013			@ I=0: then the IRQ
	msr	cpsr_fc, r0
	nop
	nop
	nop
	nop
	ldr	r0, =VARS
	ldr	r1, [r0, #8]
	CHECK	r1, 1

	@ 4. Reset in User mode: SPSR_svc holds the User CPSR, R13_svc is the SVC one
	TEST	4
	ldr	r0, =VARS
	mov	r1, #3
	str	r1, [r0]
	ldr	r1, =IO
	mov	r2, #24
	str	r2, [r1, #0x18]
	ldr	r0, =0x40000010			@ Z=1, User, I=0 F=0
	msr	cpsr_fc, r0
	ldr	sp, =USR_STACK
slide3:
	.rept	64
	nop
	.endr
	b	no_reset

after3:
	CHECK	r4, 0x400000D3
	CHECK	r5, 0x40000010
	ldr	r0, =slide3 - 8
	sub	r6, r6, r0
	cmp	r6, #(64 * 4 + 16)
	movls	r6, #0
	CHECK	r6, 0
	CHECK	sp, SVC_STACK
	b	pass

no_reset:
	ldr	r11, =0xDEAD0018
	b	fail_now

fiq_entry:					@ R8-R12 are banked: no save
	ldr	r8, =VARS
	ldr	r9, [r8, #4]
	add	r9, r9, #1
	str	r9, [r8, #4]
	mov	r8, #IO
	mov	r9, #0
	str	r9, [r8, #0x14]			@ release the line
	subs	pc, lr, #4

irq_entry:
	stmfd	sp!, {r0, r1}
	ldr	r0, =VARS
	ldr	r1, [r0, #8]
	add	r1, r1, #1
	str	r1, [r0, #8]
	mov	r0, #IO
	mov	r1, #0
	str	r1, [r0, #0x10]			@ release the line
	ldmfd	sp!, {r0, r1}
	subs	pc, lr, #4

swi_entry:
	ldr	r11, =0xDEAD0008
	b	fail_now
	.ltorg
