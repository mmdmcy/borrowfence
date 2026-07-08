#include <borrowfence/borrowfence.hpp>

#include <iostream>

int main() {
  borrowfence::LeakScope leaks;

  auto* leaked = new borrowfence::Box<int>(BORROWFENCE_BOX(int, 42));

  try {
    leaks.expect_clean();
  } catch (const borrowfence::leak_error& error) {
    std::cout << error.what() << '\n';
  }

  delete leaked;
}
