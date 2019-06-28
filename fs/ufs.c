#include <inc/string.h>
#include <inc/partition.h>

#include "ufs.h"

// --------------------------------------------------------------
// Super block
// --------------------------------------------------------------

// Validate the file system super-block.
void
check_super(void)
{
	if (super->s_magic != UFS_MAGIC)
		panic("bad unix file system magic number");

	if (super->s_nblocks > DISKSIZE/BLKSIZE)
		panic("file system is too large");

	cprintf("superblock is good\n");
}

// --------------------------------------------------------------
// Free block bitmap
// --------------------------------------------------------------

// Check to see if the block bitmap indicates that block 'blockno' is free.
// Return 1 if the block is free, 0 if not.
bool
block_is_free(uint32_t blockno)
{
	if (super == 0 || blockno >= super->s_nblocks)
		return 0;
	if (bitmap_b[blockno / 32] & (1 << (blockno % 32)))
		return 1;
	return 0;
}

// Mark a block free in the bitmap
void
free_block(uint32_t blockno)
{
	// Blockno zero is the null pointer of block numbers.
	if (blockno == 0)
		panic("attempt to free zero block");
	bitmap_b[blockno/32] |= 1<<(blockno%32);
}

// Search the bitmap for a free block and allocate it.  When you
// allocate a block, immediately flush the changed bitmap block
// to disk.
//
// Return block number allocated on success,
// -E_NO_DISK if we are out of blocks.
int
alloc_block(void)
{
	// The bitmap consists of one or more blocks.  A single bitmap block
	// contains the in-use bits for BLKBITSIZE blocks.  There are
	// super->s_nblocks blocks in the disk altogether.

	for (uint32_t blockno = 0; blockno < super->s_nblocks; ++blockno)
		if (block_is_free(blockno)) {
			bitmap_b[blockno/32] &= ~(1<<(blockno%32));
			flush_block(&bitmap_b[blockno/32]);
			return blockno;
		}
	return -E_NO_DISK;
}

// Validate the file system block bitmap.
//
// Check that all reserved blocks -- 0, 1, and the bitmap blocks themselves --
// are all marked as in-use.
void
check_block_bitmap(void)
{
	uint32_t i, j;

	// Make sure all bitmap blocks, including both block bitmap and
	// i-node bitmap, are marked in-use
	for (i = 0; i * BLKBITSIZE < super->s_nblocks; i++)
		assert(!block_is_free(2+i));
	for (j = 0; j * BLKBITSIZE < super->s_ninodes; i++, j++)
		assert(!block_is_free(2+i));

	// Make sure the reserved and root blocks are marked in-use.
	assert(!block_is_free(0));
	assert(!block_is_free(1));

	cprintf("block bitmap is good\n");
}

// --------------------------------------------------------------
// Free i-node bitmap
// --------------------------------------------------------------

// Check to see if the i-node bitmap indicates that the i-node of
// file 'fileno' is free.
// Return 1 if the i-node is free, 0 if not.
bool
inode_is_free(uint32_t fileno)
{
	if (super == 0 || fileno >= super->s_ninodes)
		return 0;
	if (bitmap_i[fileno / 32] & (1 << (fileno % 32)))
		return 1;
	return 0;
}

// Mark an i-node free in the bitmap (if its reference count is 0)
void
free_inode(uint32_t fileno)
{
	int refcnt = inodes[fileno].f_refcnt;
	if (refcnt != 0)
		panic("attempt to free an i-node with non-zero refcnt %d", refcnt);

	// Fileno zero is the root directory
	if (fileno == 0)
		panic("attempt to free root directory");

	bitmap_i[fileno/32] |= 1<<(fileno%32);
}

// Decrement the reference count of an i-node,
// freeing it if there are no more refs.
void
decref_inode(uint32_t fileno)
{
	int refcnt = inodes[fileno].f_refcnt;
	if (refcnt <= 0)
		panic("attempt to decref an i-node with non-positive refcnt %d", refcnt);

	if (--inodes[fileno].f_refcnt == 0)
		free_inode(fileno);
}

// Search the bitmap for a free i-node and allocate it. When you
// allocate an i-node, immediately flush the changed bitmap block
// to disk.
//
// Return file number allocated on success,
// -E_NO_DISK if we are out of blocks.
int
alloc_inode(void)
{
	// The bitmap consists of one or more blocks.  A single bitmap block
	// contains the in-use bits for BLKBITSIZE i-nodes.  There are
	// super->s_ninodes i-nodes in the disk altogether.

	for (uint32_t fileno = 0; fileno < super->s_ninodes; ++fileno)
		if (inode_is_free(fileno)) {
			bitmap_i[fileno/32] &= ~(1<<(fileno%32));
			flush_block(&bitmap_i[fileno/32]);
			return fileno;
		}
	return -E_NO_DISK;
}

