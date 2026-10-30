#include <stdio.h>
#include <stdlib.h>

__attribute__((noinline)) int get(int i) { return rand() + i; }

#define R1(i) acc = acc * 31 + get(acc ^ (i));
#define R4(i) R1(i) R1(i + 1) R1(i + 2) R1(i + 3)
#define R16(i) R4(i) R4(i + 4) R4(i + 8) R4(i + 12)
#define R64(i) R16(i) R16(i + 16) R16(i + 32) R16(i + 48)
#define R256(i) R64(i) R64(i + 64) R64(i + 128) R64(i + 192)
#define R1K(i) R256(i) R256(i + 256) R256(i + 512) R256(i + 768)

__attribute__((noinline)) int big(int seed)
{
  int acc = get(seed);
  R1K(0) R1K(1024) R1K(2048) R1K(3072)
  return acc * seed;
}

int main(int argc, char **argv)
{
  printf("%d\n", big(argc));
  return 0;
}
