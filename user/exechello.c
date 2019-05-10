#include <inc/lib.h>


void
umain(int argc, char **argv)
{
	envid_t envid; int r;
	cprintf("i am parent environment %08x\n", thisenv->env_id);
	if ((envid = fork()) < 0)
		panic("fork() failed: %e", envid);
	if (envid == 0) {
		if ((r = execl("hello", "hello", 0)) < 0)
			panic("exec(hello) failed: %e", r);
	}
}
