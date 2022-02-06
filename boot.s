# boot/boot.s

.section .init, "ax"
.global _start

_start:
    # check hart identifier
    csrr t0, mhartid
    bnez t0, 4f
    # cancel address translation & protection
    csrw satp, zero
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop
    # reset BSS
    la a0, __bss_start
    la a1, __bss_end
    bgeu a0, a1, 2f
1:
    sd zero, (a0)
    addi a0, a0, 8
    bltu a0, a1, 1b
2:
    la sp, __stack_top
    # enable machine mode interrupts
    li t0, (1 << 11) | (1 << 7) | (1 << 3)
    csrw mstatus, t0
    # write-in address of main
    la t1, main
    csrw mepc, t1
    # write-in trap vector
    la t2, mtrap_vector
    csrw mtvec, t2
    li t3, (1 << 11) | (1 << 7) | (1 << 3)
    csrw mie, t3
    la ra, 4f
    # return to main
    mret
4:
    wfi
    j 4b