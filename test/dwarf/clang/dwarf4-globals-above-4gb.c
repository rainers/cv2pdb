int g_counter = 3;
static int s_total;

int main(int argc, char **argv)
{
  s_total += argc * g_counter;
  return s_total;
}
