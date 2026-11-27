---
schema_version: 1
id: cxx-programs
domain: 09-platform
status: active
title: "TODO-01 -- C++ Programs"
---

# TODO-01 -- C++ Programs

> **Goal:** A program written in C++23 builds with the SDK toolchain, links a `cxx` part that carries LLVM libc++, libc++abi and libunwind statically, and runs on Windows with exceptions, RTTI, `thread_local`, static constructors and `atexit` working, importing no Microsoft runtime DLL.

> **Current state:** Measured 2026-11-27 at `8d5bf9a`. The SDK is C only: `cmake/ImpossibleSDK.cmake` compiles C23 with `-ffreestanding`, `src/start.c` calls `im_main` and then `ExitProcess` and runs no constructor or destructor, `parts.json` has no `cxx` part, and the public headers in `include/impossible/` are not compiled as C++. Impossible OS plans the same runtime for user mode (`impossible-os` `todo/11-apps/TODO-05-pdf-viewer.md` §11) and, under `docs/specs/2026-11-27-sdk-as-platform-design.md`, takes it from here instead of building its own.

## Inputs

- `docs/specs/2026-11-27-sdk-as-platform-design.md`: "What does the SDK add?" item 1, and the boundary rules in "How is the boundary checked?" that the `cxx` part is built under.
- `toolchain.json`: the only place a pinned version, URL or hash is written. `tools/bootstrap.py` installs it.
- `abi/tiers.json`: the functions the runtime may call; every one added is work for Impossible OS.
- -> XREF: D08 T01 §1 -- `parts.json` and `tools/check_parts.py` already list parts; `cxx` is one more entry, checked the same way
- -> XREF: D09 T02 §6 -- the link step that drops system import libraries; the `cxx` part must pass it
- -> XREF: D09 T05 §5 -- `docs/guide/cxx.md`, the supported subset, is written from this file's proof
- -> XREF: D09 T04 §6 -- the `cxx` part and this file's sample on arm64
- `impossible-os` `todo/11-apps/TODO-15-imported-programs.md` §22 -- imports this part into Impossible OS and keeps what only the OS can do (unwinding on its exception dispatch, the six `user/tests/cxx/` tests under its launcher); it builds no runtime of its own
- Consumers: Recto `todo/00-workspace/TODO-06-impossible-sdk-platform.md`, Office `todo/03-architecture/TODO-01-tech-stack.md` §2, Isotone `todo/00-workspace/TODO-08-sdk-platform-build.md`

## Outcome

- The C++ proof sample, built like an app, throws two frames up through a live object, catches it, uses `thread_local` from two threads and runs a static constructor and destructor, and passes `tools/check_imports.py`.
- Every public header in `include/impossible/` compiles as C++23 in CI, and no Microsoft runtime DLL is imported by a C++ program.

**Adjacency:** all=not-applicable (a language runtime holds no records, settings, reports or notices)

**Adjacency rationale:** The runtime is linked into programs; it keeps nothing of its own.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Public headers compile as C++23 | -- | [ ] |
| 2 | §2 | Pinned LLVM runtime sources | -- | [ ] |
| 3 | §3 | C library pieces the runtime needs | §2 | [ ] |
| 4 | §4 | Thread primitives for libc++ | §2, §3 | [ ] |
| 5 | §5 | The `cxx` part and the LLVM runtimes | §3, §4 | [ ] |
| 6 | §6 | Exceptions, RTTI and emulated TLS | §5 | [ ] |
| 7 | §7 | Constructors, destructors and `atexit` | §5 | [ ] |
| 8 | §8 | The C++ program build in CMake | §1, §5, §7 | [ ] |
| 9 | §9 | C++ proof sample | §6, §7, §8 | [ ] |

## 1. Public headers compile as C++23

Every header a program includes must be usable from C++: `extern "C"` around declarations, and no construct that only C accepts.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `tests/fixtures/header_cxx.cpp` with one include per exported header. Done when: it compiles with `-std=c++23 -Wall -Wextra -Werror` in a test target.
2. Fix each header the compile names, in `include/impossible/*.h`. Done when: the same headers still compile as C23 in the existing targets.

- [ ] Add `tests/fixtures/header_cxx.cpp` that includes every header under `include/impossible/`. Done when: the file is compiled by `tests/test_parts.py` as C++23 with warnings as errors and the test lists each header it covered. Cheaper substitute: including only `base.h`.
- [ ] Wrap every declaration in `include/impossible/*.h` in `extern "C"` under `#ifdef __cplusplus`. Done when: `llvm-nm` on a C++ object that calls `im_alloc` shows the unmangled name. Cheaper substitute: wrapping in each program.
- [ ] Remove C-only constructs from the headers (compound literals, designated initialisers out of order, implicit `void *` conversions, `bool` and `static_assert` spellings). Done when: the C++ compile passes and every C target still passes. Cheaper substitute: `-fpermissive` flags.
- [ ] Add each directory a part exports (`ui/`, `parts/*/`) to the same compile, in `tests/test_parts.py`. Done when: each exported header compiles as C++23 and a header added without the guard fails the test. Cheaper substitute: only `include/impossible/`.
- [ ] Commit: `include: public headers compile as C++23`

