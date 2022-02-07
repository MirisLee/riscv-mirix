/* init/setup.c */

#include <asm.h>
#include <mem.h>

extern void main();
void timer_init();
extern void timer_vec();    /* kernel/kvec.s */

__attribute__((aligned(16)))
char stack0[4096];
long timer_area[5];

void setup() {
    /* set MPP mode to supervisor */
    unsigned long status = r_mstatus();
    status &= ~MSTATUS_MPP_MASK;
    status |= MSTATUS_MPP_S;
    w_mstatus(status);

    w_mepc((long )main);
    w_stap(0);

    /* delegate all interrupts/exceptions to supervisor mode */
    w_mideleg(0xffff);
    w_medeleg(0xffff);
    w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);

    /* configure physical memory protection */
    w_pmpaddr0(0x3fffffffffffffull);
    w_pmpcfg0(0xf);

    timer_init();

    int id = r_mhartid();
    w_tp(id);
    _mret();
}

void timer_init() {
    int interval = 1000000;
    *(long *)CLINT_MTIMECMP = *(long *)CLINT_MTIME + interval;

    /* prepare for timer_vec */
    long *scratch = &timer_area[0];
    scratch[3] = CLINT_MTIMECMP;
    scratch[4] = interval;
    w_mscratch((long )scratch);

    w_mtvec((long )timer_vec);
    w_mstatus(r_mstatus() | MSTATUS_MIE);
    w_mie(r_mie() | MIE_MTIE);
}