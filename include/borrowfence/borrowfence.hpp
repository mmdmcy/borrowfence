#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <mutex>
#include <source_location>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace borrowfence {

class borrow_error final : public std::logic_error {
public:
  explicit borrow_error(const std::string& message) : std::logic_error(message) {}
};

class lifetime_error final : public std::logic_error {
public:
  explicit lifetime_error(const std::string& message) : std::logic_error(message) {}
};

class leak_error final : public std::runtime_error {
public:
  explicit leak_error(const std::string& message) : std::runtime_error(message) {}
};

namespace detail {

struct adopt_borrow_t {};
inline constexpr adopt_borrow_t adopt_borrow {};

struct box_tag {};

class borrow_state {
public:
  borrow_state() = default;
  borrow_state(const borrow_state&) = delete;
  borrow_state& operator=(const borrow_state&) = delete;

  void acquire_shared() {
    std::lock_guard lock(mutex_);
    if (mutable_active_) {
      throw borrow_error("BorrowFence: cannot create immutable borrow while mutable borrow is active");
    }
    ++shared_count_;
  }

  void release_shared() noexcept {
    std::lock_guard lock(mutex_);
    if (shared_count_ > 0) {
      --shared_count_;
    }
  }

  void acquire_mutable() {
    std::lock_guard lock(mutex_);
    if (mutable_active_ || shared_count_ != 0) {
      throw borrow_error("BorrowFence: cannot create mutable borrow while another borrow is active");
    }
    mutable_active_ = true;
  }

  void release_mutable() noexcept {
    std::lock_guard lock(mutex_);
    mutable_active_ = false;
  }

  [[nodiscard]] bool has_active_borrows() const noexcept {
    std::lock_guard lock(mutex_);
    return mutable_active_ || shared_count_ != 0;
  }

private:
  mutable std::mutex mutex_;
  std::size_t shared_count_ = 0;
  bool mutable_active_ = false;
};

[[noreturn]] inline void terminate_active_borrow(const char* owner_name) noexcept {
  std::fputs("BorrowFence lifetime violation: destroying ", stderr);
  std::fputs(owner_name, stderr);
  std::fputs(" while borrows are active\n", stderr);
  std::terminate();
}

struct allocation_record {
  std::uint64_t id = 0;
  std::string type;
  std::string file;
  std::string function;
  std::uint_least32_t line = 0;
  std::uint_least32_t column = 0;
};

class allocation_tracker {
public:
  static allocation_tracker& instance() noexcept {
    static auto* tracker = new allocation_tracker();
    return *tracker;
  }

  std::uint64_t register_allocation(std::string type, std::source_location location) {
    std::lock_guard lock(mutex_);
    const auto id = next_id_++;
    active_.emplace(id, allocation_record {
                            id,
                            std::move(type),
                            location.file_name(),
                            location.function_name(),
                            location.line(),
                            location.column(),
                        });
    return id;
  }

  std::uint64_t register_pointer(void* pointer, std::string type, std::source_location location) {
    if (pointer == nullptr) {
      throw lifetime_error("BorrowFence: cannot track a null allocation pointer");
    }

    std::lock_guard lock(mutex_);
    if (pointer_ids_.contains(pointer)) {
      throw lifetime_error("BorrowFence: allocation pointer is already tracked");
    }

    const auto id = next_id_++;
    active_.emplace(id, allocation_record {
                            id,
                            std::move(type),
                            location.file_name(),
                            location.function_name(),
                            location.line(),
                            location.column(),
                        });
    pointer_ids_.emplace(pointer, id);
    return id;
  }

  void unregister_allocation(std::uint64_t id) noexcept {
    std::lock_guard lock(mutex_);
    active_.erase(id);
  }

  void unregister_pointer(void* pointer) {
    if (pointer == nullptr) {
      return;
    }

    std::lock_guard lock(mutex_);
    const auto pointer_entry = pointer_ids_.find(pointer);
    if (pointer_entry == pointer_ids_.end()) {
      throw lifetime_error("BorrowFence: tracked_delete received an untracked pointer");
    }

    active_.erase(pointer_entry->second);
    pointer_ids_.erase(pointer_entry);
  }

