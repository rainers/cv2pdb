__attribute__((noinline)) int get(volatile int *p) { return *p; }

__attribute__((noinline)) int frame(int a, int b)
{
  volatile int local = a * b;
  int arr[4] = { a, b, a, b };
  return get(&local) + arr[a & 3];
}

__attribute__((noinline)) int dynamic(int n)
{
  int buf[n];
  volatile int marker = n * 7;
  for (int i = 0; i < n; i++)
    buf[i] = i + marker;
  return get(&marker) + buf[n - 1];
}

int start(void) { return frame(1, 2) + dynamic(3); }
