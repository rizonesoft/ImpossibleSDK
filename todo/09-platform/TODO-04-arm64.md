---
schema_version: 1
id: arm64-target
domain: 09-platform
status: active
title: "TODO-04 -- Windows on arm64"
---

# TODO-04 -- Windows on arm64

> **Goal:** The SDK builds, checks and proves programs for Windows on arm64 as well as x64, so an app that ships arm64 today can keep shipping it on the SDK.

> **Current state:** Measured 2026-11-27 at `8d5bf9a`. `cmake/toolchain.cmake` sets `CMAKE_SYSTEM_PROCESSOR x86_64` and the `x86_64-w64-mingw32-clang` compiler, `src/chkstk.S` is x86-64 assembly, `cmake/ImpossibleSDK.cmake` passes `-mno-mmx -mno-sse -mno-sse2 -mno-80387` to the portable parts, and `tools/check_imports.py` refuses any machine but `0x8664`. `toolchain.json` pins llvm-mingw 20260922, which includes an aarch64 target, and there is no arm64 host in `tools/windows_host.py`. Recto ships arm64 today and keeps its own arm64 build until this file is done.

## Inputs

- `docs/specs/2026-11-27-sdk-as-platform-design.md`: "What does the SDK add?" item 4 and the order rule: arm64 is independent of the other platform work.
- `todo/README.md`: the `arm64-host` Need, met by the kit's own probe.
- -> XREF: D09 T02 §6 -- the private-name import library is generated per machine, so §4 here extends that tool
- -> XREF: D09 T01 §9 -- the C++ proof sample is the second sample proved on arm64

## Outcome

- A program builds for x64 or arm64 from one source tree, passes the import check with the right machine type, and runs on Windows arm64.
- A tier or an import list is never written twice: the machines share `abi/tiers.json`.

**Adjacency:** all=not-applicable (a build target holds no records, settings, reports or notices)

**Adjacency rationale:** The target changes how programs are built and checked; nothing is stored.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | arm64 compiler selection | -- | [ ] |
| 2 | §2 | arm64 start, stack probe and compile flags | §1 | [ ] |
| 3 | §3 | The arm64 machine type in the import check | -- | [ ] |
| 4 | §4 | arm64 private-name import library | §1, D09 T02 §6 | [ ] |
| 5 | §5 | A sample proved on Windows arm64 | §2, §3, §4 | [ ] |
| 6 | §6 | The `cxx` part and C++ sample on arm64 | §5, D09 T01 §9 | [ ] |

## 1. arm64 compiler selection

`cmake/toolchain.cmake` picks the x86-64 compiler; it must pick by the requested machine from the same pinned llvm-mingw.

**Phase:** 7
**Needs:** none
**Surface:** no

**Build order:**

1. Add an `IMPOSSIBLE_MACHINE` setting to `cmake/toolchain.cmake`. Done when: `x64` and `arm64` choose their compilers.
2. Verify the pinned archive carries the aarch64 tools in `toolchain.json`. Done when: `tools/bootstrap.py` checks their hashes.

- [ ] Add the aarch64 tool hashes (`aarch64-w64-mingw32-clang`, `-windres`) to the `runs` of llvm-mingw in `toolchain.json`. Done when: `tools/bootstrap.py` verifies them on both hosts. Cheaper substitute: trusting the archive.
- [ ] Set `CMAKE_SYSTEM_PROCESSOR` and the compilers from `IMPOSSIBLE_MACHINE` in `cmake/toolchain.cmake`. Done when: configuring with `arm64` names the aarch64 compiler and the default stays x64. Cheaper substitute: a second toolchain file.
- [ ] Make `tools/build.py` build both machines when asked, in `tools/build.py`. Done when: `python3 tools/build.py --machine arm64` configures and builds. Cheaper substitute: manual CMake.
- [ ] Commit: `cmake: choose the compiler by machine`

**Test checkpoint:** `python3 tools/build.py --machine arm64` configures, and `python3 tools/build.py` still builds x64 and exits 0.

## 2. arm64 start, stack probe and compile flags

The x64 start-up assembly and the x86 floating-point flags do not apply on arm64.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Port `src/chkstk.S` to `src/chkstk_arm64.S`. Done when: a large stack frame probes without a crash.
2. Choose flags by machine in `cmake/ImpossibleSDK.cmake`. Done when: the portable parts build with the arm64 equivalent of no floating point.

- [ ] Write `src/chkstk_arm64.S` and select by machine in `cmake/ImpossibleSDK.cmake`. Done when: a 1 MB stack frame in `tests/fixtures/selftest.c` runs on arm64. Cheaper substitute: a smaller frame limit.
- [ ] Replace `-mno-mmx -mno-sse -mno-sse2 -mno-80387` by `-mgeneral-regs-only` for arm64 in `cmake/ImpossibleSDK.cmake`. Done when: `ImpossibleParts` and `ImpossibleUI` build and the no-float check of `tests/test_parts.py` passes. Cheaper substitute: allowing floating point.
- [ ] Check `src/*.c` for x64-only code (inline assembly, `__builtin_ia32_*`) in `tests/test_seed.py`. Done when: the test names any use and none remains. Cheaper substitute: a one-time grep.
- [ ] Commit: `src: arm64 start-up and flags`

**Test checkpoint:** `python3 tools/build.py --machine arm64` exits 0 and the portable-part check fails when a floating-point instruction is planted.

