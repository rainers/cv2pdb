__attribute__((noinline)) void sink(int *p, long long *q, int *r)
{
  __asm__ volatile("" :: "r"(p), "r"(q), "r"(r) : "memory");
}

__attribute__((noinline)) int mixed(int a, int b)
{
  int r = a ^ b;
  int lv = a * 3;
  long long lll = a;
  sink(&lv, &lll, &b);
  r += lv;
  sink(&lv, &lll, &b);
  return r + lv + (int)lll + b;
}

int main(int argc, char **argv)
{
  return mixed(argc, argc + 1);
}
