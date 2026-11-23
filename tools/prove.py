#!/usr/bin/env python3
"""Prove a repository on Windows, from WSL: python3 tools/prove.py <repository folder>

The repositories are worked on in WSL. Each has a copy on a Windows drive, its
git remote "proof", which only this script changes. It sends the committed work
of the repository and of Impossible SDK to their copies, then builds and tests
the copy with Windows' own Python, as one call from WSL.
"""
import argparse
import subprocess
import sys
from pathlib import Path

SDK = Path(__file__).resolve().parents[1]


def git(folder, *arguments):
    done = subprocess.run(["git", "-C", str(folder), *arguments], capture_output=True, text=True, timeout=300)
    if done.returncode:
        raise SystemExit(f"error: git {arguments[0]} in {folder}: {done.stderr.strip()}")
    return done.stdout.strip()


def send(folder):
    """Brings the copy to this repository's commit and says where it is."""
    if git(folder, "status", "--porcelain"):
        raise SystemExit(f"error: {folder} has uncommitted changes, and a proof is of a commit")
    copy = Path(git(folder, "remote", "get-url", "proof"))
    git(folder, "push", "-q", "proof", "HEAD:main")
    if git(copy, "rev-parse", "HEAD") != git(folder, "rev-parse", "HEAD") or git(copy, "status", "--porcelain"):
        raise SystemExit(f"error: {copy} is not a clean copy of {folder}: someone edited it")
    return copy


def command(copy, sdk_copy):
    """The Windows command, as one argument each."""
    def there(path):
        return subprocess.run(["wslpath", "-w", str(path)], capture_output=True, text=True, check=True, timeout=30).stdout.strip()

    if copy == sdk_copy:
        return ["python.exe", there(copy / "tools/build.py")]
    return ["python.exe", there(sdk_copy / "tools/app_build.py"), there(copy)]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("folder", type=Path)
    args = parser.parse_args()
    folder = args.folder.resolve()
    sdk_copy = send(SDK)
    copy = sdk_copy if folder == SDK else send(folder)
    done = subprocess.run(command(copy, sdk_copy), cwd=copy, timeout=3600, stdin=subprocess.DEVNULL)
    print(f"{'proved' if done.returncode == 0 else 'NOT proved'} on Windows: {folder.name} {git(folder, 'rev-parse', '--short', 'HEAD')}")
    return done.returncode


if __name__ == "__main__":
    sys.exit(main())
