@ SWI entry and exception returns (ARM60 datasheet): SWI saves CPSR in SPSR_svc,
@ enters SVC mode with I=1 (F, flags unchanged), puts the address of the next
@ instruction in R14_svc and jumps to 0x08. MOVS PC,R14, SUBS PC,R14,#n and
@ LDM {..,PC}^ restore CPSR from the SPSR of the current mode.
	.include "common.inc"

	.equ	SVC_STACK, 0x8000
	.equ	USR_STACK, 0x7000

reset_entry:
	ldr	sp, =SVC_STACK
	mov	r6, #0				@ SWI count, kept by the handlers

	@ 1. SWI from SVC mode: LR, SPSR, CPSR in the handler, return with MOVS PC,LR
	TEST	1
	msr	cpsr_f, #0xB0000000		@ N=1 C=1 V=1
swi1:	svc	#0x123456
	mrs	r7, cpsr			@ after the return
	CHECK	r6, 1
	CHECK	r7, 0xB00000D3			@ flags and mode restored from SPSR_svc
	CHECK	r4, swi1 + 4			@ R14_svc = SWI address + 4
	CHECK	r5, 0xB00000D3			@ SPSR_svc = CPSR at the SWI
	CHECK	r3, 0xB00000D3			@ CPSR in the handler: SVC, I=1 (already), F and flags kept

	@ 2. A conditional SWI that fails is not taken
	TEST	2
	msr	cpsr_f, #0x40000000		@ Z=1
	svcne	#0
	CHECK	r6, 1

	@ 3. SWI from User mode with IRQ/FIQ enabled (lines not driven): banked R13/R14,
	@    return with LDMFD SP!,{..,PC}^
	TEST	3
	mov	r0, #0x10
	msr	cpsr_fc, r0			@ User mode, I=0 F=0
	ldr	sp, =USR_STACK
	ldr	lr, =0x0BADC0DE
	msr	cpsr_f, #0x60000000		@ Z=1 C=1
swi3:	svc	#3
	mrs	r7, cpsr
	CHECK	r6, 2
	CHECK	r7, 0x60000010			@ back in User mode, flags restored
	CHECK	sp, USR_STACK			@ R13_usr untouched by the handler
	CHECK	lr, 0x0BADC0DE			@ R14_usr untouched
	CHECK	r4, swi3 + 4
	CHECK	r5, 0x60000010
	CHECK	r3, 0x60000093			@ SVC, I=1, F=0 kept, flags kept
	CHECK	r2, SVC_STACK			@ R13_svc seen by the handler

	@ 4. SWI from User mode, return with SUBS PC,R14,#0 (S bit, PC destination)
	TEST	4
	msr	cpsr_f, #0x90000000		@ N=1 V=1
swi4:	svc	#4
	mrs	r7, cpsr
	CHECK	r6, 3
	CHECK	r7, 0x90000010
	CHECK	r4, swi4 + 4

	@ 5. User-bank transfers from SVC mode (task switch): STM {R13,R14}^ stores the User
	@    registers, LDM {R13,R14}^ loads them, R13_svc/R14_svc untouched
	TEST	5
	ldr	sp, =USR_STACK - 0x100
	ldr	lr, =0x0E0E0E0E
	svc	#5
	CHECK	r6, 4
	CHECK	r1, USR_STACK - 0x100		@ User R13 stored by STM ^
	CHECK	r2, 0x0E0E0E0E			@ User R14
	CHECK	sp, 0x12345678			@ User R13 loaded by LDM ^
	CHECK	lr, 0x9ABCDEF0			@ User R14
	b	pass

swi_entry:
	mrs	r3, cpsr			@ CPSR in the handler
	mrs	r5, spsr			@ SPSR_svc
	mov	r4, lr				@ R14_svc
	mov	r2, sp				@ R13_svc
	add	r6, r6, #1
	msr	cpsr_f, #0x00000000		@ the return must restore the caller's flags
	cmp	r6, #4
	beq	swi_usr_bank
	cmp	r6, #2
	bgt	swi_ret_subs
	beq	swi_ret_ldm
	movs	pc, lr
swi_usr_bank:
	ldr	r0, =0x7800
	stmia	r0, {r13, r14}^			@ User R13/R14
	ldr	r1, [r0]
	ldr	r2, [r0, #4]
	ldr	r3, =0x12345678
	ldr	r4, =0x9ABCDEF0
	str	r3, [r0, #8]
	str	r4, [r0, #12]
	add	r5, r0, #8
	ldmia	r5, {r13, r14}^			@ new User R13/R14
	nop
	movs	pc, lr				@ R14_svc must still hold the return address
swi_ret_ldm:
	stmfd	sp!, {r0, lr}
	mov	r0, #0x55
	ldmfd	sp!, {r0, pc}^
swi_ret_subs:
	subs	pc, lr, #0
	.ltorg
