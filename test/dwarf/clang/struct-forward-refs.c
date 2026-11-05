typedef struct Tdf Tdf;
struct Tdf { Tdf *next; int v; };

struct PP { struct PP **pp; int v; };

struct CS { const struct CS *c; int v; };

struct Anon { union { struct { struct Anon *next; int h; }; struct { struct Anon *prev; int g; }; }; };

struct MB;
struct MA { struct MB *b; int v; };
struct MB { struct MA *a; int w; };

struct Outer { struct { struct Outer *up; int v; } inner; };

Tdf g_tdf;
struct PP g_pp;
struct CS g_cs;
struct Anon g_anon;
struct MA g_ma;
struct MB g_mb;
struct Outer g_outer;

int main(void)
{
  g_tdf.next = &g_tdf;
  g_cs.c = &g_cs;
  g_anon.next = &g_anon;
  g_ma.b = &g_mb;
  g_mb.a = &g_ma;
  g_outer.inner.up = &g_outer;
  return g_tdf.v + g_pp.v + g_cs.v + g_anon.h + g_ma.v + g_mb.w + g_outer.inner.v;
}
