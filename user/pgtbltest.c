#include "kernel/types.h"
#include "kernel/riscv.h"
#include "user/user.h"

static void
err(const char *msg)
{
  printf("pgtbltest: FAILED: %s\n", msg);
  exit(1);
}

static void
pgaccess_basic_test(char *buf)
{
  unsigned int abits = 0;

  // Baseline call clears any PTE_A bits set during malloc() initialization.
  if(pgaccess(buf, 32, &abits) < 0)
    err("baseline pgaccess failed");

  buf[PGSIZE * 1] += 1;
  buf[PGSIZE * 2] += 1;
  buf[PGSIZE * 30] += 1;

  if(pgaccess(buf, 32, &abits) < 0)
    err("pgaccess failed");

  unsigned int expected = (1U << 1) | (1U << 2) | (1U << 30);
  if(abits != expected){
    printf("pgaccess_basic_test: got 0x%x, expected 0x%x\n", abits, expected);
    err("incorrect access bits for pages 1, 2, 30");
  }
  printf("pgaccess_basic_test: OK (abits=0x%x)\n", abits);
}

static void
pgaccess_clear_test(char *buf)
{
  unsigned int abits = 0xffffffffU;

  if(pgaccess(buf, 32, &abits) < 0)
    err("pgaccess clear check failed");

  if(abits != 0){
    printf("pgaccess_clear_test: got 0x%x, expected 0\n", abits);
    err("PTE_A bits were not cleared");
  }
  printf("pgaccess_clear_test: OK (abits=0x%x)\n", abits);
}

static void
pgaccess_read_write_test(char *buf)
{
  unsigned int abits = 0;

  if(pgaccess(buf, 32, &abits) < 0)
    err("baseline pgaccess failed");

  // Volatile read ensures compiler emits a load instruction on page 0.
  volatile char r = buf[0];
  (void)r;
  // Write to page 15.
  buf[PGSIZE * 15] = 0x5a;

  if(pgaccess(buf, 32, &abits) < 0)
    err("pgaccess read/write failed");

  unsigned int expected = (1U << 0) | (1U << 15);
  if(abits != expected){
    printf("pgaccess_read_write_test: got 0x%x, expected 0x%x\n", abits, expected);
    err("incorrect access bits for read page 0 and write page 15");
  }
  printf("pgaccess_read_write_test: OK (abits=0x%x)\n", abits);
}

static void
pgaccess_invalid_test(char *buf)
{
  unsigned int abits = 0;

  if(pgaccess(buf, -1, &abits) != -1)
    err("negative len should fail");
  if(pgaccess(buf, 33, &abits) != -1)
    err("len > 32 should fail");
  if(pgaccess(buf, 1, (void *)0xffffffffffffffffULL) != -1)
    err("kernel/invalid mask pointer should fail");
  if(pgaccess(buf, 1, (void *)0x0) != -1)
    err("read-only user address 0 mask pointer should fail");
  if(pgaccess((void *)MAXVA, 1, &abits) != -1)
    err("base >= MAXVA should fail");
  if(pgaccess((void *)(MAXVA - PGSIZE), 2, &abits) != -1)
    err("range crossing MAXVA should fail");

  printf("pgaccess_invalid_test: OK\n");
}

int
main(int argc, char *argv[])
{
  printf("pgtbltest starting\n");

  char *buf = malloc(32 * PGSIZE);
  if(buf == 0)
    err("malloc failed");

  pgaccess_basic_test(buf);
  pgaccess_clear_test(buf);
  pgaccess_read_write_test(buf);
  pgaccess_invalid_test(buf);

  free(buf);
  printf("pgtbltest: all tests passed!\n");
  exit(0);
}
