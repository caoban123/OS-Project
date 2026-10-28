#ifndef SYSINFO_H
#define SYSINFO_H

#include "types.h"
#include "param.h"

struct sysinfo_fd {
  int fd;
  int file_id;
};

// file_id identifies a slot in the system file table within this snapshot.
// It may be reused after the syscall; ref includes temporary kernel references.
struct sysinfo_file {
  int file_id;
  int type;
  int readable;
  int writable;
  int ref;

  int npids;
  int pids[NPROC];
};

struct sysinfo_proc {
  int pid;
  int state;
  int parent_pid;
  uint64 scheduling_count;

  int nfds;
  struct sysinfo_fd fds[NOFILE];
};

// A consistent snapshot. Only the first nopenfiles/nproc array entries are used.
// File ownership lists contain each PID once, even if it has duplicate FDs.
struct sysinfo {
  int nopenfiles;
  int nproc;

  struct sysinfo_file files[NFILE];
  struct sysinfo_proc processes[NPROC];
};

#endif
