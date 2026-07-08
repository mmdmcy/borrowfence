#include <borrowfence/borrowfence.hpp>

#include <iostream>

int main() {
  borrowfence::BorrowCell<int> value(10);
  auto read = value.borrow();

  try {
    auto write = value.borrow_mut();
    (void)write;
  } catch (const borrowfence::borrow_error& error) {
    std::cout << error.what() << '\n';
  }
}
