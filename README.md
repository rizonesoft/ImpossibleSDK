# Impossible SDK

The small C kit that Impossible OS side programs are built on. A program is an ordinary 64-bit Windows executable with no C runtime, paints its own pixels, and calls only the Windows functions on one published list. Impossible OS is to implement that list, so that the same program runs on both; today it runs on Windows only.

Status: seed. Start-up, memory, console output, one window, the import check and the build exist. Drawing, text, controls and accessibility are planned in `D:\ImpossibleOS\plan.md`.

## Build

The same commands work in Windows and in WSL. The host is 64-bit x86 with Python 3.12 or newer.

```
python tools/bootstrap.py    # installs the pinned toolchain once per machine
python tools/build.py        # builds into build/<host> and runs the tests
```

In WSL the command is `python3`. Both hosts produce the same bytes. On plain Linux the programs build but the tests that run them are skipped.

## Which Windows functions may a program call?

Those in [`abi/tiers.json`](abi/tiers.json), up to the program's tier: 44 functions for a console program, 78 for a windowed one. `tools/check_imports.py` reads every finished program and fails the build on anything else, on imports by ordinal, delay-loaded DLLs and thread-local storage.

## Use it in a program

```cmake
project(MyProgram LANGUAGES C)
include("sdk/cmake/ImpossibleSDK.cmake")
impossible_add_program(MyProgram WINDOW TIER 1 SOURCES src/main.c)
```

A program defines `im_main()`. Configure it with `sdk/cmake/toolchain.cmake` as the toolchain file and `IMPOSSIBLE_LLVM` set to the llvm-mingw folder, as `tools/build.py` does. See [`samples/`](samples/).

## WSL cannot start a Windows program sometimes?

`UtilAcceptVsock ... accept4 failed` means the Linux kernel found no unbroken 512 KB of memory for the connection (`dmesg` shows `page allocation failure: order:7`). Set `vm.min_free_kbytes = 262144` and `vm.compaction_proactiveness = 60` in `/etc/sysctl.d/`. The tests report it and do not hide it.

## License

MIT. See [LICENSE](LICENSE).
