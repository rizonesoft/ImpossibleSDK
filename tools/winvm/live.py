#!/usr/bin/env python3
"""The live reference tests, in the test machine and never on this desktop (WSL only).

    python3 tools/winvm/live.py            run a pass if one is owed: 0 passed or none owed, 1 failed, 75 not now
    python3 tools/winvm/live.py --wait     the same, trying again every 2 minutes while the answer is not now
    python3 tools/winvm/live.py --check    0 when none is owed, 1 when one is

A pass is owed while tests/live-pass.json does not name what reference/, tests/reference/ and COVERED hold now. Not now means
the machine is busy, another machine is running or memory is short: the pass stays owed. The design is
docs/specs/2026-11-24-ui-test-vm-design.md.
"""
import argparse
import contextlib
import hashlib
import json
import os
import shutil
import signal
import subprocess
import sys
import time
import tomllib
import urllib.request
import zipfile
from pathlib import Path

try:
    import fcntl
except ImportError:  # Windows, where only the tests import this
    fcntl = None

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
PASS = "tests/live-pass.json"
COVERED = (
    "tests/test_live.py",
    "tests/frames.py",
    "tests/layout_cases.py",
    "tests/fixtures/layout_reference.json",
    "tools/winui/check_package.py",
    "tools/winui/pin.json",
    "tools/winvm/needs.json",
    "ui/clip.c",
    "ui/clip.h",
    "ui/draw.c",
    "ui/draw.h",
    "ui/layer.c",
    "ui/layer.h",
    "ui/template_data.c",
)
TRY = "build/live-try.json"
CACHE = Path.home() / ".cache/impossible-sdk/winvm"
NOT_NOW = 75
FLOOR_MB = 5120         # what must be available to start
FLOOR_SECONDS = 1800    # from the first try; after that Hyper-V alone decides
HARD_SECONDS = 270      # the guest turns itself off; with start and stop the turn stays within 300
TURN_SECONDS = 340      # the last resort, below the 360 inventory waits
RETRY_SECONDS = 120


def files(root):
    """Every file of the working tree that git tracks or would, as it is now."""
    listed = subprocess.run(["git", "-C", str(root), "ls-files", "-z", "-co", "--exclude-standard"],
                            capture_output=True, check=True, timeout=60).stdout.decode().split("\0")
    return sorted({name for name in listed if name and (root / name).is_file()})


def owes(name):
    return name.startswith(("reference/", "tests/reference/")) or name in COVERED


def fingerprint(root, names=None):
    digest = hashlib.sha256()
    for name in names if names is not None else files(root):
        if owes(name):
            digest.update(name.encode() + b"\0" + hashlib.sha256((root / name).read_bytes()).digest())
    return digest.hexdigest()


def read(path):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    part = path.with_name(path.name + ".part")
    part.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
    os.replace(part, path)


def owed(root):
    return read(root / PASS).get("fingerprint") != fingerprint(root)


def floor_mb(root, print_now, now):
    """The memory target holds for 30 minutes from the first try of what is owed."""
    first = read(root / TRY)
    if first.get("fingerprint") != print_now:
        first = {"fingerprint": print_now, "first": now}
        write(root / TRY, first)
    return FLOOR_MB if now - first["first"] < FLOOR_SECONDS else 0


def fetch(need):
    CACHE.mkdir(parents=True, exist_ok=True)
    path = CACHE / need["url"].rsplit("/", 1)[1]
    if not path.is_file():
        part = path.with_name(f"{path.name}.{os.getpid()}.part")
        with urllib.request.urlopen(need["url"], timeout=120) as answer:
            part.write_bytes(answer.read())
        os.replace(part, path)
    if hashlib.sha256(path.read_bytes()).hexdigest() != need["sha256"]:
        path.unlink()
        raise SystemExit(f"error: {path.name} is not the pinned file, and was removed")
    return path


def there(path):
    return subprocess.run(["wslpath", "-w", str(path)], capture_output=True, text=True, check=True, timeout=30).stdout.strip()


def windows_temp():
    text = subprocess.run(["powershell.exe", "-NoProfile", "-Command", "[IO.Path]::GetTempPath()"],
                          capture_output=True, text=True, check=True, timeout=60).stdout.strip()
    return Path(subprocess.run(["wslpath", "-u", text], capture_output=True, text=True, check=True, timeout=30).stdout.strip())


