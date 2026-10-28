
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "kernel/sysinfo.h"
#include "user/user.h"

static struct sysinfo *info;

void
require(int condition, char *message)
{
  if(!condition){
    printf("ERROR: %s\n", message);
    exit(1);
  }
}

void
refresh(void)
{
  memset(info, 0, sizeof(*info));
  require(sysinfo(info) == 0, "sysinfo syscall failed");

  require(info->nopenfiles >= 0 &&
          info->nopenfiles <= NFILE,
          "Invalid file count");

  require(info->nproc >= 0 &&
          info->nproc <= NPROC,
          "Invalid process count");
}

struct sysinfo_proc *
find_proc(int pid)
{
  for(int i = 0; i < info->nproc; i++){
    if(info->processes[i].pid == pid)
      return &info->processes[i];
  }

  return 0;
}

int
find_file_id(int pid, int fd)
{
  struct sysinfo_proc *p = find_proc(pid);

  if(p == 0)
    return -1;

  if(p->nfds < 0 || p->nfds > NOFILE)
    return -1;

  for(int i = 0; i < p->nfds; i++){
    if(p->fds[i].fd == fd)
      return p->fds[i].file_id;
  }

  return -1;
}

struct sysinfo_file *
find_file(int file_id)
{
  for(int i = 0; i < info->nopenfiles; i++){
    if(info->files[i].file_id == file_id)
      return &info->files[i];
  }

  return 0;
}

int
has_pid(struct sysinfo_file *f, int pid)
{
  if(f == 0 || f->npids < 0 || f->npids > NPROC)
    return 0;

  for(int i = 0; i < f->npids; i++){
    if(f->pids[i] == pid)
      return 1;
  }

  return 0;
}

void
test_basic(void)
{
  printf("\n=== BASIC SYSINFO ===\n");

  refresh();

  int me = getpid();
  struct sysinfo_proc *p = find_proc(me);

  require(info->nproc >= 1,
          "No active processes found");

  require(p != 0,
          "Current process not found");

  require(p->scheduling_count > 0,
          "Invalid scheduling count");

  printf("Current PID: %d\n", me);
  printf("Active processes: %d\n", info->nproc);
  printf("Open file objects: %d\n", info->nopenfiles);
  printf("Scheduling count: %d\n",
         (int)p->scheduling_count);

  printf("Basic sysinfo checks completed\n");
}

void
test_open_dup_close(void)
{
  printf("\n=== OPEN / DUP / CLOSE ===\n");

  int me = getpid();

  refresh();
  int before = info->nopenfiles;

  int fd = open("sysinfo_advanced.tmp",
                O_CREATE | O_RDWR);

  require(fd >= 0, "open failed");

  refresh();

  int file_id = find_file_id(me, fd);
  struct sysinfo_file *f = find_file(file_id);

  require(info->nopenfiles == before + 1,
          "open did not create one file object");

  require(file_id >= 0 && f != 0,
          "File object not found");

  require(f->readable && f->writable,
          "Incorrect read/write permissions");

  require(has_pid(f, me),
          "Current PID missing from file");

  int ref_before = f->ref;

  printf("Opened FD=%d, file_id=%d, ref=%d\n",
         fd, file_id, ref_before);

  int fd2 = dup(fd);

  require(fd2 >= 0, "dup failed");

  refresh();
  f = find_file(file_id);

  require(find_file_id(me, fd2) == file_id,
          "dup does not share file object");

  require(f != 0 && f->ref == ref_before + 1,
          "Incorrect reference count after dup");

  require(info->nopenfiles == before + 1,
          "dup unexpectedly created file object");

  printf("Duplicated FD=%d, file_id=%d, ref=%d\n",
         fd2, file_id, f->ref);

  close(fd2);
  close(fd);

  refresh();

  require(find_file(file_id) == 0,
          "File object remains after close");

  unlink("sysinfo_advanced.tmp");

  printf("Open / dup / close checks completed\n");
}

void
test_fork_wait(void)
{
  printf("\n=== FORK / WAIT ===\n");

  int me = getpid();
  int syncpipe[2];

  require(pipe(syncpipe) == 0,
          "Cannot create synchronization pipe");

  int fd = open("sysinfo_fork.tmp",
                O_CREATE | O_RDWR);

  require(fd >= 0,
          "Cannot open fork test file");

  refresh();

  int file_id = find_file_id(me, fd);
  struct sysinfo_file *f = find_file(file_id);

  require(f != 0,
          "Fork test file object missing");

  int ref_before = f->ref;

  int child = fork();

  require(child >= 0, "fork failed");

  if(child == 0){
    char signal;

    if(read(syncpipe[0], &signal, 1) != 1)
      exit(1);

    exit(0);
  }

  // Parent inspects the child before releasing it.
  refresh();

  struct sysinfo_proc *cp = find_proc(child);
  f = find_file(file_id);

  require(cp != 0,
          "Child process not listed");

  require(cp->parent_pid == me,
          "Incorrect parent PID");

  require(find_file_id(child, fd) == file_id,
          "Child did not inherit open file");

  require(f != 0 && f->ref == ref_before + 1,
          "Incorrect file ref after fork");

  require(has_pid(f, child),
          "Child PID missing from file list");

  printf("Parent PID: %d\n", me);
  printf("Child PID: %d\n", child);
  printf("Inherited file_id: %d\n", file_id);
  printf("Reference count during fork: %d\n",
         f->ref);

  require(write(syncpipe[1], "X", 1) == 1,
          "Cannot release child");

  require(wait(0) == child,
          "wait failed");

  refresh();

  require(find_proc(child) == 0,
          "Child remains after wait");

  f = find_file(file_id);

  require(f != 0 && f->ref == ref_before,
          "File reference not restored");

  close(fd);
  close(syncpipe[0]);
  close(syncpipe[1]);

  unlink("sysinfo_fork.tmp");

  printf("Fork / wait checks completed\n");
}

void
test_invalid_pointer(void)
{
  printf("\n=== INVALID POINTER ===\n");

  int result = sysinfo(
    (struct sysinfo *)0xffffffffffffffffULL
  );

  require(result == -1,
          "Invalid pointer was accepted");

  printf("Invalid pointer correctly rejected\n");
}

void
test_repeated_calls(void)
{
  printf("\n=== REPEATED SYSCALLS ===\n");

  for(int i = 0; i < 20; i++)
    refresh();

  printf("20 consecutive sysinfo calls completed\n");
}

int
main(void)
{
  info = malloc(sizeof(struct sysinfo));

  if(info == 0){
    printf("ERROR: Cannot allocate sysinfo buffer\n");
    exit(1);
  }

  printf("\n===== ADVANCED SYSINFO TEST =====\n");
  printf("Buffer size: %d bytes\n",
         (int)sizeof(struct sysinfo));

  test_basic();
  test_open_dup_close();
  test_fork_wait();
  test_invalid_pointer();
  test_repeated_calls();

  printf("\n===== TEST SUMMARY =====\n");
  printf("All test stages completed successfully\n");

  free(info);
  exit(0);
}
