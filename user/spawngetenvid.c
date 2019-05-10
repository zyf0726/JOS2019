#include <inc/lib.h>


void
umain(int argc, char **argv)
{
	envid_t envid; int r;
	if (argc == 0) {
		cprintf(">> i am the initial environment %08x\n", sys_getenvid());
		if ((r = spawnl("spawngetenvid", "5", 0)) < 0)
			panic("spawnl(spawngetenvid) failed: %e", r);
	}
	else {
		cprintf(">> i am the environment %08x\n", sys_getenvid(), argv[0][0]);
		if (argv[0][0] > '1') {
			--argv[0][0];
			if ((r = spawn("spawngetenvid", (const char **) argv)) < 0)
				panic("spawn(spawngetenvid) failed: %e", r);
		}
	}
	cprintf(">> free %08x\n", sys_getenvid());
}
