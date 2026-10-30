int g_counter = 1;
static int s_counter = 2;

int main(int argc, char **argv)
{
  s_counter += argc;
  return g_counter + s_counter;
}
