#include <borrowfence/borrowfence.hpp>

#include <iostream>
#include <string>

int main() {
  borrowfence::LeakScope leaks;

  {
    auto message = BORROWFENCE_BOX(std::string, "BorrowFence");

    {
      auto read = message.borrow();
      std::cout << *read << '\n';
    }

    {
      auto write = message.borrow_mut();
      write->append(" keeps C++ ownership visible");
    }

    std::cout << *message.borrow() << '\n';
  }

  leaks.expect_clean();
}
