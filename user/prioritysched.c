#include <inc/lib.h>

int
wrapped_fork(void)
{
    envid_t envid = fork();
    if (envid < 0)
        panic("fork error(%e)", envid);
    if (envid == 0)
        cprintf("Hello, I am environment %08x\n", sys_getenvid());
    return envid;
}

void 
umain(int argc, char **argv)
{
    int errno;
    envid_t envid;

    cprintf("Hello, I am environment %08x\n", sys_getenvid());
    sys_env_set_priority(0, -1);
    if ((envid = wrapped_fork()) != 0) {
        // create a new child environment of priority 1
        sys_env_set_priority(envid, 1);
        if ((envid = wrapped_fork()) != 0) {
            // create a new child environment of priority -2
            sys_env_set_priority(envid, -2);
            if (wrapped_fork() != 0)
                if (wrapped_fork() != 0)
                    if (wrapped_fork() != 0) {
                        // create three child environments of priorty 0
                        if ((envid = wrapped_fork()) != 0) {
                            // create a new child environment of priority -1
                            sys_env_set_priority(envid, -1);
                        }
                    }
        }
    }
    
    for (int run = 0; run < 3; ++run) {
        cprintf("Back in environment %08x[prio=%s%d], iteration %d.\n",
                thisenv->env_id,
                thisenv->env_priority >= 0 ? " " : "",
                thisenv->env_priority,
                run);
        sys_yield();
    }
}