## 3. The arm64 machine type in the import check

`tools/check_imports.py` refuses any machine but x64.

**Phase:** 7
**Needs:** none
**Surface:** no

**Build order:**

1. Accept `0xAA64` in `tools/check_imports.py` with its optional-header rules. Done when: an arm64 fixture passes.
2. Refuse a machine the program was not built for. Done when: an x64 file checked as arm64 fails.

- [ ] Accept machine `0xAA64` and the PE32+ magic in `tools/check_imports.py`. Done when: a committed arm64 fixture executable passes at its tier. Cheaper substitute: skipping the machine test.
- [ ] Add a `--machine` argument and refuse a mismatch in `tools/check_imports.py`. Done when: each of the four machine and file combinations gives the right verdict. Cheaper substitute: accepting either.
- [ ] Refuse any other machine, with its number, in `tools/check_imports.py`. Done when: an x86 fixture is rejected with a named reason. Cheaper substitute: a generic error.
- [ ] Cover the three cases in `tests/test_seed.py` with committed fixtures. Done when: each fails when the production check is reverted. Cheaper substitute: running it on the sample only.
- [ ] Commit: `tools: the arm64 machine type`

**Test checkpoint:** `python3 -m unittest tests.test_seed` exits 0 and fails when `0xAA64` is removed from `tools/check_imports.py`.

## 4. arm64 private-name import library

`D09 T02 §6` generates the private-name library for x64 with `llvm-dlltool`; arm64 needs the same library for its machine.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add `--machine` to `tools/gen_imports.py`. Done when: `llvm-dlltool -m arm64` builds the library.
2. Link arm64 programs against it. Done when: an arm64 program with no system import library links.

- [ ] Pass `-m arm64` to `llvm-dlltool` in `tools/gen_imports.py` and write the library per machine under `build/<machine>/`. Done when: both libraries build from the same `abi/tiers.json`. Cheaper substitute: a second tier list.
- [ ] Link by machine in `cmake/ImpossibleSDK.cmake`. Done when: an arm64 program links with no system import library and its import table names the real DLL functions. Cheaper substitute: system libraries on arm64.
- [ ] Prove the plain-name failure on arm64 in `tests/test_seed.py`. Done when: an arm64 object calling `ExitProcess` fails to link with an undefined symbol. Cheaper substitute: the x64 test.
- [ ] Commit: `tools: the arm64 private-name library`

**Test checkpoint:** `python3 tools/build.py --machine arm64` exits 0 and the plain-name object fails to link.

## 5. A sample proved on Windows arm64

The proof is a run on Windows arm64, not a build.

**Phase:** 7
**Needs:** arm64-host
**Surface:** no

**Build order:**

1. Build `hello-window` and `selftest` for arm64. Done when: both pass `tools/check_imports.py --machine arm64`.
2. Run them on a Windows arm64 host. Done when: `tools/windows_host.py` reports the machine and the runs pass.

- [ ] Report the host's machine from `tools/windows_host.py`. Done when: it prints `arm64` on a Windows arm64 host and `x64` elsewhere, and the kit's own `arm64-host` probe shown by `python3 scripts/todo-graph.py detect` agrees. Cheaper substitute: an environment variable.
- [ ] Run `selftest` for arm64 on the host in `tests/test_live.py`. Done when: it exits 0 on Windows arm64. Cheaper substitute: emulation on x64.
- [ ] Run `hello-window` for arm64 on the host and read a painted pixel in `tests/test_live.py`. Done when: the pixel equals the x64 result. Cheaper substitute: checking that it starts.
- [ ] Record the host's machine, Windows build and result in the test's output. Done when: the record names an arm64 machine. Cheaper substitute: none.
- [ ] Commit: `tests: a sample proved on Windows arm64`

**Test checkpoint:** `python3 tests/test_live.py` exits 0 on a Windows arm64 host and its arm64 case is skipped, never passed, on any other.

## 6. The `cxx` part and C++ sample on arm64

Recto ships C++ on arm64, so the `cxx` part and its proof sample must run there.

**Phase:** 7
**Needs:** arm64-host
**Surface:** no

**Build order:**

1. Build the `cxx` part for arm64. Done when: the archive has no undefined symbol outside `parts/cxx/needs.txt`.
2. Run the C++ sample on arm64. Done when: it passes every check.

- [ ] Build `ImpossibleCxx` for arm64 in `cmake/ImpossibleSDK.cmake` and take the unwinder's arm64 table format into `parts/cxx/unwind.c`. Done when: a throw reaches its catch two frames up on arm64. Cheaper substitute: x64 only.
- [ ] Check emulated thread-local storage on arm64 in `parts/cxx/tls.c`. Done when: two threads see their own values and no TLS directory is present. Cheaper substitute: a shared global.
- [ ] Run `samples/cxx` on a Windows arm64 host in `tests/test_live.py`. Done when: it passes its checks and `tools/check_imports.py --machine arm64`. Cheaper substitute: building it.
- [ ] Commit: `cxx: the part and sample on arm64`

**Test checkpoint:** `python3 tests/test_live.py` exits 0 on a Windows arm64 host and the C++ case fails when `parts/cxx/unwind.c` has no arm64 tables.

## Verification

- A sample and the C++ proof sample run on Windows arm64 and pass `tools/check_imports.py --machine arm64`.
- `python3 scripts/todo-graph.py validate` and `python3 scripts/todo-graph.py plan --check` pass.
