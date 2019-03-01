/* See COPYRIGHT for copyright information. */

#ifndef _CONSOLE_H_
#define _CONSOLE_H_
#ifndef JOS_KERNEL
# error "This is a JOS kernel header; user programs should not #include it"
#endif

#include <inc/types.h>

#define MONO_BASE	0x3B4
#define MONO_BUF	0xB0000
#define CGA_BASE	0x3D4
#define CGA_BUF		0xB8000

#define CRT_ROWS	25
#define CRT_COLS	80
#define CRT_SIZE	(CRT_ROWS * CRT_COLS)

// Support colored text output
#define CGA_COLOR_BLACK    0x0
#define CGA_COLOR_BLUE     0x1
#define CGA_COLOR_GREEN    0x2
#define CGA_COLOR_CYAN     0x3
#define CGA_COLOR_RED      0x4
#define CGA_COLOR_MAGENTA  0x5
#define CGA_COLOR_BROWN    0x6
#define CGA_COLOR_GRAY     0x7
#define CGA_COLOR_DARKGRAY        0x8
#define CGA_COLOR_BRIGHTBLUE      0x9
#define CGA_COLOR_BRIGHTGREEN     0xa
#define CGA_COLOR_BRIGHTCYAN      0xb
#define CGA_COLOR_BIRGHTRED       0xc
#define CGA_COLOR_BRIGHTMAGENTA   0xd
#define CGA_COLOR_YELLOW  0xe
#define CGA_COLOR_WHITE   0xf
void cga_set_bg(uint8_t);
void cga_set_fg(uint8_t);
void cga_reset(void);

void cons_init(void);
int cons_getc(void);

void kbd_intr(void); // irq 1
void serial_intr(void); // irq 4

#endif /* _CONSOLE_H_ */
