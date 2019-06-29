/*
 * Unix file system format
 */

// We don't actually want to define off_t!
#define off_t xxx_off_t
#define bool xxx_bool
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#undef off_t
#undef bool

// Prevent inc/types.h, included from inc/ufs.h,
// from attempting to redefine types defined in the host's inttypes.h.
#define JOS_INC_TYPES_H
// Typedef the types that inc/mmu.h needs.
typedef uint32_t physaddr_t;
typedef uint32_t off_t;
typedef int bool;

#include <inc/mmu.h>
#include <inc/ufs.h>

#define ROUNDUP(n, v) ((n) - 1 + (v) - ((n) - 1) % (v))
#define MAX_DIR_ENTS 128

struct Dir
{
	struct Inode *f;
	struct DirEntry *ents;
	int n;
};

uint32_t nblocks, ninodes;
char *diskmap, *diskpos;

struct Super *super;
uint32_t *bitmap_b;
uint32_t *bitmap_i;
struct Inode *inodes, *inodepos;

void
panic(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	abort();
}

void
readn(int f, void *out, size_t n)
{
	size_t p = 0;
	while (p < n) {
		ssize_t m = read(f, out + p, n - p);
		if (m < 0)
			panic("read: %s", strerror(errno));
		if (m == 0)
			panic("read: Unexpected EOF");
		p += m;
	}
}

uint32_t
blockof(void *pos)
{
	return ((char*)pos - diskmap) / BLKSIZE;
}

void *
alloc_block(uint32_t bytes)
{
	void *start = diskpos;
	diskpos += ROUNDUP(bytes, BLKSIZE);
	if (blockof(diskpos) >= nblocks)
		panic("out of disk blocks");
	return start;
}

struct Inode *
alloc_inode(void) {
	struct Inode *ret = inodepos;
	if (++inodepos >= inodes + ninodes)
		panic("out of i-nodes");
	return ret;
}

void
opendisk(const char *name)
{
	int r, fileno, diskfd;
	int nbitblocks_b, nbitblocks_i;
	int nblocks_inode;
	struct Inode *f;

	if ((diskfd = open(name, O_RDWR | O_CREAT, 0666)) < 0)
		panic("open %s: %s", name, strerror(errno));

	if ((r = ftruncate(diskfd, 0)) < 0
	    || (r = ftruncate(diskfd, nblocks * BLKSIZE)) < 0)
		panic("truncate %s: %s", name, strerror(errno));

	if ((diskmap = mmap(NULL, nblocks * BLKSIZE, PROT_READ|PROT_WRITE,
			    MAP_SHARED, diskfd, 0)) == MAP_FAILED)
		panic("mmap %s: %s", name, strerror(errno));

	close(diskfd);

	diskpos = diskmap;
	alloc_block(BLKSIZE);
	super = alloc_block(BLKSIZE);
	super->s_magic = UFS_MAGIC;
	super->s_nblocks = nblocks;
	super->s_ninodes = ninodes;
	super->s_root.f_fileno = 0;
	strcpy(super->s_root.f_name, "/");

	nbitblocks_b = (nblocks + BLKBITSIZE - 1) / BLKBITSIZE;
	bitmap_b = alloc_block(nbitblocks_b * BLKSIZE);
	memset(bitmap_b, 0xFF, nbitblocks_b * BLKSIZE);

	nbitblocks_i = (ninodes + BLKBITSIZE - 1) / BLKBITSIZE;
	bitmap_i = alloc_block(nbitblocks_i * BLKSIZE);
	memset(bitmap_i, 0xFF, nbitblocks_i * BLKSIZE);

	nblocks_inode = (ninodes * sizeof(struct Inode) + BLKSIZE - 1) / BLKSIZE;
	inodepos = inodes = alloc_block(nblocks_inode * BLKSIZE);
	memset(inodes, 0, nblocks_inode * BLKSIZE);
	for (fileno = 0; fileno < ninodes; ++fileno)
		inodes[fileno].f_fileno = fileno;

	f = alloc_inode();
	f->f_type = FTYPE_DIR;
	f->f_refcnt = 1;
}

