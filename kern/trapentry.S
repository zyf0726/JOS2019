/* See COPYRIGHT for copyright information. */

#include <inc/mmu.h>
#include <inc/memlayout.h>
#include <inc/trap.h>

#include <kern/picirq.h>


###################################################################
# exceptions/interrupts
###################################################################

/* TRAPHANDLER defines a globally-visible function for handling a trap.
 * It pushes a trap number onto the stack, then jumps to _alltraps.
 * Use TRAPHANDLER for traps where the CPU automatically pushes an error code.
 *
 * You shouldn't call a TRAPHANDLER function from C, but you may
 * need to _declare_ one in C (for instance, to get a function pointer
 * during IDT setup).  You can declare the function with
 *   void NAME();
 * where NAME is the argument passed to TRAPHANDLER.
 */
#define TRAPHANDLER(name, num)						\
	.globl name;		/* define global symbol for 'name' */	\
	.type name, @function;	/* symbol type is function */		\
	.align 2;		/* align function definition */		\
	name:			/* function starts here */		\
	pushl $(num);							\
	jmp _alltraps

/* Use TRAPHANDLER_NOEC for traps where the CPU doesn't push an error code.
 * It pushes a 0 in place of the error code, so the trap frame has the same
 * format in either case.
 */
#define TRAPHANDLER_NOEC(name, num)					\
	.globl name;							\
	.type name, @function;						\
	.align 2;							\
	name:								\
	pushl $0;							\
	pushl $(num);							\
	jmp _alltraps

.text

/*
 * Lab 3: Your code here for generating entry points for the different traps.
 */
TRAPHANDLER_NOEC(handler_divide, T_DIVIDE)
TRAPHANDLER_NOEC(handler_debug,  T_DEBUG)
TRAPHANDLER_NOEC(handler_nmi,    T_NMI)
TRAPHANDLER_NOEC(handler_brkpt,  T_BRKPT)
TRAPHANDLER_NOEC(handler_oflow,  T_OFLOW)
TRAPHANDLER_NOEC(handler_bound,  T_BOUND)
TRAPHANDLER_NOEC(handler_illop,  T_ILLOP)
TRAPHANDLER_NOEC(handler_device, T_DEVICE)
TRAPHANDLER     (handler_dblflt, T_DBLFLT)
/* reserved	trap number 9 */
TRAPHANDLER     (handler_tss,   T_TSS)
TRAPHANDLER     (handler_segnp, T_SEGNP)
TRAPHANDLER     (handler_stack, T_STACK)
TRAPHANDLER     (handler_gpflt, T_GPFLT)
TRAPHANDLER     (handler_pgflt, T_PGFLT)
/* reserved	trap number 15 */
TRAPHANDLER_NOEC(handler_fperr, T_FPERR)
TRAPHANDLER     (handler_align, T_ALIGN)
TRAPHANDLER_NOEC(handler_mchk,  T_MCHK)
TRAPHANDLER_NOEC(handler_simderr, T_SIMDERR)

TRAPHANDLER_NOEC(handler_irq_0,  IRQ_OFFSET + 0)
TRAPHANDLER_NOEC(handler_irq_1,  IRQ_OFFSET + 1)
TRAPHANDLER_NOEC(handler_irq_2,  IRQ_OFFSET + 2)
TRAPHANDLER_NOEC(handler_irq_3,  IRQ_OFFSET + 3)
TRAPHANDLER_NOEC(handler_irq_4,  IRQ_OFFSET + 4)
TRAPHANDLER_NOEC(handler_irq_5,  IRQ_OFFSET + 5)
TRAPHANDLER_NOEC(handler_irq_6,  IRQ_OFFSET + 6)
TRAPHANDLER_NOEC(handler_irq_7,  IRQ_OFFSET + 7)
TRAPHANDLER_NOEC(handler_irq_8,  IRQ_OFFSET + 8)
TRAPHANDLER_NOEC(handler_irq_9,  IRQ_OFFSET + 9)
TRAPHANDLER_NOEC(handler_irq_10, IRQ_OFFSET + 10)
TRAPHANDLER_NOEC(handler_irq_11, IRQ_OFFSET + 11)
TRAPHANDLER_NOEC(handler_irq_12, IRQ_OFFSET + 12)
TRAPHANDLER_NOEC(handler_irq_13, IRQ_OFFSET + 13)
TRAPHANDLER_NOEC(handler_irq_14, IRQ_OFFSET + 14)
TRAPHANDLER_NOEC(handler_irq_15, IRQ_OFFSET + 15)

TRAPHANDLER_NOEC(handler_syscall, T_SYSCALL)

/*
 * Lab 3: Your code here for _alltraps
 */
_alltraps:
	/* 'tf_trapno' has been pushed by TRAPHANDLER */
	pushw	$0x0	/* tf_padding2 */
	pushw	%ds	 	/* tf_ds */
	pushw	$0x0	/* tf_padding1 */
	pushw	%es		/* tf_es */
	pushal			/* tf_regs */
	movl	$(GD_KD), %eax
	movw	%ax, %ds
	movw	%ax, %es
	pushl	%esp
	call	trap

.data
########################################
# trap handler table
########################################
	.p2align	4
	.globl		hdlr_tab
hdlr_tab:
	.long	handler_divide
	.long	handler_debug
	.long	handler_nmi
	.long 	handler_brkpt
	.long	handler_oflow
	.long	handler_bound
	.long	handler_illop
	.long	handler_device
	.long	handler_dblflt
	.long	0
	.long	handler_tss
	.long	handler_segnp
	.long	handler_stack
	.long	handler_gpflt
	.long	handler_pgflt
	.long	0
	.long	handler_fperr
	.long	handler_align
	.long	handler_mchk
	.long	handler_simderr
	.space	(IRQ_OFFSET-20) << 2
	.long	handler_irq_0
	.long	handler_irq_1
	.long	handler_irq_2
	.long	handler_irq_3
	.long	handler_irq_4
	.long	handler_irq_5
	.long	handler_irq_6
	.long	handler_irq_7
	.long	handler_irq_8
	.long	handler_irq_9
	.long	handler_irq_10
	.long	handler_irq_11
	.long	handler_irq_12
	.long	handler_irq_13
	.long	handler_irq_14
	.long	handler_irq_15
	.long	handler_syscall
