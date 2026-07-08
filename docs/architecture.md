# BorrowFence Architecture

BorrowFence is intentionally a library, not a C++ dialect. The first version focuses on a narrow safety boundary that can be implemented and tested in normal C++20.

## Runtime Borrow State

`BorrowCell<T>` owns a value and a small borrow state. A call to `borrow()` increments the immutable borrow count. A call to `borrow_mut()` succeeds only when no immutable or mutable borrow exists. `Ref<T>` and `Mut<T>` release the borrow state in their destructors.

This mirrors the shape of Rust's `RefCell<T>`, not Rust's compile-time borrow checker. Violations are reported as `borrow_error` at runtime.

## Ownership

`Box<T>` owns a heap-allocated `BorrowCell<T>`. Moving a `Box<T>` keeps active borrows valid because the cell remains on the heap. Destroying the cell while borrows are active is a lifetime violation and terminates the process; continuing would leave guards pointing at destroyed state.

`not_null<T*>` and `observer<T>` do not own memory. They exist to make pointer intent visible in APIs.

## Leak Tracking

`Box<T>` registers a small allocation token with a process-local tracker. `LeakScope` snapshots the currently active tracked owners and can later report new tracked owners that are still alive.

`tracked_new` and `tracked_new_array` register raw pointers in the same tracker. Their matching delete helpers unregister the pointer before releasing the allocation. This gives code with unavoidable manual allocation a way to make leaks visible to `LeakScope`.

This only covers BorrowFence-managed owners and opt-in tracked manual allocations. It is a debugging aid, not a replacement for LeakSanitizer or ownership review.

## Current Limits

- Borrow checking is runtime-only.
- BorrowFence does not analyze ordinary raw pointers or standard containers.
- The borrow state is synchronized for bookkeeping, but BorrowFence does not make arbitrary `T` operations data-race safe.
- Leak tracking reports live BorrowFence owners and opt-in tracked manual allocations, not every heap allocation in the process.
- Type names use the compiler's `typeid(T).name()` output and are not demangled.
