
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "kernel/sysinfo.h"
#include "user/user.h"

static struct sysinfo *info;

void validate_snapshot(void);

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
  validate_snapshot();
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

// Cross-check both directions of the file/process relationship on every call,
// including while workers concurrently allocate, duplicate and close files.
void
validate_snapshot(void)
{
  for(int i = 0; i < info->nproc; i++){
    struct sysinfo_proc *p = &info->processes[i];
    require(p->pid > 0 && p->state > 0 && p->state <= 5,
            "Invalid process identity or state");
    require(p->nfds >= 0 && p->nfds <= NOFILE, "Invalid FD count");
    for(int j = 0; j < i; j++)
      require(p->pid != info->processes[j].pid, "Duplicate process PID");
    for(int j = 0; j < p->nfds; j++){
      require(p->fds[j].fd >= 0 && p->fds[j].fd < NOFILE, "Invalid FD number");
      for(int k = 0; k < j; k++)
        require(p->fds[j].fd != p->fds[k].fd, "Duplicate FD number");
      struct sysinfo_file *f = find_file(p->fds[j].file_id);
      require(f != 0 && has_pid(f, p->pid), "FD and file owner list disagree");
    }
  }
  for(int i = 0; i < info->nopenfiles; i++){
    struct sysinfo_file *f = &info->files[i];
    require(f->file_id >= 0 && f->file_id < NFILE && f->ref > 0,
            "Invalid file identity or reference count");
    require(f->type >= 1 && f->type <= 3, "Uninitialized file metadata");
    require(f->npids >= 0 && f->npids <= NPROC, "Invalid file PID count");
    for(int j = 0; j < i; j++)
      require(f->file_id != info->files[j].file_id, "Duplicate file ID");
    int references = 0;
    for(int j = 0; j < info->nproc; j++){
      struct sysinfo_proc *p = &info->processes[j];
      for(int k = 0; k < p->nfds; k++)
        references += p->fds[k].file_id == f->file_id;
    }
    // open/close may hold temporary references outside a process FD table.
    require(f->ref >= references, "File ref count smaller than visible FD count");
    for(int j = 0; j < f->npids; j++){
      struct sysinfo_proc *p = find_proc(f->pids[j]);
      require(p != 0, "File references missing process");
      for(int k = 0; k < j; k++)
        require(f->pids[k] != f->pids[j], "Duplicate PID in file owner list");
      int found = 0;
      for(int k = 0; k < p->nfds; k++)
        found |= p->fds[k].file_id == f->file_id;
      require(found, "File owner has no matching FD");
    }
  }
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
  require(!has_pid(f, child) && has_pid(f, me),
          "File owners not updated after child exit");

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

// Values exported by sysinfo match kernel/file.h and kernel/proc.h.
// Keep user tests independent of those kernel-private structure definitions.
enum { INFO_PIPE = 1, INFO_INODE = 2, INFO_DEVICE = 3 };
enum { INFO_UNUSED = 0, INFO_RUNNING = 4, INFO_ZOMBIE = 5 };

void
test_types_and_modes(void)
{
  printf("\n=== FILE TYPES / MODES / INDEPENDENT OPEN ===\n");
  int me = getpid();
  int seed = open("sysinfo_modes.tmp", O_CREATE | O_RDWR);
  require(seed >= 0, "Cannot create mode test file");
  require(close(seed) == 0, "Cannot close seed file");

  refresh();
  int before = info->nopenfiles;
  int modes[] = {O_RDONLY, O_WRONLY, O_RDWR};
  int fds[3];
  int ids[3];
  for(int i = 0; i < 3; i++){
    fds[i] = open("sysinfo_modes.tmp", modes[i]);
    require(fds[i] >= 0, "Cannot open mode test file");
  }
  refresh();
  require(info->nopenfiles == before + 3,
          "Independent opens must create three file objects");
  for(int i = 0; i < 3; i++){
    ids[i] = find_file_id(me, fds[i]);
    struct sysinfo_file *f = find_file(ids[i]);
    require(f != 0 && f->type == INFO_INODE, "Incorrect inode file type");
    require(!!f->readable == (i != 1) && !!f->writable == (i != 0),
            "Incorrect access mode");
    require(f->ref == 1 && f->npids == 1 && has_pid(f, me),
            "Incorrect ownership of independent open");
    for(int j = 0; j < i; j++)
      require(ids[i] != ids[j], "Independent opens share a file object");
    printf("FD=%d file_id=%d type=%d read=%d write=%d ref=%d\n",
           fds[i], ids[i], f->type, f->readable, f->writable, f->ref);
  }
  for(int i = 0; i < 3; i++)
    require(close(fds[i]) == 0, "Cannot close mode test FD");
  require(unlink("sysinfo_modes.tmp") == 0, "Cannot remove mode test file");

  int ends[2];
  require(pipe(ends) == 0, "Cannot create type test pipe");
  int console = open("console", O_RDWR);
  require(console >= 0, "Cannot open console device");
  refresh();
  int read_id = find_file_id(me, ends[0]);
  int write_id = find_file_id(me, ends[1]);
  struct sysinfo_file *r = find_file(read_id);
  struct sysinfo_file *w = find_file(write_id);
  struct sysinfo_file *d = find_file(find_file_id(me, console));
  require(read_id != write_id && r != 0 && w != 0,
          "Pipe endpoints must be distinct file objects");
  require(r->type == INFO_PIPE && r->readable && !r->writable,
          "Incorrect pipe reader metadata");
  require(w->type == INFO_PIPE && !w->readable && w->writable,
          "Incorrect pipe writer metadata");
  require(d != 0 && d->type == INFO_DEVICE && d->readable && d->writable,
          "Incorrect console device metadata");
  printf("Pipe reader=%d writer=%d; console type=%d\n",
         read_id, write_id, d->type);
  require(close(ends[0]) == 0 && close(ends[1]) == 0 && close(console) == 0,
          "Cannot close type test files");
  refresh();
  require(info->nopenfiles == before, "File objects leaked after type tests");
}

void
test_fd_limit(void)
{
  printf("\n=== DUP / FD LIMIT / FAILED OPEN ===\n");
  int me = getpid();
  refresh();
  int before_files = info->nopenfiles;
  struct sysinfo_proc *p = find_proc(me);
  require(p != 0, "Current process missing");
  int before_fds = p->nfds;
  int fd = open("README", O_RDONLY);
  require(fd >= 0, "Cannot open README");
  refresh();
  int id = find_file_id(me, fd);
  struct sysinfo_file *f = find_file(id);
  require(f != 0, "README file object missing");
  int original_ref = f->ref;
  int duplicates[NOFILE];
  int n = 0;
  int next;
  while((next = dup(fd)) >= 0){
    require(n < NOFILE, "dup exceeded FD limit");
    duplicates[n++] = next;
  }
  require(n == NOFILE - before_fds - 1, "Unexpected per-process FD limit");
  require(open("README", O_RDONLY) == -1, "open accepted full FD table");
  refresh();
  p = find_proc(me);
  f = find_file(id);
  require(p != 0 && p->nfds == NOFILE, "Full FD table reported incorrectly");
  require(info->nopenfiles == before_files + 1,
          "dup or failed open leaked a file object");
  require(f != 0 && f->ref == original_ref + n,
          "Incorrect ref count with multiple duplicates");
  require(f->npids == 1 && has_pid(f, me), "dup duplicated PID in owner list");
  for(int i = 0; i < n; i++){
    require(find_file_id(me, duplicates[i]) == id, "Duplicate maps to wrong file");
    require(close(duplicates[i]) == 0, "Cannot close duplicate");
  }
  refresh();
  f = find_file(id);
  require(f != 0 && f->ref == original_ref, "Closing duplicates lost original file");
  require(close(fd) == 0, "Cannot close original FD");
  refresh();
  require(info->nopenfiles == before_files, "FD limit test leaked file objects");
  printf("Checked %d duplicates; PID appears once; failed open cleaned up\n", n);
}

void
test_zombie_and_scheduling(void)
{
  printf("\n=== SCHEDULING / ZOMBIE / WAIT ===\n");
  int me = getpid();
  refresh();
  struct sysinfo_proc *p = find_proc(me);
  require(p != 0 && p->state == INFO_RUNNING, "Caller is not RUNNING");
  uint64 count = p->scheduling_count;
  require(sleep(2) == 0, "Scheduling test sleep failed");
  refresh();
  p = find_proc(me);
  require(p != 0 && p->scheduling_count > count,
          "Scheduling count did not increase after sleep");

  int before = info->nproc;
  int child = fork();
  require(child >= 0, "Zombie test fork failed");
  if(child == 0)
    exit(23);

  int zombie = 0;
  for(int attempt = 0; attempt < 100; attempt++){
    refresh();
    p = find_proc(child);
    require(p != 0 && p->state != INFO_UNUSED, "Unreaped child missing");
    if(p->state == INFO_ZOMBIE){
      zombie = 1;
      break;
    }
    sleep(1);
  }
  require(zombie, "Child never reached ZOMBIE");
  require(info->nproc == before + 1, "ZOMBIE not counted as active process");
  require(p->parent_pid == me && p->nfds == 0 && p->scheduling_count > 0,
          "Incorrect zombie metadata");
  printf("Child PID=%d state=%d parent=%d nfds=%d\n",
         child, p->state, p->parent_pid, p->nfds);
  int status = -1;
  require(wait(&status) == child && status == 23, "Incorrect child exit status");
  refresh();
  require(find_proc(child) == 0 && info->nproc == before,
          "Reaped child still counted");
}

void
test_buffer_boundary(void)
{
  printf("\n=== USER BUFFER BOUNDARIES ===\n");
  // The struct would extend one byte past the process break.
  char *end = sbrk(0);
  struct sysinfo *short_buffer = (struct sysinfo *)(end - sizeof(*info) + 1);
  require(sysinfo(short_buffer) == -1, "Buffer past process break accepted");
  require(sysinfo((struct sysinfo *)end) == -1, "Process break accepted as buffer");
  // Recover normally after rejected requests.
  refresh();
  printf("Out-of-range buffers rejected; subsequent valid call succeeded\n");
}

void
test_concurrent_calls(void)
{
  printf("\n=== CONCURRENT SYSINFO / OPEN / CLOSE ===\n");
  refresh();
  int before_files = info->nopenfiles;
  int before_procs = info->nproc;
  int gate[2];
  require(pipe(gate) == 0, "Cannot create concurrent test gate");
  int children[2];
  for(int i = 0; i < 2; i++){
    children[i] = fork();
    require(children[i] >= 0, "Concurrent test fork failed");
    if(children[i] == 0){
      close(gate[1]);
      char signal;
      require(read(gate[0], &signal, 1) == 1, "Cannot wait for test gate");
      close(gate[0]);
      for(int j = 0; j < 20; j++){
        int fd = open("README", O_RDONLY);
        require(fd >= 0, "Concurrent open failed");
        int duplicate = dup(fd);
        require(duplicate >= 0, "Concurrent dup failed");
        int ends[2];
        require(pipe(ends) == 0, "Concurrent pipe failed");
        int child = fork();
        require(child >= 0, "Concurrent worker fork failed");
        if(child == 0)
          exit(0);
        refresh();
        struct sysinfo_proc *self = find_proc(getpid());
        require(self != 0 && self->nfds <= NOFILE,
                "Concurrent caller metadata missing");
        int id = find_file_id(getpid(), fd);
        require(id >= 0 && id == find_file_id(getpid(), duplicate),
                "Concurrent duplicate mapping missing or different");
        int status = -1;
        require(wait(&status) == child && status == 0,
                "Concurrent worker child failed");
        require(close(ends[0]) == 0 && close(ends[1]) == 0 &&
                close(duplicate) == 0 && close(fd) == 0,
                "Concurrent close failed");
      }
      exit(0);
    }
  }
  close(gate[0]);
  require(write(gate[1], "XX", 2) == 2, "Cannot release concurrent children");
  close(gate[1]);
  for(int j = 0; j < 20; j++)
    refresh();
  int reaped[2] = {0, 0};
  for(int i = 0; i < 2; i++){
    int status = -1;
    int pid = wait(&status);
    int worker = pid == children[0] ? 0 : pid == children[1] ? 1 : -1;
    require(worker >= 0 && status == 0, "Concurrent worker failed");
    require(!reaped[worker], "Worker reaped twice");
    reaped[worker] = 1;
  }
  refresh();
  require(find_proc(children[0]) == 0 && find_proc(children[1]) == 0,
          "Concurrent children remain after wait");
  require(info->nopenfiles == before_files && info->nproc == before_procs,
          "Concurrent test leaked file objects or processes");
  printf("Two workers and parent completed concurrent calls\n");
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
  test_types_and_modes();
  test_fd_limit();
  test_zombie_and_scheduling();
  test_buffer_boundary();
  test_concurrent_calls();

  printf("\n===== TEST SUMMARY =====\n");
  printf("All test stages completed successfully\n");

  free(info);
  exit(0);
}
