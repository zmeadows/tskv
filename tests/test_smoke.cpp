#include <cassert>
#include <iostream>

int main() {
#if defined(__clang__)
  std::cout << "compiler=Clang version=" << __clang_major__ << "."
            << __clang_minor__ << "\n";
#else
  std::cout << "compiler=Other\n";
#endif
  std::cout << "cxx_std=" << __cplusplus << "\n";
  assert(true);
  return 0;
}