// Validate the file system i-node bitmap.
//
// Check that the reserved i-node 0 for root directory is marked
// as in-use.
void
check_inode_bitmap(void)
{
	assert(!inode_is_free(0));
	cprintf("i-node bitmap is good\n");
}

// Validate the file system i-nodes.
//
// Check that each i-node has the correct 'fileno' that equals to
// its index in array 'inodes'
void
check_inodes(void)
{
	for (int fileno = 0; fileno < super->s_ninodes; ++fileno)
		assert(inodes[fileno].f_fileno == fileno);

	cprintf("i-nodes are good\n");
}


// --------------------------------------------------------------
// File system structures
// --------------------------------------------------------------


// Initialize the file system
void
ufs_init(void)
{
	int nbitblocks_b, nbitblocks_i;

	static_assert(sizeof(struct DirEntry) == 128);
	static_assert(sizeof(struct Inode) == 64);

	// Find a JOS disk.  Use the second IDE disk (number 1) if available
	if (ide_probe_disk1())
		ide_set_disk(1);
	else
		ide_set_disk(0);
	bc_init();

	// Set "super" to point to the super block.
	super = diskaddr(1);
	check_super();

	// Set "bitmap_b" to the beginning of the first bitmap block.
	bitmap_b = diskaddr(2);
	check_block_bitmap();

	// Set "bitmap_i" to the first block after the end of "bitmap_b"
	nbitblocks_b = (super->s_nblocks + BLKBITSIZE - 1) / BLKBITSIZE;
	bitmap_i = diskaddr(2 + nbitblocks_b);
	check_inode_bitmap();

	// Set "inodes" to the first block after the end of "bitmap_i"
	nbitblocks_i = (super->s_ninodes + BLKBITSIZE - 1) / BLKBITSIZE;
	inodes = diskaddr(2 + nbitblocks_b + nbitblocks_i);
	check_inodes();

}


// Find the disk block number slot for the 'filebno'th block in file 'f'.
// Set '*ppdiskbno' to point to that slot.
// The slot will be one of the f->f_direct[] entries,
// or an entry in the indirect block.
// When 'alloc' is set, this function will allocate an indirect block
// if necessary.
//
// Returns:
//	0 on success (but note that *ppdiskbno might equal 0).
//	-E_NOT_FOUND if the function needed to allocate an indirect block, but
//		alloc was 0.
//	-E_NO_DISK if there's no space on the disk for an indirect block.
//	-E_INVAL if filebno is out of range (it's >= NDIRECT + NINDIRECT).
//
static int
file_block_walk(struct Inode *f, uint32_t filebno, uint32_t **ppdiskbno, bool alloc)
{
	if (filebno >= NDIRECT + NINDIRECT)
		return -E_INVAL;
	if (filebno < NDIRECT) {
		*ppdiskbno = &f->f_direct[filebno];
		return 0;
	}

	int r; uint32_t *ind;
	if (!f->f_indirect) {
		if (!alloc)
			return -E_NOT_FOUND;
		if ((r = alloc_block()) < 0)
			return r;
		memset(diskaddr(r), 0, BLKSIZE);
		f->f_indirect = r;
	}
	ind = (uint32_t *) diskaddr(f->f_indirect);
	*ppdiskbno = &ind[filebno - NDIRECT];
	return 0;
}

// Set *blk to the address in memory where the 'filebno'th
// block of file 'f' would be mapped.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_NO_DISK if a block needed to be allocated but the disk is full.
//	-E_INVAL if filebno is out of range.
int
file_get_block(struct Inode *f, uint32_t filebno, char **blk)
{
	int r; uintptr_t *pdiskbno;
	if ((r = file_block_walk(f, filebno, &pdiskbno, 1)) < 0)
		return r;
	if (*pdiskbno == 0) {
		if ((r = alloc_block()) < 0)
			return r;
		*pdiskbno = r;
	}
	*blk = (char *) diskaddr(*pdiskbno);
	return 0;
}

