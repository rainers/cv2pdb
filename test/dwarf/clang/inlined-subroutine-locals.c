volatile int g_sink;

__attribute__((noinline)) void opaque(int *p)
{
  g_sink = *p;
}

static inline __attribute__((always_inline)) int scale(int factor)
{
  int scaled = factor * 3;
  opaque(&scaled);
  return scaled + factor;
}

static inline __attribute__((always_inline)) int offset_of(int unit)
{
  g_sink = 1;
  return unit * 8;
}

int main(int argc, char **argv)
{
  return scale(argc) + offset_of(4);
}
