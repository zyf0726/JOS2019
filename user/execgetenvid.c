#include <inc/lib.h>


void
umain(int argc, char **argv)
{
	envid_t envid; int r;
	if (argc == 0) {
		cprintf(">> i am the initial environment %08x\n", sys_getenvid());
		if ((r = execl("execgetenvid", "5", 0)) < 0)
			panic(">> execl(execgetenvid) failed: %e", r);
	}
	else {
		cprintf(">> i am the environment %08x\n", sys_getenvid(), argv[0][0]);
		if (argv[0][0] > '1') {
			--argv[0][0];
			if ((r = exec("execgetenvid", (const char **) argv)) < 0)
				panic("exec(execgetenvid) failed: %e", r);
		}
	}
	cprintf(">> free %08x\n", sys_getenvid());
}