@contextlib.contextmanager
def alone(root):
    """One attempt at a time for a checkout: they share the folder the job is packed in. False when another has it."""
    path = root / "build/live.lock"
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w") as file:
        try:
            fcntl.flock(file, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            yield False
            return
        yield True


def _kill_process_tree(proc):
    if proc.poll() is not None:
        return
    if os.name == "nt":
        subprocess.run(["taskkill", "/F", "/T", "/PID", str(proc.pid)],
                       stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=30)
    else:
        try:
            os.killpg(proc.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass


def _run_with_tree_kill(command, cwd, timeout, check=False):
    kwargs = {"cwd": cwd, "stdin": subprocess.DEVNULL}
    if os.name == "nt":
        kwargs["creationflags"] = subprocess.CREATE_NEW_PROCESS_GROUP | subprocess.CREATE_NO_WINDOW
    else:
        kwargs["start_new_session"] = True
    proc = subprocess.Popen(command, **kwargs)
    try:
        proc.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        _kill_process_tree(proc)
        proc.wait()
        raise
    if check and proc.returncode != 0:
        raise subprocess.CalledProcessError(proc.returncode, command)
    return proc.returncode


def pack(root):
    """The whole job in one folder on a Windows disk: the tree, the program, Python and a font. Its path and fingerprint."""
    work = windows_temp() / "impossible-live" / hashlib.sha256(str(root).encode()).hexdigest()[:12]
    job = work / "in"
    for part in ("repo", "python", "fonts"):
        shutil.rmtree(job / part, ignore_errors=True)
    names = files(root)
    for name in names:
        target = job / "repo" / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(root / name, target)
    packed = fingerprint(job / "repo", names)
    needs = read(HERE / "needs.json")
    with zipfile.ZipFile(fetch(needs["python"])) as archive:
        archive.extractall(job / "python")
    (job / "fonts").mkdir(parents=True)
    font = fetch(needs["font"])
    shutil.copyfile(font, job / "fonts" / font.name)
    mark = work / "exe.txt"
    if not (job / "exe/Reference.exe").is_file() or not mark.is_file() or mark.read_text() != packed:
        mark.unlink(missing_ok=True)
        shutil.rmtree(job / "exe", ignore_errors=True)
        source = work / "source/reference"
        shutil.rmtree(work / "source", ignore_errors=True)
        shutil.copytree(job / "repo/reference", source)
        command = ["dotnet.exe", "publish", "-c", "Release", "-p:SelfContained=true", "-v", "q", "--nologo", "-o", there(job) + "\\exe"]
        if shutil.which("bosun"):
            command = ["bosun", "run", "--", *command]
        _run_with_tree_kill(command, cwd=source, timeout=1200, check=True)
        mark.write_text(packed)
    return work, packed


def command_for(settings, work, out, floor, bosun):
    machine = ["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", there(HERE / "vm.ps1"), "-Run",
               "-Name", settings["name"], "-VmPath", settings["path"], "-In", there(work) + "\\in", "-Out", there(work) + "\\" + out,
               "-Command", r"python\python.exe repo\reference\live_job.py", "-HardSec", str(HARD_SECONDS)]
    if settings.get("avoid"):
        machine += ["-Avoid", ",".join(settings["avoid"])]
    if not bosun:
        return machine
    return ["bosun", "run", "--ns", "Bosun.VM", "--slots", "1", "--min-avail-mb", str(floor), "--max-wait", "10", "--or-skip", "--", *machine]


def run_machine(command, out):
    """The runner's exit code, then what the machine wrote ({} when nothing)."""
    try:
        code = _run_with_tree_kill(command, cwd=out.parent, timeout=TURN_SECONDS, check=False)
    except subprocess.TimeoutExpired:
        code = 3
    return code, read(out / "result.json")


def attempt(root, settings, machine=run_machine, packer=pack, now=time.time):
    if not settings.get("name"):
        print("not now: .conclave/local.toml names no machine ([winvm])")
        return NOT_NOW
    floor = floor_mb(root, fingerprint(root), now())
    work, packed = packer(root)
    out = work / f"out-{os.getpid()}-{int(now() * 1000)}"
    try:
        code, result = machine(command_for(settings, work, out.name, floor, bool(shutil.which("bosun"))), out)
        if (code == NOT_NOW and result.get("outcome") != "ran") or result.get("outcome") == "not-now" or (code == 4 and not result):
            print(f"not now: {result.get('reason', 'the machine is in use, or memory is short')}; the pass stays owed")
            return NOT_NOW
        passed = code == 0 and result.get("outcome") == "ran" and result.get("exit") == 0
        log = out / "stdout.txt"
        if log.is_file():
            text = log.read_text(encoding="utf-8", errors="replace")
            print(text[-1500:] if passed else text[-6000:])
        if not passed:
            print(f"FAILED: exit {code}, {result or 'no result'}")
            return 1
        commit = subprocess.run(["git", "-C", str(root), "rev-parse", "--short", "HEAD"], capture_output=True, text=True, timeout=30).stdout.strip()
        stamp = time.strftime("%Y-%m-%dT%H:%MZ", time.gmtime(now()))
        write(root / PASS, {"fingerprint": packed, "commit": commit, "at": stamp, "seconds": result.get("seconds")})
        print(f"passed in {result.get('seconds')} s")
        return 0
    finally:
        shutil.rmtree(out, ignore_errors=True)


def settings_of(root):
    try:
        return tomllib.loads((root / ".conclave/local.toml").read_text(encoding="utf-8")).get("winvm", {})
    except OSError:
        return {}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--wait", action="store_true")
    parser.add_argument("--again", action="store_true", help="run a pass even when none is owed")
    given = parser.parse_args(argv)
    if not owed(ROOT) and not given.again:
        print("no live pass is owed")
        return 0
    if given.check:
        print("a live pass is owed: python3 tools/winvm/live.py")
        return 1
    while True:
        with alone(ROOT) as mine:
            if mine and not owed(ROOT) and not given.again:  # another pass settled it meanwhile
                return 0
            if mine:
                code = attempt(ROOT, settings_of(ROOT))
            else:
                print("not now: another pass of this checkout is under way")
                code = NOT_NOW
        if code != NOT_NOW or not given.wait:
            return code
        time.sleep(RETRY_SECONDS)


if __name__ == "__main__":
    sys.exit(main())
