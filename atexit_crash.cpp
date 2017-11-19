extern "C" int printf(const char *, ...);
extern "C" int atexit(void (*)(void));

void run_at_exit() {}

int main() {
  printf("%p\n", &run_at_exit);
  atexit(run_at_exit);
  printf("hello, world\n");
  return 0;
}

