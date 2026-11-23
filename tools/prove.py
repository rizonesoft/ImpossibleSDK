#!/usr/bin/env python3
"""Prove a repository on Windows, from WSL: python3 tools/prove.py <repository folder>

Builds the committed work in WSL, then Windows' own Python runs the tests against
those same programs where they are, on WSL's disk, as one call from WSL. A test
that needs Windows' file system makes its folder with tempfile, which on Windows
is a Windows disk.
"""
import argparse
import os
import subprocess
import sys
from pathlib import Path

SDK = Path(__file__).resolve().parents[1]


def git(folder, *arguments):
    done = subprocess.run(["git", "-C", str(folder), *arguments], capture_output=True, text=True, timeout=300)
    if done.returncode:
        raise SystemExit(f"error: git {arguments[0]} in {folder}: {done.stderr.strip()}")
    return done.stdout.strip()


def committed(folder):
    if git(folder, "status", "--porcelain"):
        raise SystemExit(f"error: {folder} has uncommitted changes, and a proof is of a commit")
    return git(folder, "rev-parse", "--short", "HEAD")


def there(path):
    """The Windows spelling of a path."""
    return subprocess.run(["wslpath", "-w", str(path)], capture_output=True, text=True, check=True, timeout=30).stdout.strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("folder", type=Path)
    folder = parser.parse_args().folder.resolve()
    commit = committed(folder)
    builder = [str(SDK / "tools/build.py")] if folder == SDK else [str(SDK / "tools/app_build.py"), str(folder)]
    subprocess.run([sys.executable, *builder, "--no-test"], check=True, timeout=1800)
    env = {**os.environ, "IMPOSSIBLE_BUILD": there(folder / "build/linux"), "IMPOSSIBLE_SDK_DIR": there(SDK),
           "PYTHONDONTWRITEBYTECODE": "1", "WSLENV": "IMPOSSIBLE_BUILD:IMPOSSIBLE_SDK_DIR:PYTHONDONTWRITEBYTECODE"}
    done = subprocess.run(["python.exe", "-B", "-m", "unittest", "discover", "-s", there(folder / "tests"), "-v"],
                          cwd="/mnt/c/Windows/Temp", env=env, timeout=3600, stdin=subprocess.DEVNULL)
    if committed(folder) != commit:
        raise SystemExit("error: the repository moved on during the proof")
    print(f"{'proved' if done.returncode == 0 else 'NOT proved'} on Windows: {folder.name} {commit}")
    return done.returncode


if __name__ == "__main__":
    try:
        sys.exit(main())
    except subprocess.CalledProcessError as error:
        sys.exit(error.returncode)
