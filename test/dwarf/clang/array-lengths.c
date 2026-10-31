char g_chars[8] = "abcdefg";
int g_matrix[3][5];
struct with_array { int n; short member_items[6]; } g_struct;
struct zero_length { int n; char member_zero[0]; } g_zero;

int main(int argc, char **argv)
{
  return g_chars[argc] + g_matrix[argc][argc] + g_struct.member_items[argc] + g_zero.n;
}
