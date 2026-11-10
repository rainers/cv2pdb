volatile int g_sink;

__attribute__((noinline)) int peek(int *p)
{
  g_sink = *p;
  return *p;
}

__attribute__((noinline)) int step(int v)
{
  g_sink = v;
  return v + 1;
}

__attribute__((noinline)) int target(int n)
{
  int r = 0;
  if (__builtin_expect(n > 100, 0))
  {
    int cold_slot = n + 1000;
    r += peek(&cold_slot);
  }
  {
    int count = step(n);
    r += step(count);
    if (__builtin_expect(n > 50, 0))
      r += step(count * 5);
    int late = step(count + r);
    r += step(late);
  }
  return r;
}

int main(int argc, char **argv)
{
  return target(argc + 70);
}
