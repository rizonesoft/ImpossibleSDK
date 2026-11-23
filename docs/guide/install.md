<!-- page: group="Start here" order=1 sources=tools/bootstrap.py,tools/build.py,toolchain.json reviewed=2026-11-24 -->
# Install

What do you need? Windows or WSL on 64-bit x86, Python 3.12 or newer, and Git. Nothing else is installed system-wide.

## How do you get it?

In WSL the command is `python3`.

```sh
git clone https://github.com/rizonesoft/ImpossibleSDK.git
cd ImpossibleSDK
python tools/bootstrap.py    # installs the pinned toolchain, once per machine
python tools/build.py        # builds into build/<host> and runs the tests
```

`bootstrap.py` downloads the compiler, CMake and Ninja named in the [toolchain](site:toolchain.html), checks each archive's SHA-256, and keeps one copy for every program on the machine: in `%LOCALAPPDATA%\ImpossibleSDK` on Windows, in `~/.cache/impossible-sdk` in WSL and on Linux. Set `IMPOSSIBLE_TOOLS` to keep it somewhere else.

## How do you know it worked?

The last line of the tests is `OK`, and these two programs run:

| Program | What it does |
| --- | --- |
| `hello-console.exe` | Prints one line, with an accent and an emoji |
| `hello-window.exe` | Opens a window with light under the pointer |

They are in `build/windows`, or in `build/linux` when you built in WSL. Both folders hold Windows programs.

## Does it work on plain Linux?

The programs build. The tests that start them are skipped, because plain Linux cannot run a Windows program.

## Why does WSL sometimes not start a Windows program?

`UtilAcceptVsock ... accept4 failed` means the Linux kernel found no unbroken 512 KB of memory for the connection. `dmesg` then shows `page allocation failure: order:7`. Set `vm.min_free_kbytes = 262144` and `vm.compaction_proactiveness = 60` in `/etc/sysctl.d/`. The tests report it and do not hide it.
