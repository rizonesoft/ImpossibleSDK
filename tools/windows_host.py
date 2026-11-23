#!/usr/bin/env python3
"""Exit 0 when Windows can run this repository's proofs from here: its Python reads a file of the repository,
PowerShell starts, and .NET can build the reference (tools/prove.py and reference/run.py use all three).
todo/plan.json probes with it, and the plan's tool stops a probe after ten seconds."""
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FILE = ROOT / "toolchain.json"
SECONDS = 8  # for everything together


def main():
    until = time.monotonic() + SECONDS

    def start(*command):
        return subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)

    def answer(process):
        out, _ = process.communicate(timeout=max(0.1, until - time.monotonic()))
        if process.returncode:
            raise OSError(f"{process.args[0]} failed")
        return out

    started = []
    try:
        there = answer(start("wslpath", "-w", str(FILE))).strip()
        read = "import sys; sys.stdout.write(str(len(open(sys.argv[1], 'rb').read())))"
        for command in (("python.exe", "-c", read, there), ("pwsh.exe", "-NoProfile", "-Command", "exit 0"), ("dotnet.exe", "--list-sdks")):
            started.append(start(*command))
        size, _, sdks = map(answer, started)
    except (OSError, subprocess.SubprocessError):
        return 1
    finally:
        for process in started:
            if process.poll() is None:
                process.kill()
                process.wait()
    wanted = int(re.search(r"<TargetFramework>net(\d+)", (ROOT / "reference/Reference.csproj").read_text(encoding="utf-8"))[1])
    newest = max((int(major) for major in re.findall(r"^(\d+)\.", sdks, re.MULTILINE)), default=0)
    return 0 if size.strip() == str(FILE.stat().st_size) and newest >= wanted else 1


if __name__ == "__main__":
    sys.exit(main())
