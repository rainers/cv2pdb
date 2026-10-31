#include <stdio.h>
#include <stdlib.h>

struct hook_table { void (*begin)(); void (*end)(); };

__attribute__((noinline)) int get(int i) { return rand() + i; }

namespace host {
int calls;

__attribute__((noinline)) int run(int n);
}

static hook_table hooks;

__attribute__((noinline)) void set_hooks(void (*b)(), void (*e)())
{
  hooks.begin = b;
  hooks.end = e;
}

__attribute__((noinline)) int host::run(int n)
{
  int s = get(n);
  calls++;
  if (hooks.begin)
    hooks.begin();
  s += get(s);
  if (hooks.end)
    hooks.end();
  return s * n;
}

static void on_begin() { puts("begin"); }
static void on_end() { puts("end"); }

__attribute__((noinline)) void run_table(int set)
{
  static hook_table table;
  if (set) {
    table.begin = on_begin;
    table.end = on_end;
  }
  if (table.begin)
    table.begin();
  if (table.end)
    table.end();
}

int main(int argc, char **argv)
{
  set_hooks(on_begin, on_end);
  printf("%d %d\n", host::run(argc), host::calls);
  run_table(argc);
  run_table(0);
  return 0;
}
