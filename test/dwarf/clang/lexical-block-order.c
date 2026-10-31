volatile int g_sink;

__attribute__((noinline)) int peek(int *p)
{
  g_sink = *p;
  return *p;
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
    int hot_slot = n + 2000;
    r += peek(&hot_slot);
    if (__builtin_expect(n > 50, 0))
    {
      int inner_cold_slot = n + 3000;
      r += peek(&inner_cold_slot);
    }
    {
      int inner_hot_slot = n + 4000;
      r += peek(&inner_hot_slot);
    }
  }
  return r;
}

int main(int argc, char **argv)
{
  return target(argc);
}
