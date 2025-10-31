#include <cstdio>
#include <cstring>

static int print_help() {
  std::puts("tskv server — usage:\n"
            "  server [--help]\n"
            "\n"
            "Starts the tskv server (Step 0 stub).");
  return 0;
}

int main(int argc, char **argv) {
  if (argc > 1 && (std::strcmp(argv[1], "--help") == 0 ||
                   std::strcmp(argv[1], "-h") == 0)) {
    return print_help();
  }
  // Default behavior for Step 0: just print help and exit success.
  return print_help();
}
