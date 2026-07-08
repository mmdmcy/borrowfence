#include <borrowfence/borrowfence.hpp>

int main() {
  auto* cell = new borrowfence::BorrowCell<int>(12);
  auto read = cell->borrow();
  (void)read;
  delete cell;
}
