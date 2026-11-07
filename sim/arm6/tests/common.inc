@ Common startup code, macros and result reporting for the ARM6 core tests.
@ Assembled big-endian (-EB) like the 3DO, linked at address 0 (see run.sh).
@
@ Result protocol (see tb_arm6.sv):
@   success: write 0x0000600D to 0xF0000000
@   failure: write the test id, the actual and the expected value to
@            0xF0000004, 0xF0000008, 0xF000000C, then 0x00000BAD to 0xF0000000
@ Each test file defines reset_entry and swi_entry. A test that takes interrupts
@ sets USE_IRQ and/or USE_FIQ before including this file and defines irq_entry
@ and/or fiq_entry. The interrupt lines are driven through 0xF0000010 (IRQ) and
@ 0xF0000014 (FIQ): writing 0 releases the line, n > 0 asserts it n cycles later.

	.syntax	unified
	.arm
	.text
	.global	_start
_start:
	b	reset_entry		@ 0x00 reset
	b	vec_undef		@ 0x04 undefined instruction
	b	swi_entry		@ 0x08 SWI
	b	vec_unexpected		@ 0x0C prefetch abort
	b	vec_unexpected		@ 0x10 data abort
	b	vec_unexpected		@ 0x14 address exception
	.ifdef	USE_IRQ
	b	irq_entry		@ 0x18 IRQ
	.else
	b	vec_unexpected		@ 0x18 IRQ
	.endif
	.ifdef	USE_FIQ
	b	fiq_entry		@ 0x1C FIQ
	.else
	b	vec_unexpected		@ 0x1C FIQ
	.endif

vec_undef:
	ldr	r11, =0xDEAD0004
	b	fail_now
vec_unexpected:
	ldr	r11, =0xDEAD0000
	b	fail_now

@ TEST id: id reported if one of the following checks fails
	.macro	TEST id
	ldr	r11, =\id
	.endm

@ CHECK reg, value: fail unless reg == value (clobbers r10, r12 and the flags)
	.macro	CHECK reg, value
	mov	r10, \reg
	ldr	r12, =\value
	cmp	r10, r12
	bne	fail_now
	.endm

@ CHECK_CPSR value: fail unless CPSR == value (clobbers r10, r12 and the flags)
	.macro	CHECK_CPSR value
	mrs	r10, cpsr
	ldr	r12, =\value
	cmp	r10, r12
	bne	fail_now
	.endm

@ CHECK_NZCV nzcv: fail unless CPSR[31:28] == nzcv, e.g. 0b0010 for "only C set"
	.macro	CHECK_NZCV nzcv
	mrs	r10, cpsr
	and	r10, r10, #0xF0000000
	ldr	r12, =(\nzcv << 28)
	cmp	r10, r12
	bne	fail_now
	.endm

@ Known flag states, set with arithmetic only (no MSR)
	.macro	SET_C1			@ N=0 Z=1 C=1 V=0
	mov	r10, #0
	cmp	r10, #0
	.endm
	.macro	SET_C0			@ N=1 Z=0 C=0 V=0
	mov	r10, #0
	cmp	r10, #1
	.endm

pass:
	mov	r9, #0xF0000000
	ldr	r0, =0x0000600D
	str	r0, [r9]
1:	b	1b

fail_now:
	mov	r9, #0xF0000000
	str	r11, [r9, #4]
	str	r10, [r9, #8]
	str	r12, [r9, #12]
	ldr	r0, =0x00000BAD
	str	r0, [r9]
1:	b	1b
	.ltorg
