#include <sanitizer/lsan_interface.h>

void leak_one_small_raw_allocation() {
  auto* values = new int[4] {1, 2, 3, 4};
  values[0] = 9;
  values = nullptr;
}

int main() {
  leak_one_small_raw_allocation();
  __lsan_do_leak_check();
}
