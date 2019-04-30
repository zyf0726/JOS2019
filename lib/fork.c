// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//

static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.
	if ((err & FEC_WR) == 0)
		panic("pgfault: not a write fault for err=%x va=%08x\n",
				err, utf->utf_fault_va);

	if (!(uvpd[PDX(addr)] & PTE_P) || !(uvpt[PGNUM(addr)] & PTE_P))
		panic("pgfault: page is not present for va=%08x\n", utf->utf_fault_va);
	pte_t pte = uvpt[PGNUM(addr)];
	if ((pte & PTE_COW) == 0)
		panic("pgfault: not a copy-on-write fault for va=%08x\n",
				utf->utf_fault_va);

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.

	// LAB 4: Your code here.
	if ((r = sys_page_alloc(0, PFTEMP, PTE_P | PTE_U | PTE_W)) != 0)
		panic("pgfault: sys_page_alloc error(%e)\n", r);
	memcpy(PFTEMP, ROUNDDOWN(addr, PGSIZE), PGSIZE);
	if ((r = sys_page_map(0, PFTEMP, 0, ROUNDDOWN(addr, PGSIZE),
						  PTE_P | PTE_U | PTE_W)) != 0)
		panic("pgfault: sys_page_map error(%e)\n", r);
	if ((r = sys_page_unmap(0, PFTEMP)) != 0)
		panic("pgfault: sys_page_unmap error(%e)\n", r);
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{
	int r;

	// LAB 4: Your code here.
	void *addr = (void *) (pn * PGSIZE);
	if (!(uvpd[PDX(addr)] & PTE_P) || !(uvpt[pn] & PTE_P))
		return 0;
	pte_t pte = uvpt[pn];
	if ((pte & PTE_W) || (pte & PTE_COW)) {
		// The ordering here - marking a page as COW in the child
		// before marking it in the parent - actually matters.
		if ((r = sys_page_map(0, addr, envid, addr, PTE_P | PTE_U | PTE_COW)) < 0)
			return r;
		if ((r = sys_page_map(0, addr, 0, addr, PTE_P | PTE_U | PTE_COW)) < 0)
			return r;
	}
	else {
		if ((r = sys_page_map(0, addr, envid, addr, pte & PTE_SYSCALL)) < 0)
			return r;
	}
	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	// LAB 4: Your code here.
	int errno; envid_t envid;

	set_pgfault_handler(pgfault);
	if ((envid = sys_exofork()) < 0)
		return envid;
	if (envid == 0) { // child
		thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
	}
	// parent
	for (uintptr_t va = 0; va < USTACKTOP; va += PGSIZE)
		if ((errno = duppage(envid, PGNUM(va))) != 0)
			return errno;
	if ((errno = sys_page_alloc(envid, (void *) (UXSTACKTOP - PGSIZE),
								PTE_P | PTE_U | PTE_W)) != 0)
		return errno;
	if ((errno = sys_env_set_pgfault_upcall(envid, thisenv->env_pgfault_upcall)) != 0)
		return errno;
	if ((errno = sys_env_set_status(envid, ENV_RUNNABLE)) != 0)
		return errno;
	return envid;
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