**Test checkpoint:** `python3 tools/build.py` exits 0 and `tests/test_parts.py` fails when the `extern "C"` guard is removed from `include/impossible/base.h`.

## 2. Pinned LLVM runtime sources

The runtime is built here from pinned source, never a prebuilt archive, so that the boundary rule "every object is built from source" holds for it too.

**Phase:** 7
**Needs:** none
**Surface:** no

**Build order:**

1. Add the `llvm-runtimes` pin to `toolchain.json`. Done when: `tools/bootstrap.py` downloads the source archive and verifies its hash.
2. Record the licence notices in `licenses/`. Done when: each notice names its component and version.

- [ ] Pin the LLVM release that matches llvm-mingw 20260922 in `toolchain.json` (`libcxx`, `libcxxabi`, `libunwind`, `runtimes/cmake`). Done when: `tools/bootstrap.py` verifies the URL and sha256 and refuses a changed archive. Cheaper substitute: a floating branch.
- [ ] Extract only the three runtime folders to `build/` in `tools/bootstrap.py`. Done when: a second run finds the verified folders and downloads nothing. Cheaper substitute: committing the sources.
- [ ] Add the LLVM Apache-2.0 with LLVM exceptions notice for each component to `licenses/`. Done when: `tests/test_brand.py` or its licence check lists them. Cheaper substitute: one shared notice.
- [ ] Commit: `toolchain: pin the LLVM runtime sources`

**Test checkpoint:** `python3 tools/bootstrap.py --check` exits 0 and fails when the pinned hash in `toolchain.json` is changed.

## 3. C library pieces the runtime needs

libc++ and libc++abi call memory, string, math and allocation routines that no C runtime supplies here; the SDK's base supplies each one, once.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. List the unresolved symbols of the runtime build with `llvm-nm -u`. Done when: the list is checked in as `parts/cxx/needs.txt`.
2. Implement each in `parts/cxx/libc/`. Done when: the runtime links with nothing undefined.

- [ ] Record the undefined symbols of the libc++, libc++abi and libunwind objects in `parts/cxx/needs.txt` by `tools/check_parts.py`. Done when: the file matches the build and a new undefined symbol fails the check. Cheaper substitute: a hand list.
- [ ] Map `malloc`, `free`, `realloc`, `calloc` and aligned allocation onto `im_alloc` and `im_free` in `parts/cxx/libc/alloc.c`. Done when: 1,000,000 random allocations with alignments 8 to 4096 never overlap or leak. Cheaper substitute: `HeapAlloc` calls in the part.
- [ ] Implement the string and memory routines the list names in `parts/cxx/libc/string.c`. Done when: a differential test against the host C library on 10,000 random inputs agrees. Cheaper substitute: pulling the C library in.
- [ ] Implement the few math and conversion routines the list names in `parts/cxx/libc/math.c` without the C runtime. Done when: results agree with the host on 10,000 random inputs, within the stated ULP. Cheaper substitute: linking `libm`.
- [ ] Commit: `cxx: the C library pieces the runtime needs`

**Test checkpoint:** `python3 tools/build.py` exits 0 and `tests/test_parts.py` fails when `parts/cxx/libc/alloc.c` returns a misaligned block.

## 4. Thread primitives for libc++

libc++'s external threading API (`_LIBCPP_HAS_THREAD_API_EXTERNAL`) asks for mutexes, condition variables, threads, once and thread-local keys; this section supplies them over listed Windows functions and no Windows header.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `parts/cxx/threading.c` and `parts/cxx/__external_threading` with the first item. Done when: the first item below passes.
2. Cover it in `tests/test_parts.py` and a native build under a thread checker. Done when: the checks fail on a planted fault.

