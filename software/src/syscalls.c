/*
 * Minimal newlib syscall stubs for bare-metal RISC-V.
 * Output goes to the DW APB UART at 0xC0009000, which is connected
 * to the UART_PHY built-in terminal in Virtualizer Studio.
 */

#include <sys/stat.h>
#include <errno.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UART_BASE     0xC000A000UL
#define UART_THR      (*(volatile uint32_t *)(UART_BASE + 0x00))
#define UART_DLL      (*(volatile uint32_t *)(UART_BASE + 0x00))
#define UART_DLH      (*(volatile uint32_t *)(UART_BASE + 0x04))
#define UART_IER      (*(volatile uint32_t *)(UART_BASE + 0x04))
#define UART_FCR      (*(volatile uint32_t *)(UART_BASE + 0x08))
#define UART_LCR      (*(volatile uint32_t *)(UART_BASE + 0x0C))
#define UART_LSR      (*(volatile uint32_t *)(UART_BASE + 0x14))
#define UART_LSR_THRE (1 << 5)

static int uart_inited = 0;

static void uart_init(void) {
    UART_IER = 0x00;        /* disable interrupts */
    UART_LCR = 0x80;        /* DLAB = 1 */
    UART_DLL = 0x01;        /* divisor latch low  (baud = clk/16/1) */
    UART_DLH = 0x00;        /* divisor latch high */
    UART_LCR = 0x03;        /* 8N1, DLAB = 0 */
    UART_FCR = 0x07;        /* enable + reset FIFOs */
    uart_inited = 1;
}

static void uart_putc(char c) {
    if (!uart_inited)
        uart_init();
    UART_THR = (uint32_t)(unsigned char)c;
}

extern char _end[];
static char *heap_ptr = 0;

void *_sbrk(ptrdiff_t incr) {
    if (heap_ptr == 0)
        heap_ptr = _end;
    char *prev = heap_ptr;
    heap_ptr += incr;
    return prev;
}

int _write(int fd, const char *buf, int len) {
    (void)fd;
    for (int i = 0; i < len; i++) {
        if (buf[i] == '\n')
            uart_putc('\r');
        uart_putc(buf[i]);
    }
    return len;
}

int _read(int fd, char *buf, int len) {
    (void)fd; (void)buf; (void)len;
    return 0;
}

int _close(int fd) {
    (void)fd;
    return -1;
}

int _fstat(int fd, struct stat *st) {
    (void)fd;
    st->st_mode = S_IFCHR;
    return 0;
}

int _isatty(int fd) {
    (void)fd;
    return 1;
}

off_t _lseek(int fd, off_t offset, int whence) {
    (void)fd; (void)offset; (void)whence;
    return 0;
}

void _exit(int status) {
    (void)status;
    while (1)
        asm volatile("wfi");
}

#ifdef __cplusplus
}
#endif
