#!/usr/bin/env python3
"""Draw every case with real WinUI, from WSL: python3 reference/run.py

.NET does not build on WSL's disk, so the sources are copied to Windows' own
temporary folder and the frames come back to build/reference/out.
"""
import shutil
import subprocess
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUT = HERE.parent / "build/reference/out"


def windows(*command, **more):
    return subprocess.run(command, check=True, timeout=1200, stdin=subprocess.DEVNULL, **more)


temp = windows("pwsh.exe", "-NoProfile", "-Command", "[IO.Path]::GetTempPath()", capture_output=True, text=True).stdout.strip()
temp = windows("wslpath", "-u", temp, capture_output=True, text=True).stdout.strip()
with tempfile.TemporaryDirectory(dir=temp, prefix="impossible-reference-") as folder:
    work = Path(folder)
    shutil.copytree(HERE, work / "reference", ignore=shutil.ignore_patterns("*.py"))
    windows("dotnet.exe", "build", "-c", "Release", "-v", "q", "--nologo", cwd=work / "reference")
    program = next((work / "build/reference/bin").rglob("Reference.exe"))
    frames = windows("wslpath", "-w", str(work / "out"), capture_output=True, text=True).stdout.strip()
    try:
        windows(str(program), frames, cwd=work)
    except subprocess.CalledProcessError:
        err = work / "out" / "error.txt"
        if err.is_file():
            import sys
            sys.stderr.write(err.read_text(encoding="utf-8", errors="replace") + "\n")
        raise
    shutil.rmtree(OUT, ignore_errors=True)
    shutil.copytree(work / "out", OUT)
print(f"{len(list(OUT.glob('*.bgra')))} frames in {OUT}")
