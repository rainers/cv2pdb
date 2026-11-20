#include <stdio.h>
#include <stdlib.h>

volatile int g_sink;

__attribute__((noinline)) int get(unsigned i) { return rand() + i; }

static inline __attribute__((always_inline)) int twice(int v)
{
  int doubled = get(v) * 2;
  g_sink = doubled;
  return doubled + v;
}

__attribute__((noinline)) int across(int n)
{
  unsigned k = 0;
  int r = get(k) + n;
  r += twice(r);
  k = 3;
  r += get(k);
  r += twice(r);
  return r;
}

int main(int argc, char **argv)
{
  printf("%d\n", across(argc));
  return 0;
}
