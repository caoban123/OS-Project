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
static void
pgaccess_edge_test(char *buf)
{
  unsigned int abits = 0xdeadbeefU;

  if(pgaccess(buf, 32, &abits) < 0)
    err("baseline pgaccess failed");

  // 1. len == 0 should succeed, write 0 to mask, and keep existing PTE_A bits.
  buf[PGSIZE * 4] = 0x11;
  abits = 0xdeadbeefU;
  if(pgaccess(buf, 0, &abits) < 0)
    err("len == 0 should succeed");
  if(abits != 0)
    err("len == 0 should clear output mask to 0");

  // 2. Failed copyout must not wipe PTE_A bits already set in hardware.
  if(pgaccess(buf, 32, (void *)0x0) != -1)
    err("invalid mask should fail");
  if(pgaccess(buf, 32, &abits) < 0)
    err("recovery pgaccess failed");
  if(abits != (1U << 4)){
    printf("pgaccess_edge_test: got 0x%x, expected 0x%x\n", abits, (1U << 4));
    err("failed syscall should not lose PTE_A bits");
  }

  // 3. Unaligned base address should still check the containing pages.
  char *unaligned = (char *)PGROUNDUP((uint64)buf) + 123;
  unaligned[PGSIZE * 6] = 0x22;
  if(pgaccess(unaligned, 8, &abits) < 0)
    err("unaligned base pgaccess failed");
  if(abits != (1U << 6))
    err("unaligned base reported wrong page bit");

  // 4. Unmapped pages below MAXVA should report 0 without crashing.
  char *unmapped = sbrk(0) + 4 * PGSIZE;
  abits = 0xffffffffU;
  if(pgaccess(unmapped, 4, &abits) < 0)
    err("unmapped range pgaccess failed");
  if(abits != 0)
    err("unmapped pages should have access bits = 0");

  printf("pgaccess_edge_test: OK\n");
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
  pgaccess_edge_test(buf);

  free(buf);
  printf("pgtbltest: all tests passed!\n");
  exit(0);
}
