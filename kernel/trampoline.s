# kernel/trampoline.s

.section trampsec
.globl trampoline
trampoline:
.align 4
.globl uvec
uvec:
	csrrw a0, sscratch, a0
	
	# save user registers in TRAPFRAME
	sd ra, 40(a0)
	sd sp, 48(a0)
	sd gp, 56(a0)
	sd tp, 64(a0)
	sd t0, 72(a0)
	sd t1, 80(a0)
	sd t2, 88(a0)
	sd s0, 96(a0)
	sd s1, 104(a0)
	sd a1, 120(a0)
	sd a2, 128(a0)
	sd a3, 136(a0)
	sd a3, 136(a0)
	sd a4, 144(a0)
	sd a5, 152(a0)
	sd a6, 160(a0)
	sd a7, 168(a0)
	sd s2, 176(a0)
	sd s3, 184(a0)
	sd s4, 192(a0)
	sd s5, 200(a0)
	sd s6, 208(a0)
	sd s7, 216(a0)
	sd s8, 224(a0)
	sd s9, 232(a0)
	sd s10, 240(a0)
	sd s11, 248(a0)
	sd t3, 256(a0)
	sd t4, 264(a0)
	sd t5, 272(a0)
	sd t6, 280(a0)

	# save the user a0
	csrr t0, sscratch
	sd t0, 112(a0)

	# restore kernel sp
	ld sp, 8(a0)

	# hold current hartid
	ld tp, 32(a0)
	
	# load the address of utrap()
	ld t0, 16(a0)

	# restore kpagetable
	ld t1, 0(a0)
	csrw satp, t1
	sfence.vma zero, zero

	jr t0

.globl uretn
uretn:
	# switch pagetable
	scrw stap, a1
	sfence.vma zero, zero

	# put the user a0
	ld t0, 112(a0)
	csrw sscratch, t0

	# retore registers
	ld ra, 40(a0)
	ld sp, 48(a0)
	ld gp, 56(a0)
	ld tp, 64(a0)
	ld t0, 72(a0)
	ld t1, 80(a0)
	ld t2, 88(a0)
	ld s0, 96(a0)
	ld s1, 104(a0)
	ld a1, 120(a0)
	ld a2, 128(a0)
	ld a3, 136(a0)
	ld a3, 136(a0)
	ld a4, 144(a0)
	ld a5, 152(a0)
	ld a6, 160(a0)
	ld a7, 168(a0)
	ld s2, 176(a0)
	ld s3, 184(a0)
	ld s4, 192(a0)
	ld s5, 200(a0)
	ld s6, 208(a0)
	ld s7, 216(a0)
	ld s8, 224(a0)
	ld s9, 232(a0)
	ld s10, 240(a0)
	ld s11, 248(a0)
	ld t3, 256(a0)
	ld t4, 264(a0)
	ld t5, 272(a0)
	ld t6, 280(a0)

	# restore a0 and save TRAPFRAME
	csrrw a0, sscratch, a0

	sret
