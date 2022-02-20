# boot/boot.s

.section .init, "ax"
.globl _start

_start:
    # set up stack
    la sp, stack0
    csrr a0, mhartid
    bnez a0, spin
    addi a0, a0, 1
    li a1, 12
    sll a0, a0, a1
    add sp, sp, a0  # sp = sp + 4096
    call setup

spin:
    j spin