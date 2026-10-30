#define CLOBBER __asm__ volatile("" ::: "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x19", "x20", "x21", "x22", "x23", "x24", "x25", "x26", "x27", "x28", "memory")

volatile int g_seed;

__attribute__((noinline)) int get(int i) { return g_seed + i; }

__attribute__((noinline)) int spill(int a, int b)
{
  int s = get(a);
  CLOBBER;
  s += get(b);
  s += get(s);
  return s * a + b;
}

int start(void) { return spill(1, 2); }
