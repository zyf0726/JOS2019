// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "backtrace", "", mon_backtrace }
};

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(commands); i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{

	// Your code here.

    // ----------------- 
    //    arg1 ~ arg5
    //     ret addr    
    //    saved %ebp  <====== 
    //      ....            |
    // -----------------    |
    //    arg1 ~ arg5       |
    //     ret addr         |
    //    saved %ebp --------  <== current %ebp
    //      ....       
    // -----------------
    
    struct Eipdebuginfo dbg_info;
    
    typedef uint32_t *ptr_32;
    ptr_32 ebp      = (ptr_32) read_ebp();
    ptr_32 init_ebp = (ptr_32) 0x0;
    ptr_32 saved_ebp;
    
    int32_t ret_addr;
    int32_t arg1, arg2, arg3, arg4, arg5;

    cprintf("Stack backtrace:\n");
    for (; ebp != init_ebp; ebp = saved_ebp) {
        saved_ebp = (ptr_32) (*ebp);
        ret_addr  = ebp[1];
        arg1 = ebp[2];
        arg2 = ebp[3];
        arg3 = ebp[4];
        arg4 = ebp[5];
        arg5 = ebp[6];
        cga_set_bg(CGA_COLOR_BLUE);
        cga_set_fg(CGA_COLOR_WHITE);
        cprintf("  ebp %08x  eip %08x  args %08x %08x %08x %08x %08x\n",
                (uint32_t) ebp, ret_addr, arg1, arg2, arg3, arg4, arg5);
        cga_reset();
                
        debuginfo_eip(ret_addr, &dbg_info);
        cga_set_fg(CGA_COLOR_YELLOW);
        cprintf("        %s:%u: %.*s+%d\n",
                dbg_info.eip_file, dbg_info.eip_line,
                dbg_info.eip_fn_namelen, dbg_info.eip_fn_name,
                (int) (ret_addr - dbg_info.eip_fn_addr));
        cga_reset();
    }
	return 0;
}



/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");


	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
