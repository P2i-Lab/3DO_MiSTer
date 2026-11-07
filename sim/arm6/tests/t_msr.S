@ MSR/MRS field handling (issue #10 and related cases). ARMv3 encodings:
@   MSR CPSR_all/SPSR_all (field mask 1001) writes the flags and the control bits,
@   MSR CPSR_flg/SPSR_flg (field mask 1000) writes only the flags, from a register
@   or a rotated immediate. In User mode only the flags can be changed.
	.include "common.inc"

reset_entry:
	@ 1. Reset state: SVC mode, IRQ and FIQ disabled
	TEST	1
	CHECK_CPSR 0x000000D3

	@ 2. MSR CPSR_flg, Rm in SVC mode writes only the flags (issue #10 reproduction)
	TEST	2
	mrs	r2, cpsr
	bic	r2, r2, #0xF0000000	@ drop the flags left by the previous check
	eor	r2, r2, #3		@ operand mode bits = User
	orr	r2, r2, #0xA0000000	@ N=1 C=1
	msr	cpsr_f, r2
	CHECK_CPSR 0xA00000D3

	@ 3. MSR CPSR_all, Rm writes flags and control bits (to IRQ mode and back)
	TEST	3
	ldr	r2, =0x600000D2
	msr	cpsr_fc, r2
	CHECK_CPSR 0x600000D2
	ldr	r2, =0x000000D3
	msr	cpsr_fc, r2
	CHECK_CPSR 0x000000D3

	@ 4. MSR SPSR_all, Rm then MSR SPSR_flg, Rm
	TEST	4
	ldr	r2, =0x000000D2
	msr	spsr_fc, r2
	mrs	r3, spsr
	CHECK	r3, 0x000000D2
	ldr	r2, =0xF0000013
	msr	spsr_f, r2		@ flags only
	mrs	r3, spsr
	CHECK	r3, 0xF00000D2

	@ 5. MSR CPSR_flg, #imm
	TEST	5
	msr	cpsr_f, #0x50000000	@ Z=1 V=1
	CHECK_CPSR 0x500000D3

	@ 6. MSR SPSR_flg, #imm
	TEST	6
	msr	spsr_f, #0x30000000	@ C=1 V=1
	mrs	r3, spsr
	CHECK	r3, 0x300000D2

	@ 7. SPSRs are banked: SPSR_irq and SPSR_svc are separate registers
	TEST	7
	ldr	r2, =0x000000D2
	msr	cpsr_fc, r2		@ IRQ mode
	ldr	r2, =0x80000010
	msr	spsr_fc, r2		@ SPSR_irq
	ldr	r2, =0x000000D3
	msr	cpsr_fc, r2		@ SVC mode
	mrs	r3, spsr
	CHECK	r3, 0x300000D2		@ SPSR_svc unchanged
	ldr	r2, =0x000000D2
	msr	cpsr_fc, r2		@ IRQ mode
	mrs	r3, spsr
	CHECK	r3, 0x80000010
	ldr	r2, =0x000000D3
	msr	cpsr_fc, r2		@ SVC mode

	@ 8. In User mode MSR CPSR_all only changes the flags; SWI goes back to SVC mode
	TEST	8
	ldr	r2, =0x00000010
	msr	cpsr_fc, r2		@ User mode, IRQ/FIQ enabled (not driven by the bench)
	ldr	r2, =0xF00000D3
	msr	cpsr_fc, r2		@ only N Z C V change
	mrs	r5, cpsr
	svc	#0
	ldr	r11, =0xDEAD0010	@ not reached: the SWI handler ends the test
	b	fail_now

swi_entry:
	CHECK	r5, 0xF0000010
	mrs	r3, spsr
	CHECK	r3, 0xF0000010		@ SPSR_svc = CPSR at the SWI
	mrs	r3, cpsr
	and	r3, r3, #0xFF
	CHECK	r3, 0x93		@ SVC mode, I=1, F unchanged
	b	pass
	.ltorg
