#include <inc/lib.h>

#define BUFSIZE 100
char buf[BUFSIZE + 10];

void
umain(int argc, char **argv)
{
	envid_t envid; int r, fd;
	if (argc == 0) {
		if ((fd = open("motd", O_RDONLY)) < 0)
			panic("open failed: %e", fd);
		if (fd != 0 && ((r = dup(0, fd)) < 0))
			panic("dup failed: %e", r);
		if ((r = execl("execfdsharing", "-", 0)) < 0)
			panic("execl failed: %e", r);
	}
	else {
		if ((r = read(0, buf, BUFSIZE)) < 0)
			panic("read failed: %e", r);
		buf[r] = '\0';
		cprintf("========================\n");
		cprintf("%s\n", buf);
		cprintf("========================\n");
	}
}
