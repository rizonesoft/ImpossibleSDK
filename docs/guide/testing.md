<!-- page: group="Guide" order=4 sources=tools/prove.py,tools/build.py,tools/check_imports.py,tests/test_seed.py,tests/test_ui.py reviewed=2026-11-24 -->
# Testing on Windows

How do you prove a program works? Build it in WSL or on Windows, and let Windows itself run the tests against the same files.

## What does the build check?

| Check | When |
| --- | --- |
| The program's imports fit its tier | Every program, after linking |
| Warnings are errors | Every file |
| `ui/` builds without floating point or system calls | With the SDK's tests |
| Generated WinUI theme and control template tables match the pinned commit | With the SDK's tests |
| Buttons match real WinUI within [stated limits](button.md), pixel by pixel | With the SDK's tests |
| Reference store (`tests/reference/`) digests, layout/automation dumps, scripts, and `reference/package.json` match | With the SDK's tests |

## How do you run the tests?

```sh
python tools/build.py                  # the SDK
python tools/app_build.py ../Hello     # a program in its own repository
python3 reference/run.py               # fresh WinUI captures in build/reference/out
python3 reference/run.py --update-store # refresh tests/reference/ atomically
```

Today the 16 100-percent button cases and 10 corner-radius probes have a matching SDK pixel test (`control-shots.exe`); multi-scale, RTL, icon, and scripted captures are verified in the store and ready for the element-tree renderer.

## What is a proof?

In WSL, `prove.py` builds the committed work, then Windows' own Python runs the tests against those programs on WSL's disk. It refuses uncommitted changes, because a proof is of a commit.

```sh
python3 tools/prove.py .
```

## How do you check one program's imports?

```sh
python tools/check_imports.py --tier 1 build/windows/hello-window.exe
```

It reads the finished file as Windows' loader does. Besides functions outside the tier, it refuses imports by ordinal, delay-loaded DLLs, bound imports, thread-local storage, and a DLL that is loaded for nothing.
