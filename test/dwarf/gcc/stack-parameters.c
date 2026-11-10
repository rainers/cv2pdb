__attribute__((noinline)) int stacked(int a, int b, int c, int d, int e)
{
  volatile int buf[16];
  for (int i = 0; i < 16; i++)
    buf[i] = a * i + b;
  e += buf[c & 15] * d;
  return buf[d & 15] + e * c;
}

int main(int argc, char **argv)
{
  return stacked(argc, argc + 1, argc + 2, argc + 3, argc + 4);
}