void
finishdisk(void)
{
	int r, i;

	for (i = 0; i < blockof(diskpos); ++i)
		bitmap_b[i/32] &= ~(1<<(i%32));
	for (i = 0; inodes + i < inodepos; ++i)
		bitmap_i[i/32] &= ~(1<<(i%32));

	if ((r = msync(diskmap, nblocks * BLKSIZE, MS_SYNC)) < 0)
		panic("msync: %s", strerror(errno));
}

void
finishfile(struct Inode *f, uint32_t start, uint32_t len)
{
	int i;
	f->f_size = len;
	len = ROUNDUP(len, BLKSIZE);
	for (i = 0; i < len / BLKSIZE && i < NDIRECT; ++i)
		f->f_direct[i] = start + i;
	if (i == NDIRECT) {
		uint32_t *ind = alloc_block(BLKSIZE);
		f->f_indirect = blockof(ind);
		for (; i < len / BLKSIZE; ++i)
			ind[i - NDIRECT] = start + i;
	}
}

void
startdir(struct Inode *f, struct Dir *dout)
{
	dout->f = f;
	dout->ents = malloc(MAX_DIR_ENTS * sizeof *dout->ents);
	dout->n = 0;
}

struct Inode *
diradd(struct Dir *d, uint32_t type, const char *name)
{
	struct DirEntry *entry = &d->ents[d->n++];
	struct Inode *f = alloc_inode();
	if (d->n > MAX_DIR_ENTS)
		panic("too many directory entries");
	strcpy(entry->f_name, name);
	entry->f_fileno = f->f_fileno;
	f->f_type = type;
	f->f_refcnt = 1;
	return f;
}

void
finishdir(struct Dir *d)
{
	int size = d->n * sizeof(struct DirEntry);
	if (size == 0)	// empty directory
		size = BLKSIZE;
	char *start = alloc_block(size);
	memmove(start, d->ents, size);
	finishfile(d->f, blockof(start), ROUNDUP(size, BLKSIZE));
	free(d->ents);
	d->ents = NULL;
}

void
writefile(struct Dir *dir, const char *name)
{
	int r, fd;
	struct Inode *f;
	struct stat st;
	const char *last;
	char *start;

	if ((fd = open(name, O_RDONLY)) < 0)
		panic("open %s: %s", name, strerror(errno));
	if ((r = fstat(fd, &st)) < 0)
		panic("stat %s: %s", name, strerror(errno));
	if (!S_ISREG(st.st_mode))
		panic("%s is not a regular file", name);
	if (st.st_size >= MAXFILESIZE)
		panic("%s too large", name);

	last = strrchr(name, '/');
	if (last)
		last++;
	else
		last = name;

	f = diradd(dir, FTYPE_REG, last);
	start = alloc_block(st.st_size);
	readn(fd, start, st.st_size);
	finishfile(f, blockof(start), st.st_size);
	close(fd);
}

void
usage(void)
{
	fprintf(stderr, "Usage: ufsformat ufs.img NBLOCKS NINODES files...\n");
	exit(2);
}

int
main(int argc, char **argv)
{
	int i; char *s;

	struct Inode *f_sub, *f_empty;
	struct Dir root, subdir, emptydir;

	assert(BLKSIZE % sizeof(struct Inode) == 0);
	assert(BLKSIZE % sizeof(struct DirEntry) == 0);

	if (argc < 4)
		usage();

	nblocks = strtol(argv[2], &s, 0);
	if (*s || s == argv[2] || nblocks < 2 || nblocks > 2048)
		usage();

	ninodes = strtol(argv[3], &s, 0);
	if (*s || s == argv[3] || ninodes < 1 || ninodes > 1024)
		usage();

	opendisk(argv[1]);

	startdir(&inodes[super->s_root.f_fileno], &root);
	for (i = 4; i < argc; i++)
		writefile(&root, argv[i]);

	f_empty = diradd(&root, FTYPE_DIR, "emptydir");
	startdir(f_empty, &emptydir);
	finishdir(&emptydir);

	f_sub   = diradd(&root, FTYPE_DIR, "subdir");
	startdir(f_sub, &subdir);
	for (i = 4; i < argc; i++)
		if (i % 8 == 0) writefile(&subdir, argv[i]);
	finishdir(&subdir);

	finishdir(&root);

	finishdisk();
	return 0;
}

