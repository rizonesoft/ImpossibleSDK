#!/usr/bin/env python3
"""Build and test a program that lives in its own repository.

    python <sdk>/tools/app_build.py <program folder> [--no-test] [--any-sdk]

The program's sdk.json names the SDK commit it is built with. When this checkout
is at another commit or has local changes, that commit is copied out of its
history once and used instead, so a program and the SDK can move separately.
--any-sdk builds with this checkout as it is, for working on both at once.
"""
import argparse
import io
import json
import os
import re
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

sys.dont_write_bytecode = True
import bootstrap
from build import build

SDK = Path(__file__).resolve().parents[1]


def pinned(program):
    try:
        wanted = json.loads((program / "sdk.json").read_text(encoding="utf-8"))["commit"]
    except (OSError, ValueError, KeyError, TypeError) as error:
        raise SystemExit(f"error: {program / 'sdk.json'} does not name a commit ({type(error).__name__})") from None
    if not isinstance(wanted, str) or not re.fullmatch(r"[0-9a-f]{40}", wanted):
        raise SystemExit(f"error: {program / 'sdk.json'}: the commit is not a full hash")
    return wanted


def sdk_at(commit, sdk=SDK, cache=None):
    """A folder that holds the SDK at that commit: this checkout, or a copy from its history."""
    def git(*arguments, **more):
        return subprocess.run(["git", "-C", str(sdk), *arguments], capture_output=True, check=True, **more).stdout

    try:
        if git("rev-parse", "HEAD").decode().strip() == commit and not git("status", "--porcelain").strip():
            return sdk
        archive = git("archive", "--format=tar", commit)
    except (OSError, subprocess.CalledProcessError):
        raise SystemExit(f"error: {sdk} does not have the pinned commit {commit}: fetch it first") from None
    target = (cache or bootstrap.tools_root()) / f"sdk-{commit}"
    if not target.is_dir():
        target.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=target.parent) as temp:
            with tarfile.open(fileobj=io.BytesIO(archive)) as bundle:
                bundle.extractall(Path(temp, "sdk"), filter="data")
            try:
                Path(temp, "sdk").rename(target)
            except OSError:
                if not target.is_dir():  # otherwise another build copied it first
                    raise
    return target


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("program", type=Path)
    parser.add_argument("--no-test", action="store_true")
    parser.add_argument("--any-sdk", action="store_true")
    parser.add_argument("--out", type=Path, help="build folder (default <program>/build/<host>)")
    args = parser.parse_args()
    program = args.program.resolve()
    out = (args.out or program / "build" / bootstrap.host()).resolve()
    sdk = SDK if args.any_sdk else sdk_at(pinned(program))
    if sdk != SDK:
        print(f"Impossible SDK {sdk.name}", flush=True)
        subprocess.run([sys.executable, str(sdk / "tools/app_build.py"), str(program), "--any-sdk", "--out", str(out),
                        *(["--no-test"] if args.no_test else [])], check=True)
        return
    build(program, out, test=not args.no_test, definitions={"IMPOSSIBLE_SDK_DIR": SDK})


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as error:
        raise SystemExit(error.returncode)
