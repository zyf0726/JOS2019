#include <inc/lib.h>
#include <inc/elf.h>

#define UTEMP2USTACK(addr)\
	((void*) (addr) + (USTACKTOP - PGSIZE) - UTEMP)

// Helper functions for exec.
static int init_stack(envid_t temp, const char **argv, uintptr_t *init_esp);
static int map_segment(envid_t temp, uintptr_t va, size_t memsz,
		       int fd, size_t filesz, off_t fileoffset, int perm);
static int copy_shared_pages(envid_t child);

// Execute a program image loaded from the file system.
// prog: the pathname of the program to run.
// argv: pointer to null-terminated array of pointers to strings,
// 	 which will be passed to the child as its command-line arguments.
// Never returns on success, < 0 on failure.
int
exec(const char *prog, const char **argv)
{
	unsigned char elf_buf[512];
	struct Trapframe temp_tf;
	envid_t temp;

	int fd, i, r;
	struct Elf *elf;
	struct Proghdr *ph;
	int perm;

	if ((r = open(prog, O_RDONLY)) < 0)
		return r;
	fd = r;

	// Read elf header
	elf = (struct Elf*) elf_buf;
	if (readn(fd, elf_buf, sizeof(elf_buf)) != sizeof(elf_buf)
	    || elf->e_magic != ELF_MAGIC) {
		close(fd);
		cprintf("elf magic %08x want %08x\n", elf->e_magic, ELF_MAGIC);
		return -E_NOT_EXEC;
	}

	// Create a temporary child environment
	if ((r = sys_exofork()) < 0)
		return r;
	temp = r;

	// Set up trap frame, including initial stack.
	temp_tf = envs[ENVX(temp)].env_tf;
	temp_tf.tf_eip = elf->e_entry;

	if ((r = init_stack(temp, argv, &temp_tf.tf_esp)) < 0)
		return r;

	// Set up program segments as defined in ELF header.
	ph = (struct Proghdr*) (elf_buf + elf->e_phoff);
	for (i = 0; i < elf->e_phnum; i++, ph++) {
		if (ph->p_type != ELF_PROG_LOAD)
			continue;
		perm = PTE_P | PTE_U;
		if (ph->p_flags & ELF_PROG_FLAG_WRITE)
			perm |= PTE_W;
		if ((r = map_segment(temp, ph->p_va, ph->p_memsz,
				     fd, ph->p_filesz, ph->p_offset, perm)) < 0)
			goto error;
	}
	close(fd);
	fd = -1;

	// Copy shared library state.
	if ((r = copy_shared_pages(temp)) < 0)
		panic("copy_shared_pages: %e", r);

	temp_tf.tf_eflags |= FL_IOPL_3;   // devious: see user/faultio.c
	if ((r = sys_env_set_trapframe(temp, &temp_tf)) < 0)
		panic("sys_env_set_trapframe: %e", r);

	if ((r = sys_env_exec(temp)) < 0)
		panic("sys_env_exec: %e", r);

error:
	sys_env_destroy(temp);
	close(fd);
	return r;
}

// Exec, taking command-line arguments array directly on the stack.
// NOTE: Must have a sentinal of NULL at the end of the args
// (none of the args may be NULL).
int
execl(const char *prog, const char *arg0, ...)
{
	int argc=0;
	va_list vl;
	va_start(vl, arg0);
	while(va_arg(vl, void *) != NULL)
		argc++;
	va_end(vl);

	const char *argv[argc+2];
	argv[0] = arg0;
	argv[argc+1] = NULL;

	va_start(vl, arg0);
	unsigned i;
	for(i=0;i<argc;i++)
		argv[i+1] = va_arg(vl, const char *);
	va_end(vl);
	return exec(prog, argv);
}

// same as 'spawn.c'
static int
init_stack(envid_t temp, const char **argv, uintptr_t *init_esp)
{
	size_t string_size;
	int argc, i, r;
	char *string_store;
	uintptr_t *argv_store;

	string_size = 0;
	for (argc = 0; argv[argc] != 0; argc++)
		string_size += strlen(argv[argc]) + 1;

	string_store = (char*) UTEMP + PGSIZE - string_size;
	argv_store = (uintptr_t*) (ROUNDDOWN(string_store, 4) - 4 * (argc + 1));
	if ((void*) (argv_store - 2) < (void*) UTEMP)
		return -E_NO_MEM;

	if ((r = sys_page_alloc(0, (void*) UTEMP, PTE_P|PTE_U|PTE_W)) < 0)
		return r;

	for (i = 0; i < argc; i++) {
		argv_store[i] = UTEMP2USTACK(string_store);
		strcpy(string_store, argv[i]);
		string_store += strlen(argv[i]) + 1;
	}
	argv_store[argc] = 0;
	assert(string_store == (char*)UTEMP + PGSIZE);

	argv_store[-1] = UTEMP2USTACK(argv_store);
	argv_store[-2] = argc;

	*init_esp = UTEMP2USTACK(&argv_store[-2]);

	if ((r = sys_page_map(0, UTEMP, temp, (void*) (USTACKTOP - PGSIZE), PTE_P | PTE_U | PTE_W)) < 0)
		goto error;
	if ((r = sys_page_unmap(0, UTEMP)) < 0)
		goto error;

	return 0;

error:
	sys_page_unmap(0, UTEMP);
	return r;
}

static int
map_segment(envid_t temp, uintptr_t va, size_t memsz,
	int fd, size_t filesz, off_t fileoffset, int perm)
{
	int i, r;
	void *blk;

	if ((i = PGOFF(va))) {
		va -= i;
		memsz += i;
		filesz += i;
		fileoffset -= i;
	}

	for (i = 0; i < memsz; i += PGSIZE) {
		if (i >= filesz) {
			if ((r = sys_page_alloc(temp, (void*) (va + i), perm)) < 0)
				return r;
		} else {
			if ((r = sys_page_alloc(0, UTEMP, PTE_P|PTE_U|PTE_W)) < 0)
				return r;
			if ((r = seek(fd, fileoffset + i)) < 0)
				return r;
			if ((r = readn(fd, UTEMP, MIN(PGSIZE, filesz-i))) < 0)
				return r;
			if ((r = sys_page_map(0, UTEMP, temp, (void*) (va + i), perm)) < 0)
				panic("exec: sys_page_map data: %e", r);
			sys_page_unmap(0, UTEMP);
		}
	}
	return 0;
}

// Copy the mappings for shared pages into the child address space.
static int
copy_shared_pages(envid_t temp)
{
	for (uintptr_t va = 0; va < USTACKTOP; va += PGSIZE) {
		void *addr = (void *) va;
		if (!(uvpd[PDX(addr)] & PTE_P) || !(uvpt[PGNUM(addr)] & PTE_P))
			continue;
		pte_t pte = uvpt[PGNUM(addr)]; int r;
		if (!(pte & PTE_SHARE))
			continue;
		if ((r = sys_page_map(0, addr, temp, addr, pte & PTE_SYSCALL)) < 0)
			return r;
	}
	return 0;
}
