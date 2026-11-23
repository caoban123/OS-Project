#include "kernel/param.h"
#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  char *nargv[MAXARG];

  if(argc < 2){
    fprintf(2, "Usage: %s command [args...]\n", argv[0]);
    exit(1);
  }

  int i;
  for(i = 1; i < argc && i - 1 < MAXARG - 1; i++){
    nargv[i - 1] = argv[i];
  }
  nargv[i - 1] = 0;

  exec_vmprint(nargv[0], nargv);
  fprintf(2, "vmprint: exec %s failed\n", nargv[0]);
  exit(1);
}
