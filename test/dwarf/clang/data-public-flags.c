struct pair
{
  int a;
  int b;
};

int g_counter = 1;
static int s_counter = 2;
double g_ratio = 0.5;
struct pair g_pair = { 3, 4 };
const char *g_name = "name";

int main(int argc, char **argv)
{
  s_counter += argc;
  return g_counter + s_counter + g_pair.a + (int)g_ratio + g_name[0];
}
