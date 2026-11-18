typedef unsigned short wide_t[70368744177663ULL];
/* Each dimension of grid_t fits in 32 bits, but its size does not. */
typedef char grid_t[3][2000000000];
wide_t *g_wide;
grid_t *g_grid;

int main(void)
{
  return g_wide != 0 || g_grid != 0;
}
