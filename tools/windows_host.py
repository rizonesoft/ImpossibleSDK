#!/usr/bin/env python3
"""Exit 0 when Windows can run this repository's proofs from here: its Python reads a file of the repository,
and PowerShell and .NET start (tools/prove.py and reference/run.py use all three). todo/plan.json probes with it."""
import subprocess
import sys
from pathlib import Path

FILE = Path(__file__).resolve().parents[1] / "toolchain.json"


def runs(*command):
    return subprocess.run(command, capture_output=True, text=True, check=True, timeout=8, stdin=subprocess.DEVNULL).stdout


def main():
    try:
        there = runs("wslpath", "-w", str(FILE)).strip()
        read = runs("python.exe", "-c", "import sys; sys.stdout.write(str(len(open(sys.argv[1], 'rb').read())))", there)
        runs("pwsh.exe", "-NoProfile", "-Command", "exit 0")
        runs("dotnet.exe", "--version")
    except (OSError, subprocess.SubprocessError):
        return 1
    return 0 if read.strip() == str(FILE.stat().st_size) else 1


if __name__ == "__main__":
    sys.exit(main())
