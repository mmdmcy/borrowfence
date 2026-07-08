#include <borrowfence/borrowfence.hpp>

#include <cassert>
#include <string>
#include <type_traits>

namespace {

template <class Exception, class Fn>
void expect_throws(Fn&& fn) {
  bool threw = false;
  try {
    fn();
  } catch (const Exception&) {
    threw = true;
  }
  assert(threw);
}

void multiple_immutable_borrows_are_allowed() {
  borrowfence::BorrowCell<int> cell(7);

  auto first = cell.borrow();
  auto second = cell.borrow();
  auto third = first;

  assert(*first == 7);
  assert(*second == 7);
  assert(*third == 7);
}

void mutable_borrow_can_change_value() {
  borrowfence::BorrowCell<std::string> cell("safe");

  {
    auto value = cell.borrow_mut();
    value->append(" C++");
  }

  auto value = cell.borrow();
  assert(*value == "safe C++");
}

void immutable_borrow_blocks_mutable_borrow() {
  borrowfence::BorrowCell<int> cell(3);
  auto read = cell.borrow();

  expect_throws<borrowfence::borrow_error>([&] {
    auto write = cell.borrow_mut();
    (void)write;
  });
}

void mutable_borrow_blocks_other_borrows() {
  borrowfence::BorrowCell<int> cell(3);
  auto write = cell.borrow_mut();

  expect_throws<borrowfence::borrow_error>([&] {
    auto read = cell.borrow();
    (void)read;
  });

  expect_throws<borrowfence::borrow_error>([&] {
    auto another_write = cell.borrow_mut();
    (void)another_write;
  });
}

void box_can_move_while_borrow_points_to_heap_cell() {
  auto box = BORROWFENCE_BOX(std::string, "hello");
  auto read = box.borrow();
  auto moved = std::move(box);

  assert(*read == "hello");
  assert(moved.has_value());
  assert(!box.has_value());
  read.reset();
}

void moved_from_box_rejects_use() {
  auto box = BORROWFENCE_BOX(int, 42);
  auto moved = std::move(box);
  (void)moved;

  expect_throws<borrowfence::lifetime_error>([&] {
    auto read = box.borrow();
    (void)read;
  });
}

void not_null_rejects_null() {
  int value = 9;
  borrowfence::not_null<int*> pointer(&value);
  assert(*pointer == 9);

  expect_throws<borrowfence::lifetime_error>([] {
    borrowfence::not_null<int*> pointer(nullptr);
    (void)pointer;
  });
}

void observer_makes_non_ownership_explicit() {
  int value = 11;
  borrowfence::observer<int> seen(value);
  assert(seen);
  assert(*seen == 11);

  borrowfence::observer<int> empty;
  assert(!empty);
  expect_throws<borrowfence::lifetime_error>([&] {
    (void)*empty;
  });
}

void leak_scope_passes_when_box_is_destroyed() {
  borrowfence::LeakScope scope;
  {
    auto box = BORROWFENCE_BOX(int, 5);
    auto read = box.borrow();
    assert(*read == 5);
  }
  scope.expect_clean();
}

void leak_scope_reports_live_tracked_box() {
  borrowfence::LeakScope scope;
  auto* leaked = new borrowfence::Box<int>(BORROWFENCE_BOX(int, 5));

  expect_throws<borrowfence::leak_error>([&] {
    scope.expect_clean();
  });

  delete leaked;
  scope.expect_clean();
}

static_assert(!std::is_copy_constructible_v<borrowfence::Mut<int>>);
static_assert(!std::is_copy_constructible_v<borrowfence::Box<int>>);
static_assert(std::is_copy_constructible_v<borrowfence::Ref<int>>);

} // namespace

int main() {
  multiple_immutable_borrows_are_allowed();
  mutable_borrow_can_change_value();
  immutable_borrow_blocks_mutable_borrow();
  mutable_borrow_blocks_other_borrows();
  box_can_move_while_borrow_points_to_heap_cell();
  moved_from_box_rejects_use();
  not_null_rejects_null();
  observer_makes_non_ownership_explicit();
  leak_scope_passes_when_box_is_destroyed();
  leak_scope_reports_live_tracked_box();
}
