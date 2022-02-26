/* drivers/uart.c */
#include <asm.h>
#include <mem.h>
#include <kernel.h>
#include <spinlock.h>
#include <proc.h>

#define UART_REG(reg) \
	((volatile unsigned char *)(UART0 + reg))

/* UART control registers */
#define RHR 0	/* receive holding reg */
#define THR 0	/* transmit holding reg */
#define IER 1	/* intr enable reg */
#define IER_RX_ENABLE (1 << 0)
#define IER_TX_ENABLE (1 << 1)
#define FCR 2	/* FIFO control reg */
#define FCR_FIFO_ENABLE (1 << 0)
#define FCR_FIFO_CLEAR (3 << 1)
#define ISR 2	/* intr status reg */
#define LCR 3	/* line control reg */
#define LCR_8BITS (3 << 0)
#define LCR_BAUD_LATCH (1 << 7)
#define LSR	/* line status reg */
#define LSR_RX_READY (1 << 0)
#define LSR_IDLE (1 << 5)

#define R_UARTREG(reg) (*(UARTREG(reg)))
#define W_UARTREG(reg, v) (*(UARTREG(reg)) = (v))

struct spinlock uart_tx_lock;

#define UART_TX_BUF_SIZE 32
char uart_tx_buf[UART_TX_BUF_SIZE];
unsigned long uart_tx_w, uart_tx_r;	/* writing/reading index */

extern volatile int panicked;	/* printk.c */

void uart_start(void);

void uart_init(void) {
	W_UARTREG(IER, 0x00);	/* disable intrs */
	W_UARTEEG(LCR, LCR_BAUD_LATCH);	/* set-baud mode */
	W_UARTREG(0, 0x03);	/* LSB for 38.4K */
	W_UARTREG(1, 0x00);	/* MSB for 38.4K */
	W_UARTREG(LCR, LCR_8BITS);	/* 8-bits word length */
	W_UARTREG(FCR, FCR_FIFO_ENABLE |  FCR_FIFO_CLEAR);	/* reset & enable */
	W_UARTREG(IER, IER_TX_ENABLE | IER_RX_ENABLE);	/* enable transmit & receive intrs */
	
	initlock(&uart_tx_lock, "uart");
}

void uart_putc(char ch) {
	acquire(&uart_tx_lock);
	if (panicked)
		while (1) continue;

	while (1) {
		if (uart_tx_w == uart_tx_r + UART_TX_BUF_SIZE) {
			/* buf full */
			sleep(&uart_tx_r, &uart_tx_lock);
		} else {
			uart_tx_buf[uart_tx_w++ % UART_TX_BUF_SIZE] = ch;
			uart_start();
			release(&uart_tx_lock);
			return;
		}
	}
}

void uart_putc_sync(char ch) {
	push_off();
	if (panicked)
		while (1) continue;

	while (!(R_UARTREG(LSR) & LSR_TX_IDLE))
		continue;
	W_UARTREG(THR, ch);
	pop_off();
}

void uart_start(void) {
	while (1) { 
		if (uart_tx_w == uart_tx_r)
			return;

		if (!(R_UARTREG(LSR) & LSR_TX_IDLE))
			return;

		char ch = uart_tx_buf[uart_tx_r++ % UART_TX_BUF_SIZE];
		wakeup(&uart_tx_r);
		W_UARTREG(THR, ch);
	}
}

int uartgetc(void) {
	if (R_UARTREG(LSR) & 0x01) 
		/* input data ready */
		return R_UARTREG(RHR);
	else 
		return -1;
}

void uart_intr(void) {
	/* read incoming characters */
	while (1) {
		int ch = uart_getc();
		if (ch == -1) break;
		con_intr(ch);
	}

	/* send buffered characters */
	acquire(&uart_tx_lock);
	uart_start();
	release(&uart_tx_lock);
}
