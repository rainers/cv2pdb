#include <stdio.h>
#include <stdlib.h>

__attribute__((noinline)) int get(int i) { return rand() + i; }

int main(int argc, char **argv)
{
  int sum = 0;
  for (int i = 0; i < argc; i++) {
    int v = get(i);
    if (v & 1) {
      int w = get(v);
      sum += w * v;
    } else
      sum -= v;
  }
  printf("%d\n", sum);
  return 0;
}