// Try to find a file named "name" in dir.
// If so, set *entry to its directory entry.
//
// Returns 0 and sets *file on success, < 0 on error.  Errors are:
//	-E_NOT_FOUND if the file is not found
static int
dir_lookup(struct Inode *dir, const char *name, struct DirEntry **entry)
{
	int r;
	uint32_t i, j, nblock;
	char *blk;
	struct DirEntry *f;

	// Search dir for name.
	// We maintain the invariant that the size of a directory-file
	// is always a multiple of the file system's block size.
	assert((dir->f_size % BLKSIZE) == 0);
	nblock = dir->f_size / BLKSIZE;
	for (i = 0; i < nblock; i++) {
		if ((r = file_get_block(dir, i, &blk)) < 0)
			return r;
		f = (struct DirEntry*) blk;
		for (j = 0; j < BLKDIRENTS; j++)
			if (strcmp(f[j].f_name, name) == 0) {
				*entry = &f[j];
				return 0;
			}
	}
	return -E_NOT_FOUND;
}

// Set *entry to point at a free DirEntry structure in dir.
// The caller is responsible for filling in the DirEntry fields.
static int
dir_alloc_entry(struct Inode *dir, struct DirEntry **entry)
{
	int r;
	uint32_t nblock, i, j;
	char *blk;
	struct DirEntry *f;

	assert((dir->f_size % BLKSIZE) == 0);
	nblock = dir->f_size / BLKSIZE;
	for (i = 0; i < nblock; i++) {
		if ((r = file_get_block(dir, i, &blk)) < 0)
			return r;
		f = (struct DirEntry*) blk;
		for (j = 0; j < BLKDIRENTS; j++)
			if (f[j].f_name[0] == '\0') {
				*entry = &f[j];
				return 0;
			}
	}
	dir->f_size += BLKSIZE;
	if ((r = file_get_block(dir, i, &blk)) < 0)
		return r;
	f = (struct DirEntry*) blk;
	*entry = &f[0];
	return 0;
}

// Skip over slashes.
static const char*
skip_slash(const char *p)
{
	while (*p == '/')
		p++;
	return p;
}

// Evaluate a path name, starting at the root.
// On success, set *pentry to the directory entry of the
// file we found, and set *pdir to the directory the file is in.
// If we cannot find the file but find the directory
// it should be in, set *pdir and copy the final path
// element into lastelem.
static int
walk_path(const char *path, struct Inode **pdir, struct DirEntry **pentry, char *lastelem)
{
	const char *p;
	char name[MAXNAMELEN];
	struct Inode *dir;
	struct DirEntry *entry;
	int r;

	// if (*path != '/')
	//	return -E_BAD_PATH;
	path = skip_slash(path);
	entry = &super->s_root;
	dir = 0;
	name[0] = 0;

	if (pdir)
		*pdir = 0;
	*pentry = 0;
	while (*path != '\0') {
		dir = &inodes[entry->f_fileno];
		p = path;
		while (*path != '/' && *path != '\0')
			path++;
		if (path - p >= MAXNAMELEN)
			return -E_BAD_PATH;
		memmove(name, p, path - p);
		name[path - p] = '\0';
		path = skip_slash(path);

		if (dir->f_type != FTYPE_DIR)
			return -E_NOT_FOUND;
		if ((r = dir_lookup(dir, name, &entry)) < 0) {
			if (r == -E_NOT_FOUND && *path == '\0') {
				if (pdir)
					*pdir = dir;
				if (lastelem)
					strcpy(lastelem, name);
				*pentry = 0;
			}
			return r;
		}
	}

	if (pdir)
		*pdir = dir;
	*pentry = entry;
	return 0;
}

// --------------------------------------------------------------
// File operations
// --------------------------------------------------------------

// Create "path".  On success set *pentry to point at the directory
// entry of this file and return 0.
// On error return < 0.
int
file_create(const char *path, struct DirEntry **pentry)
{
	char name[MAXNAMELEN];
	int r, fileno;
	struct DirEntry *entry;
	struct Inode *dir;

	if ((r = walk_path(path, &dir, &entry, name)) == 0)
		return -E_FILE_EXISTS;
	if (r != -E_NOT_FOUND || dir == 0)
		return r;
	if ((r = dir_alloc_entry(dir, &entry)) < 0)
		return r;
	if ((fileno = alloc_inode()) < 0)
		return fileno;
	inodes[fileno].f_refcnt = 1;

	strcpy(entry->f_name, name);
	entry->f_fileno = fileno;
	*pentry = entry;
	file_flush(dir);
	return 0;
}

