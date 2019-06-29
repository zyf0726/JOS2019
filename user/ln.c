#include <inc/lib.h>

void
usage(void)
{
	printf("usage: ln TARGET LINK_NAME\n");
	exit();
}

void
umain(int argc, char **argv)
{
	int i, r;

	binaryname = "ln";
	if (argc != 3)
		usage();
	else {
		r = link(argv[1], argv[2]);
		if (r < 0)
			printf("link error: %e\n", r);
	}
}