  std::unordered_set<std::uint64_t> active_ids() const {
    std::lock_guard lock(mutex_);
    std::unordered_set<std::uint64_t> ids;
    ids.reserve(active_.size());
    for (const auto& entry : active_) {
      ids.insert(entry.first);
    }
    return ids;
  }

  std::vector<allocation_record> active_except(const std::unordered_set<std::uint64_t>& baseline) const {
    std::lock_guard lock(mutex_);
    std::vector<allocation_record> records;
    for (const auto& entry : active_) {
      if (!baseline.contains(entry.first)) {
        records.push_back(entry.second);
      }
    }
    return records;
  }

private:
  allocation_tracker() = default;

  mutable std::mutex mutex_;
  std::uint64_t next_id_ = 1;
  std::unordered_map<std::uint64_t, allocation_record> active_;
  std::unordered_map<void*, std::uint64_t> pointer_ids_;
};

class allocation_token {
public:
  allocation_token() = default;

  allocation_token(std::string type, std::source_location location)
      : id_(allocation_tracker::instance().register_allocation(std::move(type), location)) {}

  allocation_token(const allocation_token&) = delete;
  allocation_token& operator=(const allocation_token&) = delete;

  allocation_token(allocation_token&& other) noexcept : id_(std::exchange(other.id_, 0)) {}

  allocation_token& operator=(allocation_token&& other) noexcept {
    if (this != &other) {
      reset();
      id_ = std::exchange(other.id_, 0);
    }
    return *this;
  }

  ~allocation_token() {
    reset();
  }

  void reset() noexcept {
    if (id_ != 0) {
      allocation_tracker::instance().unregister_allocation(id_);
      id_ = 0;
    }
  }

private:
  std::uint64_t id_ = 0;
};

template <class T>
std::string type_name() {
  return typeid(T).name();
}

} // namespace detail

template <class T>
class Ref {
public:
  Ref() = default;

  Ref(const Ref& other) : ptr_(other.ptr_), state_(other.state_) {
    if (state_ != nullptr) {
      state_->acquire_shared();
    }
  }

  Ref& operator=(const Ref& other) {
    if (this != &other) {
      reset();
      ptr_ = other.ptr_;
      state_ = other.state_;
      if (state_ != nullptr) {
        state_->acquire_shared();
      }
    }
    return *this;
  }

  Ref(Ref&& other) noexcept : ptr_(std::exchange(other.ptr_, nullptr)), state_(std::exchange(other.state_, nullptr)) {}

  Ref& operator=(Ref&& other) noexcept {
    if (this != &other) {
      reset();
      ptr_ = std::exchange(other.ptr_, nullptr);
      state_ = std::exchange(other.state_, nullptr);
    }
    return *this;
  }

  ~Ref() {
    reset();
  }

  [[nodiscard]] T& get() const {
    if (ptr_ == nullptr) {
      throw lifetime_error("BorrowFence: attempted to use an empty immutable borrow");
    }
    return *ptr_;
  }

  [[nodiscard]] T& operator*() const {
    return get();
  }

  [[nodiscard]] T* operator->() const {
    return &get();
  }

  explicit operator bool() const noexcept {
    return ptr_ != nullptr;
  }

  void reset() noexcept {
    if (state_ != nullptr) {
      state_->release_shared();
      state_ = nullptr;
      ptr_ = nullptr;
    }
  }

private:
  template <class>
  friend class BorrowCell;
  template <class>
  friend class Box;

  Ref(T* ptr, detail::borrow_state* state, detail::adopt_borrow_t) noexcept : ptr_(ptr), state_(state) {}

  T* ptr_ = nullptr;
  detail::borrow_state* state_ = nullptr;
};

template <class T>
class Mut {
public:
  Mut() = default;
  Mut(const Mut&) = delete;
  Mut& operator=(const Mut&) = delete;

  Mut(Mut&& other) noexcept : ptr_(std::exchange(other.ptr_, nullptr)), state_(std::exchange(other.state_, nullptr)) {}

  Mut& operator=(Mut&& other) noexcept {
    if (this != &other) {
      reset();
      ptr_ = std::exchange(other.ptr_, nullptr);
      state_ = std::exchange(other.state_, nullptr);
    }
    return *this;
  }

  ~Mut() {
    reset();
  }

