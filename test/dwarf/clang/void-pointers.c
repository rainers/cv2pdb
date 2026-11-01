typedef void opaque;

struct holder {
  void *payload;
};

void *g_vp;
const void *g_cvp;
opaque *g_tvp;
struct holder g_holder;

int main(void)
{
  g_vp = &g_holder;
  g_cvp = g_vp;
  g_tvp = g_vp;
  g_holder.payload = &g_cvp;
  return g_holder.payload != 0;
}
