#include <stdio.h>
#include <stdlib.h>

__attribute__((noinline)) int get(unsigned i) { return rand() + i; }

__attribute__((noinline)) int count(int n)
{
  int s = 0;
  for (unsigned i = 0; i < (unsigned)n; i++)
    s += get(i);
  return s;
}

int main(int argc, char **argv)
{
  printf("%d\n", count(argc));
  return 0;
}