- [ ] Add the functions needed (condition variables, one-time initialisation, thread-local keys, thread exit) to tier 0 in `abi/tiers.json` with what Impossible OS must give. Done when: `tools/check_imports.py --tier 0` accepts a program using them and a lower tier does not exist. Cheaper substitute: a new tier.
- [ ] Implement mutex, recursive mutex, condition variable and once in `parts/cxx/threading.c` over those functions. Done when: eight threads doing 100,000 lock and notify rounds give the counted total with no lost wake-up. Cheaper substitute: spin loops.
- [ ] Implement thread create, join, detach, yield and sleep in `parts/cxx/threading.c`. Done when: 1,000 create and join cycles leave no handle and a detached thread's result is not read after exit. Cheaper substitute: one thread.
- [ ] Declare the API in `parts/cxx/__external_threading` and include no Windows header. Done when: `tools/check_parts.py` finds no `<windows.h>` in the part and `std::mutex`, `std::thread` and `std::condition_variable` compile. Cheaper substitute: libc++'s Win32 backend.
- [ ] Run the primitives under a thread checker in `tests/test_parts.py`. Done when: a native build with thread checking finds nothing in 60 seconds. Cheaper substitute: one run.
- [ ] Commit: `cxx: thread primitives for libc++`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the thread test fails when `parts/cxx/threading.c` releases a mutex it does not hold.

## 5. The `cxx` part and the LLVM runtimes

One part, built from the pinned source with the SDK toolchain, static, with exceptions and RTTI on and no Microsoft runtime.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add the `cxx` entry to `parts.json` and the `ImpossibleCxx` target to `cmake/ImpossibleSDK.cmake`. Done when: `tools/check_parts.py` accepts it.
2. Build it from the pinned source. Done when: the archive has no undefined symbol outside `parts/cxx/needs.txt`.

- [ ] List `cxx` in `parts.json` with `depends` on the base and its licence notices. Done when: `tools/check_parts.py` refuses an undeclared include in it and `tools/copy_part.py` (when it exists, `D08 T01 §1`) copies it with its needs. Cheaper substitute: a build-only target.
- [ ] Build libc++, libc++abi and libunwind in `cmake/ImpossibleSDK.cmake` as the static `ImpossibleCxx` target with exceptions and RTTI on, no filesystem backend and no locale data beyond the C locale. Done when: the archive builds from a clean tree and its size is recorded. Cheaper substitute: a prebuilt archive from llvm-mingw.
- [ ] Compile it with `-nostdlib` and no `kernel32` UCRT shim in `cmake/ImpossibleSDK.cmake`. Done when: a program using `std::string`, `std::vector` and `std::map` imports no `ucrtbase.dll`, `msvcrt.dll` or `vcruntime*.dll`. Cheaper substitute: allowing the C runtime DLL.
- [ ] Make the part's headers the C++ standard library's own interface, in `parts/cxx/include/`. Done when: they include no Windows header, checked by rule 4 in `D09 T02 §9`. Cheaper substitute: letting a header include `<windows.h>`.
- [ ] Commit: `cxx: libc++, libc++abi and libunwind as a part`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the `cxx` target fails to link when `parts/cxx/threading.c` is removed.

## 6. Exceptions, RTTI and emulated TLS

Exceptions unwind with libunwind on the SDK's own tables, `dynamic_cast` and `typeid` work, and `thread_local` needs no TLS directory in the executable.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add the unwind table registration to `parts/cxx/unwind.c`. Done when: a throw reaches its catch two frames up.
2. Add emulated thread-local storage to `parts/cxx/tls.c`. Done when: `check_imports.py` shows no TLS directory.

- [ ] Register `.pdata` and `.xdata` for libunwind in `parts/cxx/unwind.c` without a runtime DLL. Done when: a throw through three frames with live destructors runs each destructor once, in order. Cheaper substitute: disabling exceptions.
- [ ] Build with `-femulated-tls` and implement `__emutls_get_address` over the thread-local keys of §4 in `parts/cxx/tls.c`. Done when: two threads see their own copies of a `thread_local` and a main-thread value does not leak into them. Cheaper substitute: a shared global.
- [ ] Free each thread's emulated storage at thread exit in `parts/cxx/tls.c`. Done when: 1,000 threads leave no growth in `im_alloc` use. Cheaper substitute: leaking it.
- [ ] Refuse a TLS directory in the executable in `tools/check_imports.py`. Done when: a program built without `-femulated-tls` and using `thread_local` is rejected with a named reason. Cheaper substitute: a comment.
- [ ] Prove `dynamic_cast`, `typeid` and exceptions across a part boundary in `tests/fixtures/cxx_rtti.cpp`. Done when: the test passes with the part linked statically into two parts. Cheaper substitute: one translation unit.
- [ ] Commit: `cxx: exceptions, RTTI and emulated thread-local storage`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the unwinding test fails when `parts/cxx/unwind.c` stops registering the tables.

## 7. Constructors, destructors and `atexit`

