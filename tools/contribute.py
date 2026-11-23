#!/usr/bin/env python3
"""Add to Impossible SDK from a program's run, while other runs work on it too.

    python3 ../ImpossibleSDK/tools/contribute.py start            prints a folder
    python3 ../ImpossibleSDK/tools/contribute.py finish <folder>  from the program's folder

`start` gives a folder of your own with the SDK's newest main: make, commit and
have Codex review the change there, and never in the SDK's own folder, where its
run may be working. `finish` brings it onto the newest main, builds and tests it,
pushes it, pins the program in this folder to it and removes the folder. One
finish at a time; what does not pass or does not fit is not pushed.
"""
import argparse
import fcntl
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

SDK = Path(os.environ.get("IMPOSSIBLE_SDK_REPO", Path(__file__).resolve().parents[1]))
WORK = Path(os.environ.get("IMPOSSIBLE_TOOLS", Path.home() / ".cache/impossible-sdk")) / "work"


def git(folder, *arguments):
    clean = {k: v for k, v in os.environ.items() if not k.startswith("GIT_")}
    done = subprocess.run(["git", "-C", str(folder), *arguments], capture_output=True, text=True, timeout=600, env=clean)
    if done.returncode:
        raise SystemExit(f"contribute: git {arguments[0]}: {(done.stderr or done.stdout).strip()}")
    return done.stdout.strip()


def start():
    git(SDK, "fetch", "-q", "origin", "main")
    WORK.mkdir(parents=True, exist_ok=True)
    folder = Path(tempfile.mkdtemp(dir=WORK, prefix="sdk-"))
    git(SDK, "worktree", "add", "-q", "--detach", str(folder), "origin/main")
    print(folder)


def finish(folder, program, test):
    pin = program / "sdk.json"
    if not pin.is_file():
        raise SystemExit(f"contribute: {program} is not a program's folder (it has no sdk.json)")
    if folder.parent != WORK.resolve() or not (folder / ".git").is_file():
        raise SystemExit(f"contribute: {folder} is not a folder that start made")
    if git(folder, "status", "--porcelain"):
        raise SystemExit(f"contribute: {folder} has changes that are not committed")
    with open(WORK / "lock", "w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        git(SDK, "fetch", "-q", "origin", "main")
        if not git(folder, "log", "--format=%h", "origin/main..HEAD"):
            raise SystemExit("contribute: there is nothing to add")
        git(folder, "rebase", "-q", "origin/main")
        subprocess.run([*test], cwd=folder, check=True, timeout=1800)
        if git(folder, "status", "--porcelain"):
            raise SystemExit(f"contribute: the build changed files in {folder}: commit them first")
        git(folder, "push", "-q", "origin", "HEAD:main")
        commit = git(folder, "rev-parse", "HEAD")
        git(SDK, "fetch", "-q", "origin", "main")
    wanted = json.loads(pin.read_text(encoding="utf-8"))
    wanted["commit"] = commit
    pin.write_text(json.dumps(wanted, indent=2) + "\n", encoding="utf-8")
    git(SDK, "worktree", "remove", str(folder))
    print(f"Impossible SDK {commit[:12]} is pushed and {program.name} is pinned to it")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("step", choices=("start", "finish"))
    parser.add_argument("folder", nargs="?", type=Path)
    args = parser.parse_args()
    if (args.step == "finish") != (args.folder is not None):
        parser.error("finish takes the folder, start takes none")
    if args.step == "start":
        start()
    else:
        finish(args.folder.resolve(), Path.cwd(), [sys.executable, "tools/build.py"])


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        raise SystemExit(f"contribute: the build or a test failed ({error.returncode}); nothing was pushed")
