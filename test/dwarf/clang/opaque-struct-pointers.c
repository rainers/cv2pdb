struct Opaque;
typedef struct OpaqueT OpaqueT;
union OpaqueU;
struct HasOpaque { struct Opaque *op; OpaqueT *opt; int v; };

struct Opaque *g_op = (struct Opaque *)0x1234;
OpaqueT *g_opt = (OpaqueT *)0x5678;
union OpaqueU *g_opu;
struct HasOpaque g_has;

int main(void)
{
  return g_has.v + (g_op != 0) + (g_opt != 0) + (g_opu != 0);
}
