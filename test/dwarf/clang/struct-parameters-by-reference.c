#include <stdio.h>
#include <stdlib.h>

struct pair { long long lo, hi; };

__attribute__((noinline)) int get(int i) { return rand() + i; }

__attribute__((noinline)) long long byval(struct pair p, int n)
{
  long long s = get(n);
  s += get((int)s);
  return s + p.lo * p.hi;
}

int main(int argc, char **argv)
{
  struct pair p = { argc, argc + 1 };
  printf("%lld\n", byval(p, argc));
  return 0;
}
