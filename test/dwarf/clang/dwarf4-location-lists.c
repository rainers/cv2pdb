#include <stdio.h>
#include <stdlib.h>

#define CLOBBER __asm__ volatile("" ::: "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15", "memory")

__attribute__((noinline)) int get(int i) { return rand() + i; }

__attribute__((noinline)) int spill(int a, int b)
{
  int s = get(a);
  CLOBBER;
  s += get(b);
  CLOBBER;
  s += get(s);
  CLOBBER;
  return s * a + b;
}

int main(int argc, char **argv)
{
  printf("%d\n", spill(argc, argc + 1));
  return 0;
}
