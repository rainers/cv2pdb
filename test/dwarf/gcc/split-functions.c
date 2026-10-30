__attribute__((cold,noinline)) void cold() { volatile int i=0; }
__attribute__((dllexport)) void split_func(int i)
{
  if (i)
    cold();
}