// Open "path".  On success set *pf to point at the directory
// entry of this file and return 0.
// On error return < 0.
int
file_open(const char *path, struct DirEntry **pentry)
{
	return walk_path(path, 0, pentry, 0);
}

// Read count bytes from f into buf, starting from seek position
// offset.  This meant to mimic the standard pread function.
// Returns the number of bytes read, < 0 on error.
ssize_t
file_read(struct Inode *f, void *buf, size_t count, off_t offset)
{
	int r, bn;
	off_t pos;
	char *blk;

	if (offset >= f->f_size)
		return 0;

	count = MIN(count, f->f_size - offset);

	for (pos = offset; pos < offset + count; ) {
		if ((r = file_get_block(f, pos / BLKSIZE, &blk)) < 0)
			return r;
		bn = MIN(BLKSIZE - pos % BLKSIZE, offset + count - pos);
		memmove(buf, blk + pos % BLKSIZE, bn);
		pos += bn;
		buf += bn;
	}

	return count;
}

// Write count bytes from buf into f, starting at seek position
// offset.  This is meant to mimic the standard pwrite function.
// Extends the file if necessary.
// Returns the number of bytes written, < 0 on error.
int
file_write(struct Inode *f, const void *buf, size_t count, off_t offset)
{
	int r, bn;
	off_t pos;
	char *blk;

	// Extend file if necessary
	if (offset + count > f->f_size)
		if ((r = file_set_size(f, offset + count)) < 0)
			return r;

	for (pos = offset; pos < offset + count; ) {
		if ((r = file_get_block(f, pos / BLKSIZE, &blk)) < 0)
			return r;
		bn = MIN(BLKSIZE - pos % BLKSIZE, offset + count - pos);
		memmove(blk + pos % BLKSIZE, buf, bn);
		pos += bn;
		buf += bn;
	}

	return count;
}

// Remove a block from file f.  If it's not there, just silently succeed.
// Returns 0 on success, < 0 on error.
static int
file_free_block(struct Inode *f, uint32_t filebno)
{
	int r;
	uint32_t *ptr;

	if ((r = file_block_walk(f, filebno, &ptr, 0)) < 0)
		return r;
	if (*ptr) {
		free_block(*ptr);
		*ptr = 0;
	}
	return 0;
}

// Remove any blocks currently used by file 'f',
// but not necessary for a file of size 'newsize'.
// For both the old and new sizes, figure out the number of blocks required,
// and then clear the blocks from new_nblocks to old_nblocks.
// If the new_nblocks is no more than NDIRECT, and the indirect block has
// been allocated (f->f_indirect != 0), then free the indirect block too.
// (Remember to clear the f->f_indirect pointer so you'll know
// whether it's valid!)
// Do not change f->f_size.
static void
file_truncate_blocks(struct Inode *f, off_t newsize)
{
	int r;
	uint32_t bno, old_nblocks, new_nblocks;

	old_nblocks = (f->f_size + BLKSIZE - 1) / BLKSIZE;
	new_nblocks = (newsize + BLKSIZE - 1) / BLKSIZE;
	for (bno = new_nblocks; bno < old_nblocks; bno++)
		if ((r = file_free_block(f, bno)) < 0)
			cprintf("warning: file_free_block: %e", r);

	if (new_nblocks <= NDIRECT && f->f_indirect) {
		free_block(f->f_indirect);
		f->f_indirect = 0;
	}
}

// Set the size of file f, truncating or extending as necessary.
int
file_set_size(struct Inode *f, off_t newsize)
{
	if (f->f_size > newsize)
		file_truncate_blocks(f, newsize);
	f->f_size = newsize;
	flush_block(f);
	return 0;
}

// Flush the contents and metadata of file f out to disk.
// Loop over all the blocks in file.
// Translate the file block number into a disk block number
// and then check whether that disk block is dirty.  If so, write it out.
void
file_flush(struct Inode *f)
{
	int i;
	uint32_t *pdiskbno;

	for (i = 0; i < (f->f_size + BLKSIZE - 1) / BLKSIZE; i++) {
		if (file_block_walk(f, i, &pdiskbno, 0) < 0 ||
		    pdiskbno == NULL || *pdiskbno == 0)
			continue;
		flush_block(diskaddr(*pdiskbno));
	}
	flush_block(f);
	if (f->f_indirect)
		flush_block(diskaddr(f->f_indirect));
}

// Sync the entire file system.  A big hammer.
void
fs_sync(void)
{
	int i;
	for (i = 1; i < super->s_nblocks; i++)
		flush_block(diskaddr(i));
}

