/* kernel/trap.c */
#include <asm.h>
#include <mem.h>
#include <kernel.h>
#include <spinlock.h>
#include <proc.h>

struct spinlock tickslock;
unsigned int ticks;

extern char trampoline[], uvec[], uretn[];	/* kernel/trampoline.s */

void kvec();	/* kernel/kvec.s */

extern int dev_intr();

void trap_init(void) {
	initlock(&tickslock, "time");
	w_stvec((long )kvec);
}

void utrap(void) {
	int dev_nr = 0;

	if (r_sstatus() & SSTATUS_SPP)
		panic("utrap: mode error");

	/* send intrs to ktrap() */
	w_stvec((long )kvec);
	struct proc *p = myproc();
	p->trapframe->epc = r_sepc();
	if (r_scause() == 8) {	/* syscall */
		if (p->killed) exit(-1);
		p->trapframe->epc += 4; 	/* point to the next instruction */
		intr_on();
		syscall();
	} else if (!(dev_nr = dev_intr())) {
		printk("utrap: unexpected scause %p\tpid=%d\n", r_scause(), p->pid);
		printk("\tsepc=%p\tstval=%p\n", r_sepc(), r_stval());
		p->killed = 1;
	}
	if (p->killed) exit(-1);

	if (dev_nr == 2)	/* timer_intr */
		yield();
	utrapretn();
}

void utrapretn(void) {
	struct proc *p = myproc();
	
	intr_off();

	/* send syscalls, intrs and exptions to trampoline.s */
	w_stvec(TRAMPOLINE + (uvec - trampoline));

	/* set up trapframe */
	p->trapframe->ksatp = r_satp();
	p->trapframe->ksp = p->kstack + PGSIZE;
	p->trapframe->ktrap = (unsigned long )utrap;
	p->trapframe->khartid = r_tp();

	unsigned long status = r_sstatus();
	status &= ~SSTATUS_SPP;
	status |= SSTATUS_SPIE;
	w_sstatus(status);
	
	w_spec(p->trapframe->epc);

	unsigned long satp = MAKE_SATP(p->pagetable);
	unsigned long func = TRAMPOLINE + (uretn - trampoline);
	((void (*)(unsigned long, unsigned long))func)(TRAPFRAME, satp);
}

void ktrap(void) {
	int dev_nr = 0;
	unsigned long sepc = r_sepc();
	unsigned long sstatus = r_sstatus();
	unsigned long scause = r_scause();

	if (!(sstatus & SSTATUS_SPP))
		panic("ktrap: mode error");
	if (intr_get())
		panic("ktrap: intrs enabled");

	if (!(dev_nr = dev_intr())) {
		printk("scause %p\n", scause);
		printk("\tsepc=%p\tstval=%p", r_sepc(), r_stval());
		panic("ktrap");
	}

	if (dev_nr == 2 && myproc() && myproc()->state == RUNNING)	/* timer intr */
		yield();

	w_sepc(sepc);
	w_sstatus(sstatus);
}

void timer_intr(void) {
	acquire(&tickslock);
	ticks++;
	wakeup(&ticks);
	release(&tickslock);
}

int dev_intr(void) {
	unsigned long scause = r_scause();
	
	if ((scause & 0x8000000000000000l) && (scause & 0xff) == 9) {
		/* supervisor external intr via PLIC */
		int irq = plic_claim();
		if (irq == IRQ_UART0)
			uart_intr();
		else if (irq == IRQ_VIRTIO0)
			virtio_disk_intr();
		else if (irq)
			printk("unexpected intr irq=%d\n", irq);

		if (irq)
			plic_complete(irq);
		return -1;
	} else if (scause == 0x8000000000000000l) {
		if (r_mhartid() == 0)
			timer_intr();
		w_sip(r_sip() & ~2);
		return 2;
	} else {
		return 0;
	}
}
}
