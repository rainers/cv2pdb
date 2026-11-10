#include <stdio.h>
#include <stdlib.h>

__attribute__((noinline)) int get(unsigned i) { return rand() + i; }

__attribute__((noinline)) int shadow(int n)
{
  unsigned x = 0;
  int r = get(x) + n;
  {
    unsigned x = get(r);
    r += get(x);
    r += get(x);
  }
  x = 3;
  r += get(x);
  r += get(r);
  return r;
}

int main(int argc, char **argv)
{
  printf("%d\n", shadow(argc));
  return 0;
}