  [[nodiscard]] T& get() const {
    if (ptr_ == nullptr) {
      throw lifetime_error("BorrowFence: attempted to use an empty mutable borrow");
    }
    return *ptr_;
  }

  [[nodiscard]] T& operator*() const {
    return get();
  }

  [[nodiscard]] T* operator->() const {
    return &get();
  }

  explicit operator bool() const noexcept {
    return ptr_ != nullptr;
  }

  void reset() noexcept {
    if (state_ != nullptr) {
      state_->release_mutable();
      state_ = nullptr;
      ptr_ = nullptr;
    }
  }

private:
  template <class>
  friend class BorrowCell;
  template <class>
  friend class Box;

  Mut(T* ptr, detail::borrow_state* state, detail::adopt_borrow_t) noexcept : ptr_(ptr), state_(state) {}

  T* ptr_ = nullptr;
  detail::borrow_state* state_ = nullptr;
};

template <class T>
class BorrowCell {
public:
  template <class... Args>
    requires std::constructible_from<T, Args...>
  explicit BorrowCell(Args&&... args) : value_(std::forward<Args>(args)...) {}

  BorrowCell(const BorrowCell&) = delete;
  BorrowCell& operator=(const BorrowCell&) = delete;
  BorrowCell(BorrowCell&&) = delete;
  BorrowCell& operator=(BorrowCell&&) = delete;

  ~BorrowCell() noexcept {
    if (state_.has_active_borrows()) {
      detail::terminate_active_borrow("BorrowCell");
    }
  }

  [[nodiscard]] Ref<T> borrow() {
    state_.acquire_shared();
    return Ref<T>(&value_, &state_, detail::adopt_borrow);
  }

  [[nodiscard]] Ref<const T> borrow() const {
    state_.acquire_shared();
    return Ref<const T>(&value_, &state_, detail::adopt_borrow);
  }

  [[nodiscard]] Mut<T> borrow_mut() {
    state_.acquire_mutable();
    return Mut<T>(&value_, &state_, detail::adopt_borrow);
  }

private:
  mutable detail::borrow_state state_;
  T value_;
};

template <class Pointer>
  requires std::is_pointer_v<Pointer>
class not_null {
public:
  explicit not_null(Pointer pointer) : pointer_(pointer) {
    if (pointer_ == nullptr) {
      throw lifetime_error("BorrowFence: not_null cannot hold nullptr");
    }
  }

  [[nodiscard]] Pointer get() const noexcept {
    return pointer_;
  }

  [[nodiscard]] decltype(auto) operator*() const {
    return *pointer_;
  }

  [[nodiscard]] Pointer operator->() const noexcept {
    return pointer_;
  }

  operator Pointer() const noexcept {
    return pointer_;
  }

private:
  Pointer pointer_;
};

template <class T>
class observer {
public:
  observer() = default;
  observer(std::nullptr_t) noexcept : ptr_(nullptr) {}
  explicit observer(T* ptr) noexcept : ptr_(ptr) {}
  explicit observer(T& value) noexcept : ptr_(&value) {}

  [[nodiscard]] T* get() const noexcept {
    return ptr_;
  }

  [[nodiscard]] T& value() const {
    if (ptr_ == nullptr) {
      throw lifetime_error("BorrowFence: attempted to dereference an empty observer");
    }
    return *ptr_;
  }

  [[nodiscard]] T& operator*() const {
    return value();
  }

  [[nodiscard]] T* operator->() const {
    return &value();
  }

  explicit operator bool() const noexcept {
    return ptr_ != nullptr;
  }

private:
  T* ptr_ = nullptr;
};

template <class T>
class Box {
public:
  Box(const Box&) = delete;
  Box& operator=(const Box&) = delete;
  Box(Box&&) noexcept = default;
  Box& operator=(Box&&) noexcept = default;
  ~Box() = default;

  [[nodiscard]] bool has_value() const noexcept {
    return cell_ != nullptr;
  }

  [[nodiscard]] Ref<T> borrow() {
    return cell().borrow();
  }

  [[nodiscard]] Ref<const T> borrow() const {
    return cell().borrow();
  }

  [[nodiscard]] Mut<T> borrow_mut() {
    return cell().borrow_mut();
  }

private:
  template <class U, class... Args>
  friend Box<U> make_box_at(std::source_location location, Args&&... args);

