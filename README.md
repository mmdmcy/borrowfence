# BorrowFence

BorrowFence is a C++20 header-only safety toolkit for making ownership and borrowing rules visible in code. It gives C++ projects a small Rust-inspired layer:

- `BorrowCell<T>` enforces many-readers-or-one-writer borrow rules at runtime.
- `Box<T>` is a move-only tracked owner for heap values.
- `Ref<T>` and `Mut<T>` are RAII borrow guards.
- `not_null<T*>` and `observer<T>` make pointer intent explicit.
- `tracked_new<T>()` / `tracked_delete()` track manual allocations when raw pointers are unavoidable.
- `LeakScope` reports BorrowFence-managed owners that are still alive at a checkpoint.

BorrowFence is not a borrow checker, a compiler plugin, or a promise that C++ becomes memory-safe by including one header. It is meant to complement modern C++, C++ Core Guidelines-style code review, sanitizers, and static analysis.

## Quick Start

```cpp
#include <borrowfence/borrowfence.hpp>

#include <string>

int main() {
  borrowfence::LeakScope leaks;

  {
    auto name = BORROWFENCE_BOX(std::string, "C++");

    {
      auto read = name.borrow();
      // many immutable borrows can exist together
    }

    {
      auto write = name.borrow_mut();
      write->append(" with visible ownership");
    }
  }

  leaks.expect_clean();
}
```

## Build And Test

This repo starts with a Makefile because it only needs a C++20 compiler and `make`.

```sh
make test
make asan
make leak-tests
make examples
```

Override the compiler when needed:

```sh
make clean
make CXX=g++ test
```

## What It Catches

BorrowFence catches misuse inside its own API:

- mutable borrow while immutable borrows are active;
- immutable borrow while a mutable borrow is active;
- destroying a `BorrowCell` or `Box` value while borrows are active;
- dereferencing empty observer wrappers;
- tracked BorrowFence owners left alive at a `LeakScope` checkpoint;
- manual allocations made with `tracked_new`, `tracked_new_array`, `BORROWFENCE_NEW`, or `BORROWFENCE_NEW_ARRAY` that were not released with the matching tracked delete call.

It does not catch arbitrary plain raw `new` / `delete` unless you run with tools such as LeakSanitizer. `make leak-tests` includes a tiny intentional raw leak program to verify that sanitizer path. BorrowFence also does not catch pointer arithmetic, buffer overflows, use-after-free outside BorrowFence-managed values, data races in normal C++ objects, or reference cycles created with standard `shared_ptr`. Use AddressSanitizer, LeakSanitizer, UndefinedBehaviorSanitizer, compiler warnings, and static analysis alongside it.

## Related Work

- Rust ownership and lifetimes: https://doc.rust-lang.org/nomicon/ownership.html
- Rust reference cycles and leaks: https://doc.rust-lang.org/book/ch15-06-reference-cycles.html
- C++ Core Guidelines: https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines
- Microsoft GSL: https://github.com/microsoft/GSL
- Clang AddressSanitizer: https://clang.llvm.org/docs/AddressSanitizer.html
- LeakSanitizer: https://github.com/google/sanitizers/wiki/AddressSanitizerLeakSanitizer
- Safe C++ proposal context: https://safecpp.org/P3390R0.html

## License

MIT.
