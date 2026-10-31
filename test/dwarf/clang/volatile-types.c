struct S { volatile int m; int n; };

volatile int g_seed = 7;
volatile long long g_vll = 0x1122334455667788LL;
volatile double g_vd = 1.5;
struct S g_s = { 1, 2 };
volatile struct S g_vs = { 3, 4 };
const int g_c = 5;
const volatile int g_cv = 6;
volatile int *g_pv;
int * volatile g_vp;

int main(void)
{
  g_pv = &g_s.n;
  g_vp = &g_s.n;
  return g_seed + (int)g_vll + (int)g_vd + g_s.m + g_vs.n + g_c + g_cv + *g_pv + *g_vp;
}
