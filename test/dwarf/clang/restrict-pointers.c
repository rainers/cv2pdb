struct S { int m; };
typedef char *P;

char g_text[] = "abcd";
struct S g_s = { 1 };
char *restrict g_rp = g_text;
const char *restrict g_crp = g_text;
char *const restrict g_rcp = g_text;
char *const volatile restrict g_rcvp = g_text;
struct S *restrict g_rsp = &g_s;
P restrict g_rtp = g_text;

int main(void)
{
  return *g_rp + *g_crp + *g_rcp + *g_rcvp + g_rsp->m + *g_rtp;
}
