#!/usr/bin/env python3
"""Build and test a program that lives in its own repository.

    python <sdk>/tools/app_build.py <program folder> [--no-test] [--any-sdk]

The program's sdk.json names the SDK commit it is built with, and this refuses
an SDK at another commit or with local changes. --any-sdk is for working on
both at once; it never satisfies a release.
"""
import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

sys.dont_write_bytecode = True
import bootstrap
from build import build

SDK = Path(__file__).resolve().parents[1]


def sdk_problem(program, sdk=SDK):
    """Why this SDK is not the one the program pins, or None."""
    try:
        wanted = json.loads((program / "sdk.json").read_text(encoding="utf-8"))["commit"]
    except (OSError, ValueError, KeyError, TypeError) as error:
        return f"{program / 'sdk.json'} does not name a commit ({type(error).__name__})"
    if not isinstance(wanted, str) or not re.fullmatch(r"[0-9a-f]{40}", wanted):
        return f"{program / 'sdk.json'}: the commit is not a full hash"

    def git(*arguments):
        return subprocess.run(["git", "-C", str(sdk), *arguments], capture_output=True, text=True, check=True).stdout.strip()

    try:
        head = git("rev-parse", "HEAD")
        dirty = git("status", "--porcelain")
    except (OSError, subprocess.CalledProcessError):
        return f"{sdk} is not a git checkout, so its commit is unknown"
    if head != wanted:
        return f"the program pins Impossible SDK {wanted[:12]} and {sdk} is at {head[:12]}"
    if dirty:
        return f"{sdk} has local changes"
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("program", type=Path)
    parser.add_argument("--no-test", action="store_true")
    parser.add_argument("--any-sdk", action="store_true")
    parser.add_argument("--out", type=Path, help="build folder (default <program>/build/<host>)")
    args = parser.parse_args()
    program = args.program.resolve()
    problem = sdk_problem(program)
    if problem:
        if not args.any_sdk:
            raise SystemExit(f"error: {problem} (--any-sdk builds anyway)")
        print(f"warning: {problem}", file=sys.stderr)
    build(program, (args.out or program / "build" / bootstrap.host()).resolve(), test=not args.no_test,
          definitions={"IMPOSSIBLE_SDK_DIR": SDK})


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as error:
        raise SystemExit(error.returncode)
