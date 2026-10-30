volatile int g_sink;

__attribute__((noinline)) int opaque(int *p) { g_sink = *p; return *p + 1; }

__attribute__((noinline)) int framed(int a)
{
  int local = a * 3;
  int arr[8];
  for (int i = 0; i < 8; i++)
    arr[i] = opaque(&local) + i;
  return arr[a & 7] + local;
}

int main(int argc, char **argv)
{
  return framed(argc);
}
