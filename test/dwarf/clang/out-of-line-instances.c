volatile int g_sink;

__attribute__((noinline)) void opaque(int *p) { g_sink = *p; }

static int helper(int a)
{
  int local = a * 3;
  opaque(&local);
  opaque(&a);
  return local + a;
}

int (*volatile fp)(int) = helper;

int main(int argc, char **argv)
{
  return helper(argc) + fp(argc);
}
