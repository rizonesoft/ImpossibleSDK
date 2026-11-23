#!/usr/bin/env python3
"""Build the SDK and its samples with the pinned toolchain, then run the tests.

The same command works on Windows and in WSL; output goes to build/windows or
build/linux. Both build Windows programs.
"""
import argparse
import os
import subprocess
import sys
from pathlib import Path

sys.dont_write_bytecode = True
import bootstrap

ROOT = Path(__file__).resolve().parents[1]


def build(source, out, test=True, definitions=None):
    """Configure, build and test one source folder with the pinned toolchain."""
    subprocess.run([sys.executable, str(ROOT / "tools/bootstrap.py")], check=True)
    part = {p["name"]: p for p in bootstrap.components()}
    exe = {name: str(bootstrap.home(p) / p["probe"]) for name, p in part.items()}
    subprocess.run([exe["cmake"], "-S", str(source), "-B", str(out), "-G", "Ninja",
                    f"-DCMAKE_MAKE_PROGRAM={exe['ninja']}",
                    f"-DCMAKE_TOOLCHAIN_FILE={ROOT / 'cmake/toolchain.cmake'}",
                    f"-DIMPOSSIBLE_LLVM={bootstrap.home(part['llvm-mingw'])}",
                    f"-DPython3_EXECUTABLE={sys.executable}",
                    "-DCMAKE_BUILD_TYPE=Release",
                    *(f"-D{name}={value}" for name, value in (definitions or {}).items())], check=True)
    subprocess.run([exe["cmake"], "--build", str(out)], check=True)
    if test:
        subprocess.run([sys.executable, "-B", "-m", "unittest", "discover", "-s", str(source / "tests"), "-v"],
                       check=True, env={**os.environ, "IMPOSSIBLE_BUILD": str(out), "IMPOSSIBLE_SDK_DIR": str(ROOT)})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--no-test", action="store_true")
    parser.add_argument("--out", type=Path, help="build folder (default build/<host>)")
    args = parser.parse_args()
    build(ROOT, (args.out or ROOT / "build" / bootstrap.host()).resolve(), test=not args.no_test)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as error:
        raise SystemExit(error.returncode)
