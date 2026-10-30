__attribute__((noinline)) int edge(int a) { return a * 7 + 1; }

int main(int argc, char **argv) { return edge(argc); }