`src/start.c` calls `im_main` and then `ExitProcess`; a C++ program also needs static constructors before `im_main` and destructors and `atexit` handlers after it.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Collect the constructor and destructor tables in `src/start.c` through linker-provided section bounds. Done when: a static object's constructor prints before `im_main`.
2. Run `atexit` handlers and static destructors in reverse order before `ExitProcess`. Done when: the order is the standard's.

- [ ] Run the `.init_array` and `.CRT$XCU` tables before `im_main` in `src/start.c`. Done when: 100 static objects construct in link order and a C program with none behaves as before. Cheaper substitute: a call in each program.
- [ ] Implement `atexit` and `__cxa_atexit` in `parts/cxx/atexit.c`. Done when: 64 handlers registered across two translation units run in reverse registration order, once. Cheaper substitute: a fixed array of four.
- [ ] Run destructors and handlers after `im_main` returns, before `ExitProcess`, in `src/start.c`. Done when: a destructor that writes a file leaves the file after exit. Cheaper substitute: relying on process exit.
- [ ] Run the same exit path when a program calls `std::exit` or `std::terminate` in `parts/cxx/atexit.c`. Done when: `terminate` ends with a non-zero code and does not run destructors of live frames. Cheaper substitute: `ExitProcess` directly.
- [ ] Keep C programs unchanged in `src/start.c`. Done when: `hello-console` and `hello-window` import the same functions as before this section. Cheaper substitute: a second entry point for C.
- [ ] Commit: `start: constructors, destructors and atexit`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the ordering test fails when `src/start.c` runs the destructors before `im_main`.

## 8. The C++ program build in CMake

`impossible_add_program` builds a C++ program like a C one, with the shipping build on the SDK toolchain.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Enable C++ in `cmake/ImpossibleSDK.cmake` and `cmake/toolchain.cmake`. Done when: a `.cpp` source compiles with `-std=c++23`.
2. Add a `CXX` option and link the part. Done when: a C++ program links `ImpossibleCxx`.

- [ ] Set the C++ compiler in `cmake/toolchain.cmake` to the pinned clang. Done when: `CMAKE_CXX_COMPILER` names it and no other compiler is accepted for a shipping target. Cheaper substitute: the system compiler.
- [ ] Add the `CXX` keyword to `impossible_add_program` in `cmake/ImpossibleSDK.cmake` with `-std=c++23 -fexceptions -frtti -femulated-tls -nostdinc++`. Done when: a C++ program builds and links `ImpossibleCxx` first. Cheaper substitute: flags in each program.
- [ ] Keep `check_imports.py` as the last step for C++ programs in `cmake/ImpossibleSDK.cmake`. Done when: a C++ program above its tier fails the build. Cheaper substitute: skipping it for C++.
- [ ] Allow an app to keep an MSVC build for its own tools in the guide text of `docs/guide/own-program.md`, and refuse to ship it. Done when: the page says the MSVC build does not ship and `tools/app_build.py` packages only the SDK build. Cheaper substitute: silence.
- [ ] Commit: `cmake: C++ programs`

**Test checkpoint:** `python3 tools/build.py` exits 0 and a C++ target fails the build when it is given a tier lower than its imports.

## 9. C++ proof sample

One sample shows that everything an app needs from C++ works, built like an app.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `samples/cxx/main.cpp` and `samples/cxx/module.cppm`. Done when: it builds through `impossible_add_program(... CXX ...)`.
2. Run it on Windows. Done when: it prints its checks and exits 0.

- [ ] Import a C++ module in `samples/cxx/main.cpp` and use a function it exports. Done when: the sample builds with the pinned clang module flags and the check prints the module's value. Cheaper substitute: a header include.
- [ ] Throw two frames up through a live object whose destructor runs, and catch it, in `samples/cxx/main.cpp`. Done when: the destructor's mark is seen before the catch handler runs. Cheaper substitute: a throw in one frame.
- [ ] Use `thread_local` from two threads in `samples/cxx/main.cpp`. Done when: each thread sees its own value. Cheaper substitute: one thread.
- [ ] Run a static constructor and a static destructor in `samples/cxx/main.cpp`. Done when: the constructor ran before `im_main` and the destructor wrote its mark after it. Cheaper substitute: a function-local static.
- [ ] Run it under the link rule and the import check in `tests/test_apps.py`. Done when: the executable passes `tools/check_imports.py` at the sample's tier and imports no Microsoft runtime DLL. Cheaper substitute: checking the build only.
- [ ] Commit: `samples: the C++ proof sample`

**Test checkpoint:** `python3 tools/build.py` exits 0 and the sample's test fails when the catch is removed or `-femulated-tls` is dropped.

## Verification

- The C++ proof sample, built like an app, passes every check on Windows and `tools/check_imports.py`.
- `python3 scripts/todo-graph.py validate` and `python3 scripts/todo-graph.py plan --check` pass.
