#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sysinfo.h"


uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if(n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64
sys_sysinfo(void)
{
  uint64 addr;
  struct proc *p = myproc();

  argaddr(0, &addr);

  // Check bounds first; copyout also checks page mappings and permissions.
  // This also prevents unsigned address wraparound.
  if(addr >= p->sz ||
     sizeof(struct sysinfo) > p->sz - addr)
    return -1;

  return systeminfo(addr);
}

uint64
sys_trace(void)
{
  int mask;

  argint(0, &mask);
  myproc()->trace_mask = mask;

  return 0;
}

#define PGACCESS_MAX 32

uint64
sys_pgaccess(void)
{
  uint64 base;
  int len;
  uint64 mask_addr;

  argaddr(0, &base);
  argint(1, &len);
  argaddr(2, &mask_addr);

  if(len < 0 || len > PGACCESS_MAX)
    return -1;

  uint64 va = PGROUNDDOWN(base);
  if(va >= MAXVA || (uint64)len * PGSIZE > MAXVA - va)
    return -1;

  struct proc *p = myproc();
  uint32 abits = 0;

  for(int i = 0; i < len; i++, va += PGSIZE){
    pte_t *pte = walk(p->pagetable, va, 0);
    if(pte != 0 && (*pte & PTE_V) && (*pte & PTE_U) && (*pte & PTE_A)){
      abits |= (1U << i);
      *pte &= ~PTE_A;
    }
  }

  if(copyout(p->pagetable, mask_addr, (char *)&abits, sizeof(abits)) < 0)
    return -1;

  return 0;
}
