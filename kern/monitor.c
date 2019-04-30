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
#include <kern/trap.h>

#include <inc/mmu.h>
#include <kern/pmap.h>

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
	{ "backtrace", "Display a backtrace of the stack", mon_backtrace },
    { "showmap", "Display all physical page mappings that apply to a particular range of virtual addresses", mon_showmappings },
    { "setperm", "Explicitly change the permissions of the mappings", mon_setpermbits },
    { "dumpmem-v", "Dump the contents of a range of virtual memory", mon_dumpmemory_v },
    { "dumpmem-p", "Dump the contents of a range of physical memroy", mon_dumpmemory_p }
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
        cprintf("  ebp %08x  eip %08x  args %08x %08x %08x %08x %08x\n",
                (uint32_t) ebp, ret_addr, arg1, arg2, arg3, arg4, arg5);
                
        debuginfo_eip(ret_addr, &dbg_info);
        cprintf("        %s:%u: %.*s+%d\n",
                dbg_info.eip_file, dbg_info.eip_line,
                dbg_info.eip_fn_namelen, dbg_info.eip_fn_name,
                (int) (ret_addr - dbg_info.eip_fn_addr));
    }
	return 0;
}

static void displayln_pte(pte_t pte) {
    int pb_P = pte & PTE_P; // present / invalid
    int pb_W = pte & PTE_W; // read-only / writable
    int pb_U = pte & PTE_U; // user / supervisor
    physaddr_t pa = PTE_ADDR(pte);
    if (!pb_P)
        cprintf("--------   --/--\n");
    else {
        cprintf("%08x   R", pa);
        cprintf(pb_W ? "W/" : "-/");
        if (pb_U)
            cprintf(pb_W ? "RW\n" : "R-\n");
        else
            cprintf("--\n");
    }    
}

int mon_showmappings(int argc, char **argv, struct Trapframe *tf) {
    if (argc < 2) {
        cprintf("Argument Error(2): a range of virtual addresses required.\n");
        return 0;
    }
    char *endptr_1, *endptr_2 = NULL;
    uintptr_t start_va = strtol(argv[1], &endptr_1, 16);
    uintptr_t end_va   = (argc > 2) ? strtol(argv[2], &endptr_2, 16)
                                    : (start_va + PGSIZE);
    if (*endptr_1 || (endptr_2 != NULL && *endptr_2)) {
        cprintf("Format Error: invalid address\n");
        return 0;
    }
    start_va = ROUNDDOWN(start_va, PGSIZE);
    end_va   = ROUNDUP  (end_va  , PGSIZE);
    for (; start_va < end_va; start_va += PGSIZE) {
        pte_t *pte = pgdir_walk(kern_pgdir, (void *) start_va, 0);
        cprintf("%08x  ===>  ", start_va);
        if (pte != NULL)
            displayln_pte(*pte);
        else displayln_pte(0);
    }
    return 0;
}

int mon_setpermbits(int argc, char **argv, struct Trapframe *tf) {
    if (argc < 4) {
        cprintf("Argument Error(3): a virtual address and two permission bits required.\n");
        cprintf("Format: [VA] + [U/S/-] + [RO/W/-]\n");
        cprintf("\t[VA]      virtual addresss\n");
        cprintf("\t[U/S/-]   user / supervisor / unchanged\n");
        cprintf("\t[RO/W/-]  read-only / writable / unchanged\n");
        return 0;
    }
    char *endptr;
    uintptr_t va = ROUNDDOWN(strtol(argv[1], &endptr, 16), PGSIZE);
    if (*endptr) {
        cprintf("Format Error: invalid virtual address\n");
        return 0;
    }
    if (strcmp(argv[2], "U") && strcmp(argv[2], "S")
            && strcmp(argv[2], "-")) {
        cprintf("Format Error: invalid permission bit [U/S/-]\n");
        return 0;
    }
    if (strcmp(argv[3], "RO") && strcmp(argv[3], "W")
            && strcmp(argv[3], "-")) {
        cprintf("Format Error: invalid permission bit [RO/W/-]\n");
        return 0;
    }
    pte_t *pte = pgdir_walk(kern_pgdir, (void *) va, 0);
    int pb_U = (strcmp(argv[2], "-") == 0) ? (*pte & PTE_U) :
               (strcmp(argv[2], "U") == 0) ? PTE_U :
               0;
    int pb_W = (strcmp(argv[3], "-") == 0) ? (*pte & PTE_W) :
               (strcmp(argv[3], "W") == 0) ? PTE_W :
               0;
    if (pte == NULL) {
        cprintf("no mapping at VA %s\n", argv[1]);
        return 0;
    }
    *pte = PTE_ADDR(*pte) | (pb_U | pb_W | PTE_P);
    cprintf("%08x  ===>  ", va);
    displayln_pte(*pte);
    return 0;
}

static const int TYPE_PADDR = 0;
static const int TYPE_VADDR = 1;
static void mon_dumpmemory(void *start_va, void *end_va, int addr_type) {
    while (start_va < end_va) {
        if (addr_type == TYPE_PADDR)
            cprintf("%x:", PADDR(start_va));
        else
            cprintf("%x:", (uintptr_t) start_va);
        for (int i = 0; i < 4; ++i, ++start_va) {
            if (start_va == end_va)
                break;
            cprintf("    %02x", *(uint8_t *) start_va);
        }
        cprintf("\n");
    }
}

int mon_dumpmemory_v(int argc, char **argv, struct Trapframe *tf) {
    if (argc < 3) {
        cprintf("Argument Error(2): a range of virtual addresses required.\n");
        return 0;
    }
    char *endptr_1, *endptr_2;
    uintptr_t start_va = strtol(argv[1], &endptr_1, 16);
    uintptr_t end_va   = strtol(argv[2], &endptr_2, 16);
    if (*endptr_1 || *endptr_2) {
        cprintf("Format Error: invalid virtual address\n");
        return 0;
    }
    mon_dumpmemory((void *) start_va, (void *) end_va, TYPE_VADDR);
    return 0;
}

int mon_dumpmemory_p(int argc, char **argv, struct Trapframe *tf) {
    if (argc < 3) {
        cprintf("Argument Error(2): a range of physical addresses required.\n");
        return 0;
    }
    char *endptr_1, *endptr_2;
    physaddr_t start_pa = strtol(argv[1], &endptr_1, 16);
    physaddr_t end_pa   = strtol(argv[2], &endptr_2, 16);
    if (*endptr_1 || *endptr_2) {
        cprintf("Format Error: invalid physical address\n");
        return 0;
    }
    if (end_pa >= 0x10000000) {
        cprintf("Memory Error: cannot access physical address >= 256MB\n");
        return 0;
    }
    mon_dumpmemory(KADDR(start_pa), KADDR(end_pa), TYPE_PADDR);
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

	if (tf != NULL)
		print_trapframe(tf);

	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
