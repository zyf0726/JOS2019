#include <inc/lib.h>

void
usage(void)
{
	printf("usage: rm [file...]\n");
	exit();
}

void
umain(int argc, char **argv)
{
	int i, r;

	binaryname = "rm";
	if (argc < 2)
		usage();
	else
		for (i = 1; i < argc; i++) {
			r = remove(argv[i]);
			if (r < 0)
				printf("can't remove %s: %e\n", argv[i], r);
		}
}