  template <class... Args>
    requires std::constructible_from<T, Args...>
  explicit Box(detail::box_tag, std::source_location location, Args&&... args)
      : cell_(std::make_unique<BorrowCell<T>>(std::forward<Args>(args)...)),
        token_(std::string("borrowfence::Box<") + detail::type_name<T>() + ">", location) {}

  [[nodiscard]] BorrowCell<T>& cell() {
    if (cell_ == nullptr) {
      throw lifetime_error("BorrowFence: attempted to use a moved-from Box");
    }
    return *cell_;
  }

  [[nodiscard]] const BorrowCell<T>& cell() const {
    if (cell_ == nullptr) {
      throw lifetime_error("BorrowFence: attempted to use a moved-from Box");
    }
    return *cell_;
  }

  std::unique_ptr<BorrowCell<T>> cell_;
  detail::allocation_token token_;
};

template <class T, class... Args>
[[nodiscard]] Box<T> make_box_at(std::source_location location, Args&&... args) {
  return Box<T>(detail::box_tag {}, location, std::forward<Args>(args)...);
}

template <class T, class... Args>
[[nodiscard]] Box<T> make_box(Args&&... args) {
  return make_box_at<T>(std::source_location::current(), std::forward<Args>(args)...);
}

template <class T, class... Args>
[[nodiscard]] T* tracked_new_at(std::source_location location, Args&&... args) {
  auto* pointer = new T(std::forward<Args>(args)...);
  try {
    detail::allocation_tracker::instance().register_pointer(
        pointer,
        std::string("borrowfence::tracked_new<") + detail::type_name<T>() + ">",
        location);
  } catch (...) {
    delete pointer;
    throw;
  }
  return pointer;
}

template <class T, class... Args>
[[nodiscard]] T* tracked_new(Args&&... args) {
  return tracked_new_at<T>(std::source_location::current(), std::forward<Args>(args)...);
}

template <class T>
void tracked_delete(T* pointer) {
  detail::allocation_tracker::instance().unregister_pointer(pointer);
  delete pointer;
}

template <class T>
[[nodiscard]] T* tracked_new_array_at(std::size_t count, std::source_location location) {
  auto* pointer = new T[count] {};
  try {
    detail::allocation_tracker::instance().register_pointer(
        pointer,
        std::string("borrowfence::tracked_new_array<") + detail::type_name<T>() + ">",
        location);
  } catch (...) {
    delete[] pointer;
    throw;
  }
  return pointer;
}

template <class T>
[[nodiscard]] T* tracked_new_array(std::size_t count) {
  return tracked_new_array_at<T>(count, std::source_location::current());
}

template <class T>
void tracked_delete_array(T* pointer) {
  detail::allocation_tracker::instance().unregister_pointer(pointer);
  delete[] pointer;
}

class LeakScope {
public:
  LeakScope() : baseline_(detail::allocation_tracker::instance().active_ids()) {}

  [[nodiscard]] std::vector<detail::allocation_record> leaks() const {
    return detail::allocation_tracker::instance().active_except(baseline_);
  }

  void expect_clean() const {
    const auto records = leaks();
    if (records.empty()) {
      return;
    }

    std::ostringstream message;
    message << "BorrowFence: detected " << records.size() << " tracked allocation";
    if (records.size() != 1) {
      message << "s";
    }
    message << " still alive";

    for (const auto& record : records) {
      message << "\n  #" << record.id << " " << record.type << " allocated at " << record.file << ":" << record.line;
      if (record.column != 0) {
        message << ":" << record.column;
      }
      message << " in " << record.function;
    }

    throw leak_error(message.str());
  }

private:
  std::unordered_set<std::uint64_t> baseline_;
};

} // namespace borrowfence

#define BORROWFENCE_BOX(Type, ...) \
  ::borrowfence::make_box_at<Type>(std::source_location::current() __VA_OPT__(, ) __VA_ARGS__)

#define BORROWFENCE_NEW(Type, ...) \
  ::borrowfence::tracked_new_at<Type>(std::source_location::current() __VA_OPT__(, ) __VA_ARGS__)

#define BORROWFENCE_NEW_ARRAY(Type, Count) \
  ::borrowfence::tracked_new_array_at<Type>((Count), std::source_location::current())
