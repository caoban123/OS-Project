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

  // Validate the entire user buffer before writing anything.
  // This also prevents unsigned address wraparound.
  if(addr >= p->sz ||
     sizeof(struct sysinfo) > p->sz - addr)
    return -1;

  int nfiles = 0;
  int nprocs = 0;

  struct sysinfo_file file_info;
  struct sysinfo_proc proc_info;

  // Collect system-wide open file objects.
  for(int i = 0; i < NFILE; i++){
    memset(&file_info, 0, sizeof(file_info));

    int result = get_open_file(i, &file_info);

    if(result < 0)
      return -1;

    if(result == 0)
      continue;

    if(nfiles >= NFILE)
      return -1;

    get_file_pids(i, &file_info);

    uint64 dst = addr
      + __builtin_offsetof(struct sysinfo, files)
      + (uint64)nfiles * sizeof(struct sysinfo_file);

    if(copyout(p->pagetable, dst,
               (char *)&file_info,
               sizeof(file_info)) < 0)
      return -1;

    nfiles++;
  }

  // Collect active processes.
  for(int i = 0; i < NPROC; i++){
    memset(&proc_info, 0, sizeof(proc_info));

    int result = get_process_info(i, &proc_info);

    if(result < 0)
      return -1;

    if(result == 0)
      continue;

    if(nprocs >= NPROC)
      return -1;

    uint64 dst = addr
      + __builtin_offsetof(struct sysinfo, processes)
      + (uint64)nprocs * sizeof(struct sysinfo_proc);

    if(copyout(p->pagetable, dst,
               (char *)&proc_info,
               sizeof(proc_info)) < 0)
      return -1;

    nprocs++;
  }

  // Write counters after successfully collecting records.
  if(copyout(p->pagetable,
             addr + __builtin_offsetof(struct sysinfo, nopenfiles),
             (char *)&nfiles, sizeof(nfiles)) < 0)
    return -1;

  if(copyout(p->pagetable,
             addr + __builtin_offsetof(struct sysinfo, nproc),
             (char *)&nprocs, sizeof(nprocs)) < 0)
    return -1;

  return 0;
}