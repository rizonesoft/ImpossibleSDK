#!/usr/bin/env python3
"""Add to Impossible SDK from a program's run, while other runs work on it too.

    python3 ../ImpossibleSDK/tools/contribute.py start            prints a folder
    python3 ../ImpossibleSDK/tools/contribute.py finish <folder>  from the program's folder

`start` gives a folder of your own with the SDK's newest main: make, commit and
have Codex review the change there, and never in the SDK's own folder, where its
run may be working. `finish` brings it onto the newest main, builds and tests that
very commit in a folder of its own, pushes it, pins the program in this folder to it and removes the folder. One
finish at a time; what does not pass or does not fit is not pushed.
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

SDK = Path(os.environ.get("IMPOSSIBLE_SDK_REPO", Path(__file__).resolve().parents[1]))
WORK = Path(os.environ.get("IMPOSSIBLE_TOOLS", Path.home() / ".cache/impossible-sdk")) / "work"


STARTED = "refs/worktree/started"  # git keeps it for this one folder


def git(folder, *arguments):
    clean = {k: v for k, v in os.environ.items() if not k.startswith("GIT_")}
    done = subprocess.run(["git", "-C", str(folder), *arguments], capture_output=True, text=True, timeout=600, env=clean)
    if done.returncode:
        raise SystemExit(f"contribute: git {arguments[0]}: {(done.stderr or done.stdout).strip()}")
    return done.stdout.strip()


def common(folder):
    return Path(folder, git(folder, "rev-parse", "--git-common-dir")).resolve()


def start():
    git(SDK, "fetch", "-q", "origin", "main")
    WORK.mkdir(parents=True, exist_ok=True)
    folder = Path(tempfile.mkdtemp(dir=WORK, prefix="sdk-"))
    git(SDK, "worktree", "add", "-q", "--detach", str(folder), "origin/main")
    git(folder, "update-ref", STARTED, "HEAD")
    print(folder)


def finish(folder, program, test):
    import fcntl  # only where this runs, so that --help answers on Windows too
    pin = program / "sdk.json"
    if not pin.is_file():
        raise SystemExit(f"contribute: {program} is not a program's folder (it has no sdk.json)")
    own = folder.parent == WORK.resolve() and (folder / ".git").is_file()
    if not own or common(folder) != common(SDK):
        raise SystemExit(f"contribute: {folder} is not a folder that start made")
    if git(folder, "status", "--porcelain"):
        raise SystemExit(f"contribute: {folder} has changes that are not committed")
    with open(common(SDK) / "contribute.lock", "w") as lock:  # one for this SDK, whoever asks
        fcntl.flock(lock, fcntl.LOCK_EX)
        git(SDK, "fetch", "-q", "origin", "main")
        begun, commit = git(folder, "rev-parse", STARTED), git(folder, "rev-parse", "HEAD")
        waiting = git(folder, "rev-list", "origin/main..HEAD")
        if commit == begun:
            raise SystemExit("contribute: there is nothing to add")
        if waiting:  # otherwise an earlier finish pushed it and stopped before the pin
            if git(folder, "rev-list", "--merges", "origin/main..HEAD"):
                raise SystemExit("contribute: a merge can hold changes of its own that a rebase loses: make it plain commits")
            git(folder, "rebase", "-q", "origin/main")
            commit = git(folder, "rev-parse", "HEAD")
            with tempfile.TemporaryDirectory(dir=WORK, prefix="test-") as temp:
                tested = Path(temp, "sdk")
                git(SDK, "worktree", "add", "-q", "--detach", str(tested), commit)  # the commit, and nothing that lies beside it
                try:
                    subprocess.run([*test], cwd=tested, check=True, timeout=1800)
                finally:
                    git(SDK, "worktree", "remove", "--force", str(tested))
            git(folder, "push", "-q", "origin", f"{commit}:refs/heads/main")
        git(SDK, "fetch", "-q", "origin", "main")
    wanted = json.loads(pin.read_text(encoding="utf-8"))
    wanted["commit"] = commit
    pin.write_text(json.dumps(wanted, indent=2) + "\n", encoding="utf-8")
    if git(folder, "rev-parse", "HEAD") != commit:
        raise SystemExit(f"contribute: {commit[:12]} is pushed and pinned, and {folder} has a newer commit: run finish again")
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
