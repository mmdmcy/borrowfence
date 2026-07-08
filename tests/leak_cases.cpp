#include <borrowfence/borrowfence.hpp>

#include <cassert>
#include <string>

namespace {

template <class Exception, class Fn>
Exception expect_throws(Fn&& fn) {
  try {
    fn();
  } catch (const Exception& error) {
    return error;
  }
  assert(false && "expected exception was not thrown");
}

void tracked_new_delete_is_clean() {
  borrowfence::LeakScope scope;

  auto* value = BORROWFENCE_NEW(std::string, "clean");
  assert(*value == "clean");
  borrowfence::tracked_delete(value);

  scope.expect_clean();
}

void tracked_new_leak_is_reported() {
  borrowfence::LeakScope scope;

  auto* leaked = BORROWFENCE_NEW(int, 42);
  assert(*leaked == 42);

  assert(scope.leaks().size() == 1);
  const auto error = expect_throws<borrowfence::leak_error>([&] {
    scope.expect_clean();
  });
  assert(std::string(error.what()).find("tracked_new") != std::string::npos);

  borrowfence::tracked_delete(leaked);
  scope.expect_clean();
}

void multiple_tracked_leaks_are_reported() {
  borrowfence::LeakScope scope;

  auto* first = BORROWFENCE_NEW(int, 1);
  auto* second = BORROWFENCE_NEW(std::string, "two");

  assert(scope.leaks().size() == 2);
  const auto error = expect_throws<borrowfence::leak_error>([&] {
    scope.expect_clean();
  });
  assert(std::string(error.what()).find("detected 2 tracked allocations") != std::string::npos);

  borrowfence::tracked_delete(first);
  borrowfence::tracked_delete(second);
  scope.expect_clean();
}

void leak_scope_ignores_allocations_from_its_baseline() {
  auto* existing = BORROWFENCE_NEW(int, 7);
  borrowfence::LeakScope scope;
  auto* newer = BORROWFENCE_NEW(int, 8);

  const auto leaks = scope.leaks();
  assert(leaks.size() == 1);
  assert(leaks.front().type.find("tracked_new") != std::string::npos);

  borrowfence::tracked_delete(newer);
  scope.expect_clean();
  borrowfence::tracked_delete(existing);
}

void tracked_array_leak_is_reported() {
  borrowfence::LeakScope scope;

  auto* values = BORROWFENCE_NEW_ARRAY(int, 4);
  values[0] = 99;

  assert(scope.leaks().size() == 1);
  const auto error = expect_throws<borrowfence::leak_error>([&] {
    scope.expect_clean();
  });
  assert(std::string(error.what()).find("tracked_new_array") != std::string::npos);

  borrowfence::tracked_delete_array(values);
  scope.expect_clean();
}

void tracked_delete_null_is_noop() {
  borrowfence::LeakScope scope;
  borrowfence::tracked_delete<int>(nullptr);
  borrowfence::tracked_delete_array<int>(nullptr);
  scope.expect_clean();
}

void tracked_delete_rejects_untracked_pointer() {
  borrowfence::LeakScope scope;
  auto* raw = new int(5);

  const auto error = expect_throws<borrowfence::lifetime_error>([&] {
    borrowfence::tracked_delete(raw);
  });
  assert(std::string(error.what()).find("untracked pointer") != std::string::npos);

  delete raw;
  scope.expect_clean();
}

void leaked_heap_box_is_reported() {
  borrowfence::LeakScope scope;

  auto* leaked = new borrowfence::Box<int>(BORROWFENCE_BOX(int, 5));
  assert(scope.leaks().size() == 1);

  const auto error = expect_throws<borrowfence::leak_error>([&] {
    scope.expect_clean();
  });
  assert(std::string(error.what()).find("Box") != std::string::npos);

  delete leaked;
  scope.expect_clean();
}

void moved_box_does_not_create_false_leak() {
  borrowfence::LeakScope scope;

  {
    auto first = BORROWFENCE_BOX(std::string, "move");
    auto second = std::move(first);
    assert(second.has_value());
  }

  scope.expect_clean();
}

} // namespace

int main() {
  tracked_new_delete_is_clean();
  tracked_new_leak_is_reported();
  multiple_tracked_leaks_are_reported();
  leak_scope_ignores_allocations_from_its_baseline();
  tracked_array_leak_is_reported();
  tracked_delete_null_is_noop();
  tracked_delete_rejects_untracked_pointer();
  leaked_heap_box_is_reported();
  moved_box_does_not_create_false_leak();
}
