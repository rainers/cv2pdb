typedef int handler(int);

struct ops {
  int (*op)(int, double);
  void (*done)(void);
};

int (*g_fp)(int);
int (*g_var)(const char *, ...);
handler *g_h;
void (*const g_cfp)(void) = 0;
struct ops g_ops;

int twice(int x) { return 2 * x; }

int main(void)
{
  g_fp = twice;
  g_h = twice;
  return g_fp(1) + g_h(2) + (g_var != 0) + (g_ops.op != 0) + (g_ops.done != 0) + (g_cfp != 0);
}
