"""The unattended kit's Windows shortcut (D02 T02 §10): unattended-shortcut.ps1 against a stand-in for wsl.exe.

The links are read back through the launchers kit's IShellLinkW (scripts/lib/ShellLink.cs) and their own target and
arguments are run, windowless, against the stand-in, so the command line Windows would hand wsl.exe is the one checked.
Nothing here opens a window or touches the real desktop: every link goes to a scratch folder.
"""
from __future__ import annotations

import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import unittest

SCRIPTS = Path(__file__).resolve().parents[1]


def renderer_for(scripts):
    """The launchers kit's renderer, which this kit brings (D07 T01 section 3): beside these scripts in an installed
    project, in that kit's own folder in the SDK otherwise. The SDK's folder is reached only when the local file is
    absent, through `.parent`, which stops at the drive, so a project at a drive's top (`D:\\Example\\scripts`) is
    never asked for a third parent it does not have."""
    local = scripts / "campaign-icon.py"
    if local.is_file():
        return local
    return scripts.parent.parent.parent / "launchers" / "templates" / "scripts" / "campaign-icon.py"


RENDERER = renderer_for(SCRIPTS)


def loader_for(scripts):
    """The launchers kit's profile loader, which this kit brings with the renderer (D07 T01 section 7): beside these
    scripts in an installed project, in that kit's own folder in the SDK otherwise."""
    local = scripts / "lib" / "launchers.py"
    if local.is_file():
        return local
    return scripts.parent.parent.parent / "launchers" / "templates" / "scripts" / "lib" / "launchers.py"


LOADER = loader_for(SCRIPTS)
LINK_CODE = LOADER.parent / "ShellLink.cs"  # the launchers kit's IShellLinkW, beside its loader
PROVIDER = "Microsoft.PowerShell.Core\\FileSystem::"  # what Resolve-Path's .Path puts before a file on a share
PWSH = shutil.which("pwsh")
# The stand-in for wsl.exe: it lists two distros, translates a Windows path, says whether a folder exists (all do but
# those in STUB_WSL_MISSING, each a path or <distro>:<path>), and records every call; with STUB_WSL_FAIL set it fails
# everything.
STUB_WSL = r'''import json, os, sys
args = sys.argv[1:]
with open(os.environ["STUB_WSL_LOG"], "a", encoding="utf-8") as log:
    log.write(json.dumps(args) + "\n")
if os.environ.get("STUB_WSL_FAIL"):
    sys.exit(1)
if args[:2] == ["-l", "-q"]:
    print("Ubuntu\nDebian")
    sys.exit(0)
rest = args[args.index("--exec") + 1:] if "--exec" in args else []
if rest[:2] == ["wslpath", "-a"]:
    drive, _, tail = rest[2].partition(":")
    print("/mnt/" + drive.lower() + tail.replace("\\", "/"))
elif rest[:2] == ["test", "-d"]:
    missing = os.environ.get("STUB_WSL_MISSING", "").split("|")  # a path, or <distro>:<path> for one distro only
    distro = args[args.index("-d") + 1] if "-d" in args else ""
    spelt = [m.split(":", 1)[1] for m in os.environ.get("STUB_WSL_CASE", "").split("|") if m.startswith(distro + ":")]
    otherwise = any(p.lower() == rest[2].lower() and p != rest[2] for p in spelt)  # test -d minds the case
    sys.exit(1 if rest[2] in missing or f"{distro}:{rest[2]}" in missing or otherwise else 0)
elif rest[:1] == ["find"]:
    # find <dir> -mindepth 1 -maxdepth 1 -iname <part> -print: every folder exists, in the case asked, but those under
    # a path STUB_WSL_MISSING names for that distro (any case); STUB_WSL_CASE names <distro>:<path> spelt otherwise,
    # or <distro>:+<path> for a spelling the distro has as well.
    distro = args[args.index("-d") + 1]
    if distro in os.environ.get("STUB_WSL_UNSURE", "").split("|"):
        sys.exit(4294967295)  # what wsl.exe gives when it cannot run the command in that distro
    found = rest[1].rstrip("/") + "/" + rest[rest.index("-iname") + 1].replace("\\", "")
    gone = [m.split(":", 1)[1].lower() for m in os.environ.get("STUB_WSL_MISSING", "").split("|")
            if m.startswith(distro + ":")]
    if not any(found.lower() == g or found.lower().startswith(g + "/") for g in gone):
        also = []
        for spelt in [m.split(":", 1)[1] for m in os.environ.get("STUB_WSL_CASE", "").split("|")
                      if m.startswith(distro + ":")]:
            extra = spelt.startswith("+")
            spelt = spelt.lstrip("+")
            if spelt.lower().startswith(found.lower()) and spelt[len(found):len(found) + 1] in ("", "/"):
                if extra:
                    also.append(spelt[:len(found)])
                else:
                    found = spelt[:len(found)]
        print("\n".join(dict.fromkeys([found, *also])))
'''


def windowless(group=False):
    """No console window of its own for any process a test starts on Windows (a console child of a parent with
    none opens one over the owner's work: 2026-11-12, Office's launcher tests flashed 5 to 10), in a process group of
    its own with `group`; 0 elsewhere."""
    if os.name != "nt":
        return 0
    return subprocess.CREATE_NO_WINDOW | (subprocess.CREATE_NEW_PROCESS_GROUP if group else 0)



def reclaim(pid):
    """End a fixture's own grandchild by its id, read from the file it wrote: a test that finds it still running
    fails, and this cleanup ends it so the failure leaves no process behind (D02 T02 §12, reconciliation 6)."""
    if os.name == "nt":
        subprocess.run(["taskkill", "/F", "/PID", str(pid)], capture_output=True, creationflags=windowless())
        return
    import signal
    try:
        os.kill(pid, signal.SIGKILL)
    except OSError:  # it has ended already, as the test expects
        pass


DAMAGED_RECORDS = ["", '[{"sets": []}]', "   \n", "null", "[]", "{}", '{"sets": 1}', '{"sets": [1]}',
                   '{"sets": [{"links": []}]}',
                   '{"sets": [{"args": {"ShortcutDir": ""}, "links": []}]}',
                   '{"sets": [{"args": {"ShortcutDir": "C:\\\\d"}, "links": 1}]}']


def record_variants(record):
    """(label, record) for each way a record can differ from what the shortcut writer writes: every key dropped or in
    another case, every value of another type, every text empty (D02 T02 §12's class of damaged records)."""
    import copy

    def walk(node, path):
        if isinstance(node, dict):
            for key, value in node.items():
                yield f"{path}.{key} dropped", lambda r, k=key: r.pop(k), path
                yield f"{path}.{key} as {key.swapcase()}", lambda r, k=key: r.__setitem__(k.swapcase(), r.pop(k)), path
                yield f"{path}.{key} of another type", lambda r, k=key, v=value: r.__setitem__(
                    k, 1 if not isinstance(v, int) else "1"), path
                yield from walk(value, f"{path}.{key}")
        elif isinstance(node, list) and node:
            yield from walk(node[0], f"{path}[0]")
        elif isinstance(node, str):
            yield f"{path} empty", None, path

    def at(root, path):
        node = root
        for part in path.replace("[0]", ".[0]").split(".")[1:]:
            node = node[0] if part == "[0]" else node[part]
        return node

    for label, change, path in walk(record, "$"):
        damaged = copy.deepcopy(record)
        if change is None:  # an empty text: set where it is held
            parent_path, _, key = path.rpartition(".")
            at(damaged, parent_path)[key] = ""
        else:
            change(at(damaged, path))
        yield label, damaged


def lenient_files(record):
    """(label, text) for each file a lenient JSON parser takes and strict JSON refuses, made from `record` (D02 T02
    §12, reconciliation 11: the readers must agree on the file, not only on its fields)."""
    text = json.dumps(record)
    first = next(iter(record["sets"][0]["args"]))
    yield "a trailing comma in an object", text[:-1] + ",}"
    yield "a trailing comma in a list", text.replace("]", ",]", 1)
    yield "a line comment", "// written by hand\n" + text
    yield "a block comment", "/* x */" + text
    yield "a single-quoted string", text.replace('"Ubuntu"', "'Ubuntu'", 1)
    yield "an unquoted key", text.replace('"sets"', "sets", 1)
    yield "NaN", text[:-1] + ', "n": NaN}'
    yield "a repeated key", text[:-1] + ', "sets": []}'
    yield "a key repeated in another case", text.replace(f'"{first}"', f'"{first.lower()}": "x", "{first}"', 1)


def long_path(path):
    """`path` spelt as the script spells what it prints and records: in full, never by an 8.3 short name (the CI
    runner's TEMP is C:\\Users\\RUNNER~1\\...; tests.yml run 44406061065)."""
    return os.path.realpath(path)


def damaged_bytes(record):
    """(label, bytes) for each file whose bytes are not strict UTF-8 although its text looks like `record` (D02 T02
    §12, reconciliation 12: a replacement character hid a damaged byte from the script's own decoder)."""
    text = json.dumps(record).encode("utf-8")
    at = text.index(b'"Ubuntu"') + 2
    yield "a byte that is not UTF-8", text[:at] + b"\xff" + text[at:]
    yield "a cut UTF-8 sequence", text[:at] + b"\xc3" + text[at:]
    yield "an overlong encoding", text[:at] + b"\xc0\xaf" + text[at:]
    yield "an encoded surrogate", text[:at] + b"\xed\xa0\x80" + text[at:]
    yield "UTF-16", json.dumps(record).encode("utf-16")


STARTED: set[int] = set()  # every child this file started, so a file one of them still holds is named as ours


def start_tree(argv, **kwargs):
    """Start a child in a process group of its own, with no console window, so `end_tree` can end everything it
    starts (pwsh starts Python and the WSL stand-in)."""
    kwargs.pop("creationflags", None)
    proc = subprocess.Popen(argv, creationflags=windowless(group=True), start_new_session=os.name != "nt", **kwargs)
    STARTED.add(proc.pid)
    return proc


def holders(path):
    """The processes Windows' Restart Manager says hold `path` open, as (pid, name); empty elsewhere or when it
    cannot tell. It names what kept a scratch folder from being removed (CI runs 44643416476 and 44649349100)."""
    if os.name != "nt":
        return []
    import ctypes
    from ctypes import wintypes

    class Unique(ctypes.Structure):
        _fields_ = [("pid", wintypes.DWORD), ("started", wintypes.FILETIME)]

    class Info(ctypes.Structure):
        _fields_ = [("process", Unique), ("app", wintypes.WCHAR * 256), ("service", wintypes.WCHAR * 64),
                    ("kind", wintypes.DWORD), ("status", wintypes.ULONG), ("session", wintypes.DWORD),
                    ("restartable", wintypes.BOOL)]

    rm = ctypes.WinDLL("rstrtmgr")
    session, key = wintypes.DWORD(), ctypes.create_unicode_buffer(33)
    if rm.RmStartSession(ctypes.byref(session), 0, key):
        return []
    try:
        files = (wintypes.LPCWSTR * 1)(str(path))
        if rm.RmRegisterResources(session, 1, files, 0, None, 0, None):
            return []
        needed, count, reasons = wintypes.UINT(), wintypes.UINT(16), wintypes.DWORD()
        infos = (Info * 16)()
        if rm.RmGetList(session, ctypes.byref(needed), ctypes.byref(count), infos, ctypes.byref(reasons)):
            return []
        return [(infos[i].process.pid, infos[i].app) for i in range(count.value)]
    finally:
        rm.RmEndSession(session)


def remove_scratch(path, wait=10.0):
    """Remove a test's scratch folder. A file one of this file's own children still holds fails at once, naming it.
    A file held only by another program (a virus scanner reading the record the script just moved into place) is
    waited for, up to `wait` seconds, then the failure names who holds it."""
    deadline = time.monotonic() + wait
    while True:
        try:
            shutil.rmtree(path)
            return
        except FileNotFoundError:
            return
        except PermissionError as error:
            held = getattr(error, "filename", None) or path
            who = holders(held)
            ours = [f"{name} (pid {pid})" for pid, name in who if pid in STARTED]
            if ours:
                raise AssertionError(f"{held} is still held by a process this test started: {', '.join(ours)}") from None
            if time.monotonic() >= deadline:
                named = ", ".join(f"{name} (pid {pid})" for pid, name in who) or "a process Windows did not name"
                raise AssertionError(f"{held} is still held {wait:.0f} seconds after the test: {named}") from None
            # WORKAROUND: a program outside the test (an antivirus scan on GitHub's Windows runner) may hold a file
            # it is reading for a moment; the test cannot end it, so it waits, bounded, and names it if it stays.
            time.sleep(0.25)


def end_tree(proc):
    """End a child started by `start_tree` and every process it started: `taskkill /T` while it still lives on
    Windows, its process group elsewhere (a timeout of subprocess.run ends only the direct child)."""
    if proc.poll() is None:
        if os.name == "nt":
            subprocess.run(["taskkill", "/T", "/F", "/PID", str(proc.pid)], capture_output=True,
                           creationflags=windowless())
        else:
            import signal
            try:
                os.killpg(proc.pid, signal.SIGKILL)
            except OSError:
                pass
    try:
        proc.kill()
    except OSError:
        pass
    try:
        proc.wait(timeout=60)  # reaped, so no ended child is left behind as a running one
    except subprocess.TimeoutExpired:
        # A child still alive holds what it opened: the cleanup that follows would fail on its file instead
        # (a busy CI runner took longer than the 10 seconds this once allowed, 2026-11-20).
        raise AssertionError(f"process {proc.pid} still runs 60 seconds after it was ended") from None


def run_tree(argv, *, timeout, input=None, check=False, **kwargs):
    """subprocess.run's result for `argv`, its output captured, its whole tree ended on a timeout or an interrupt
    (D02 T02 §12, reconciliation 5): every process a test here starts goes through this or `start_tree`."""
    kwargs.pop("capture_output", None)
    proc = start_tree(argv, stdin=subprocess.PIPE if input is not None else subprocess.DEVNULL,
                      stdout=subprocess.PIPE, stderr=subprocess.PIPE, **kwargs)
    try:
        out, err = proc.communicate(input, timeout=timeout)
    except BaseException:
        end_tree(proc)
        try:
            proc.communicate(timeout=5)  # bounded: a process that left the tree may hold the pipes
        except subprocess.TimeoutExpired:
            pass
        raise
    done = subprocess.CompletedProcess(argv, proc.returncode, out, err)
    if check:
        done.check_returncode()
    return done


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module



class RendererPathTest(unittest.TestCase):
    """Round 1 of D07 T01 section 3: the renderer is found beside an installed project's scripts at any depth."""

    def test_a_project_at_the_top_of_a_drive_finds_its_own_renderer(self):
        with tempfile.TemporaryDirectory() as tmp:
            scripts = Path(tmp) / "scripts"
            scripts.mkdir()
            (scripts / "campaign-icon.py").write_text("", encoding="utf-8")
            self.assertEqual(renderer_for(scripts), scripts / "campaign-icon.py")
        top = Path(Path.cwd().anchor) / "Example" / "scripts"   # two parents only, and no renderer there
        self.assertEqual(renderer_for(top).name, "campaign-icon.py")

    def test_the_kit_tree_takes_the_launchers_kits_renderer(self):
        if SCRIPTS.parent.name == "templates":
            self.assertEqual(RENDERER, SCRIPTS.parents[2] / "launchers/templates/scripts/campaign-icon.py")
            self.assertTrue(RENDERER.is_file())

class ShortcutFixture(unittest.TestCase):
    """The scratch project, the stand-in for wsl.exe and the helpers every shortcut test uses; it holds no test."""

    PROFILE = {"schema_version": 1, "name": "demo", "agent": "claude", "model": "opus", "effort": "medium",
               "prompt_file": "docs/unattended-prompt.md", "branch": "main", "skip_permissions": True, "tools": [],
               "min_disk_gb": 1, "min_ram_gb": 1, "tick_seconds": 300, "idle_minutes": 45, "stall_hours": 6,
               "preflight": [], "runners": {"agy": {"model": "gemini-4-carbon-medium", "effort": "medium"}}}

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory(ignore_cleanup_errors=True)
        self.addCleanup(self._tmp.cleanup)  # runs last; remove_scratch has removed the folder by then
        self.addCleanup(remove_scratch, self._tmp.name)
        base = Path(self._tmp.name)
        self.project = base / "My Proj's"  # a space and a quote
        for rel in ("lib/unattended.py", "unattended-shortcut.ps1", "unattended-run.py"):  # as the kit installs them
            (self.project / "scripts" / rel).parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(SCRIPTS / rel, self.project / "scripts" / rel)
        shutil.copyfile(RENDERER, self.project / "scripts" / "campaign-icon.py")
        shutil.copyfile(LOADER, self.project / "scripts" / "lib" / "launchers.py")
        shutil.copyfile(LINK_CODE, self.project / "scripts" / "lib" / "ShellLink.cs")
        (self.project / ".conclave").mkdir()
        (self.project / "docs").mkdir()
        (self.project / "docs" / "unattended-prompt.md").write_text("Run the plan.\n", encoding="utf-8")
        self.write_profile(self.PROFILE)
        icon_module = load("campaign_icon_for_shortcut", RENDERER)
        self.icon = base / "brand.png"
        self.icon.write_bytes(icon_module.write_png(256, 256, bytearray(bytes((16, 34, 26, 255)) * 256 * 256)))
        (self.project / "assets").mkdir()
        shutil.copyfile(self.icon, self.project / "assets" / "icon.png")
        self.desk, self.icons = base / "desk", base / "icons"
        # Every run keeps its lock and journals in %LOCALAPPDATA%\conclave-unattended: here, a scratch folder.
        self.local = base / "localappdata"
        self.state = self.local / "conclave-unattended"
        stub = base / "stub_wsl.py"
        stub.write_text(STUB_WSL, encoding="utf-8")
        self.wsl = base / "wsl.cmd"
        self.wsl.write_text(f'@"{sys.executable}" "{stub}" %*\r\n', encoding="utf-8")
        self.log = base / "wsl.log"
        self.env = {**os.environ, "STUB_WSL_LOG": str(self.log), "LOCALAPPDATA": str(self.local)}
        self.run = load("unattended_run_for_shortcut", SCRIPTS / "unattended-run.py")

    def write_profile(self, profile):
        (self.project / ".conclave" / "unattended.json").write_text(json.dumps(profile), encoding="utf-8")

    def write_launchers(self, title, project=None):
        """The launchers kit's profile, `title` being a string, None (unset, as the kit installs it) or raw text."""
        path = (project or self.project) / ".conclave" / "launchers.json"
        if isinstance(title, str) and title.startswith("RAW:"):
            path.write_text(title[4:], encoding="utf-8")
            return
        path.write_text(json.dumps({"schema_version": 1, "title": title, "icon": "assets/icon.png",
                                    "agents": ["claude"], "skip_permissions": False, "links": []}),
                        encoding="utf-8")

    def shortcut(self, *args, project=None, icon_dir=True, desk=None, **env):
        project = project or self.project
        # The user's and the public desktop are stood in for by a scratch folder: nothing here reads the real ones.
        desktops = self.other_desktop if hasattr(self, "other_desktop") else Path(self._tmp.name) / "desktops"
        command = [PWSH, "-NoProfile", "-NonInteractive", "-File", str(project / "scripts" / "unattended-shortcut.ps1"),
                   "-ShortcutDir", str(desk or self.desk), *(["-IconDir", str(self.icons)] if icon_dir else []),
                   "-Desktops", str(desktops), "-Wsl", str(self.wsl),
                   "-Python", sys.executable, *args]
        return run_tree(command, capture_output=True, text=True, timeout=180,
                              env={**self.env, **getattr(self, "extra_env", {}), **env}, creationflags=windowless())

    def read_link(self, path):
        """A link's fields, read through the kit's own IShellLinkW (scripts/lib/ShellLink.cs), which keeps text in any
        script at its full length. The path goes through the environment: it holds a quote."""
        script = ("[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false); Add-Type -Path $env:LINK_CODE; "
                  "$s = [Conclave.ShellLink]::Read($env:LINK_PATH); "
                  "@{target = $s.Target; arguments = $s.Arguments; icon = \"$($s.IconPath),$($s.IconIndex)\"; "
                  "description = $s.Description} | ConvertTo-Json -Compress")
        done = run_tree([PWSH, "-NoProfile", "-NonInteractive", "-Command", script], capture_output=True,
                              text=True, encoding="utf-8", timeout=60,
                              env={**os.environ, "LINK_PATH": str(path), "LINK_CODE": str(LINK_CODE)},
                              creationflags=windowless())
        self.assertEqual(done.returncode, 0, done.stderr)
        return json.loads(done.stdout)

    def set_description(self, path, text, icon=None):
        """Rewrite a link's mark, and its icon when `icon` is given, through the kit's IShellLinkW."""
        done = run_tree([PWSH, "-NoProfile", "-NonInteractive", "-Command",
                               "Add-Type -Path $env:LINK_CODE; $s = [Conclave.ShellLink]::Read($env:LINK_PATH); "
                               "$s.Description = $env:LINK_MARK; if ($env:LINK_ICON) { $s.IconPath = $env:LINK_ICON }; "
                               "[Conclave.ShellLink]::Write($env:LINK_PATH, $s)"],
                              capture_output=True, text=True, timeout=60,
                              env={**os.environ, "LINK_PATH": str(path), "LINK_MARK": text, "LINK_ICON": icon or "",
                                   "LINK_CODE": str(LINK_CODE)},
                              creationflags=windowless())
        self.assertEqual(done.returncode, 0, done.stderr)

    def calls(self):
        return [json.loads(line) for line in self.log.read_text(encoding="utf-8").splitlines()] if self.log.exists() \
            else []

    # The run's Watch and Stop links, one each whatever the runner (D02 T02 §12); `links()` leaves them out, so a test
    # about the campaign links reads only those, and `controls()` reads them.
    CONTROL_LINKS = (" - Watch run.lnk", " - Stop run.lnk")

    def links(self):
        """The campaign links (and any other link), without the run's Watch and Stop links."""
        found = sorted(p.name for p in self.desk.glob("*.lnk")) if self.desk.exists() else []
        return [name for name in found if not name.endswith(self.CONTROL_LINKS)]

    def controls(self):
        return sorted(p.name for p in self.desk.glob("*.lnk") if p.name.endswith(self.CONTROL_LINKS))

    def runner_badges(self, folder=None):
        """The campaign links' badges, without the Watch and Stop links' own."""
        return sorted(p for p in (folder or self.icons).glob("campaign-*.ico")
                      if not p.name.startswith(("campaign-watch-", "campaign-stop-")))

    def install(self):
        done = self.shortcut("-Distro", "Ubuntu", "-Icon", str(self.icon))
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        return done

    def activate(self, link, **env):
        """The link's own target and arguments, as Windows runs them on a double-click, windowless, Enter typed."""
        return run_tree(f'"{link["target"]}" {link["arguments"]}', input="\n", capture_output=True, text=True,
                              timeout=120, env={**self.env, **env}, creationflags=windowless())


class ProcessTreeTest(unittest.TestCase):
    """Reconciliation 5 of D02 T02 §12: a timeout ended only the pwsh a test started, never what it started."""

    def test_a_timeout_ends_the_childs_whole_tree(self):
        with tempfile.TemporaryDirectory() as tmp:
            pid_file = Path(tmp) / "grandchild.pid"
            detach = ("creationflags=subprocess.CREATE_NO_WINDOW" if os.name == "nt" else "start_new_session=False")
            code = ("import pathlib, subprocess, sys, time\n"
                    f"child = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(60)'], {detach})\n"
                    f"pathlib.Path({str(pid_file)!r}).write_text(str(child.pid))\n"
                    "time.sleep(60)\n")
            with self.assertRaises(subprocess.TimeoutExpired):
                run_tree([sys.executable, "-c", code], timeout=5)
            pid = int(pid_file.read_text())
            self.addCleanup(reclaim, pid)
            time.sleep(0.5)
            if os.name == "nt":
                listed = subprocess.run(["tasklist", "/FI", f"PID eq {pid}", "/NH"], capture_output=True, text=True,
                                        creationflags=windowless()).stdout
                self.assertNotIn(f" {pid} ", listed, "the grandchild outlived the timeout")
            else:
                self.assertFalse(Path(f"/proc/{pid}").exists(), "the grandchild outlived the timeout")

    def test_a_child_that_outlives_its_ending_is_reported_not_passed(self):
        # CI 2026-11-20: end_tree gave up after 10 seconds in silence, and the cleanup after it failed on a file the
        # still-running holder kept open. A child that will not end is now the failure itself.
        class Undying:
            pid = 4242
            def poll(self):
                return 0
            def kill(self):
                pass
            def wait(self, timeout=None):
                raise subprocess.TimeoutExpired("holder", timeout)
        with self.assertRaisesRegex(AssertionError, "process 4242 still runs"):
            end_tree(Undying())

    @unittest.skipUnless(os.name == "nt", "an open file blocks its removal on Windows only")
    def test_a_scratch_file_our_child_holds_fails_at_once_naming_it(self):
        with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as tmp:
            scratch = Path(tmp) / "scratch"
            scratch.mkdir()
            record = scratch / "record.json"
            record.write_text("{}", encoding="utf-8")
            holder = start_tree([sys.executable, "-c", "import sys, time; f = open(sys.argv[1]); print('held', flush=True); "
                                 "time.sleep(60)", str(record)], stdout=subprocess.PIPE, text=True)
            self.addCleanup(holder.stdout.close)
            self.addCleanup(end_tree, holder)
            self.assertEqual(holder.stdout.readline().strip(), "held")
            started = time.monotonic()
            with self.assertRaisesRegex(AssertionError, rf"held by a process this test started: .*\(pid {holder.pid}\)"):
                remove_scratch(scratch, wait=30)
            self.assertLess(time.monotonic() - started, 10, "our own holder was waited for, not named at once")
            end_tree(holder)
            remove_scratch(scratch)
            self.assertFalse(scratch.exists())

    @unittest.skipUnless(os.name == "nt", "an open file blocks its removal on Windows only")
    def test_a_scratch_file_another_program_holds_is_waited_for_then_named(self):
        with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as tmp:
            scratch = Path(tmp) / "scratch"
            scratch.mkdir()
            record = scratch / "record.json"
            record.write_text("{}", encoding="utf-8")
            with open(record, encoding="utf-8"):  # this process is no child of the test's: an outside holder
                with self.assertRaisesRegex(AssertionError, rf"still held 1 seconds after the test: .*\(pid {os.getpid()}\)"):
                    remove_scratch(scratch, wait=1)
            remove_scratch(scratch)  # released within the wait: removed
            self.assertFalse(scratch.exists())

    def test_a_scratch_folder_nobody_holds_is_removed(self):
        with tempfile.TemporaryDirectory() as tmp:
            scratch = Path(tmp) / "scratch"
            (scratch / "build").mkdir(parents=True)
            (scratch / "build" / "unattended-shortcuts.json").write_text("{}", encoding="utf-8")
            remove_scratch(scratch)
            self.assertFalse(scratch.exists())
            remove_scratch(scratch)  # already gone: nothing to do

    def test_every_process_this_file_starts_goes_through_the_tree_helpers(self):
        # The class, not the case: any later test that calls subprocess.run or Popen directly fails here, unless it
        # starts a program with no children (attrib, taskkill, tasklist).
        source = Path(__file__).read_text(encoding="utf-8")
        bare = [line.strip() for line in source.splitlines()
                if re.search(r"subprocess\.(run|Popen)\(", line) and "re.search" not in line
                and not line.strip().startswith(("f\"", "\"", "'"))  # code a test hands a child, as text
                and not re.search(r"subprocess\.(run|Popen)\(\[\"(attrib|taskkill|tasklist)\"", line)
                and "proc = subprocess.Popen(argv, creationflags=windowless(group=True)" not in line]
        self.assertEqual(bare, [])


@unittest.skipUnless(os.name == "nt" and PWSH, "a .lnk shortcut and IShellLinkW are Windows's (PowerShell 7)")
class ShortcutTest(ShortcutFixture):
    def test_one_link_per_runner_hands_wsl_the_start_command_of_that_runner(self):
        # The command itself (it starts the run, and waits for Enter when it cannot) is the Linux entry's, run in bash
        # by DesktopEntryTest; the live proof in the findings file ran it through the real wsl.exe.
        self.install()
        self.assertEqual(self.links(), ["My Proj's Claude campaign.lnk", "My Proj's Gemini campaign.lnk"])
        translated = next(c for c in self.calls() if c[3:5] == ["wslpath", "-a"])
        linux = "/mnt/" + translated[5][0].lower() + translated[5][2:].replace("\\", "/")
        self.assertTrue(linux.endswith("/My Proj's"), linux)
        for label, agent in (("Gemini", "agy"), ("Claude", "claude")):
            with self.subTest(agent):
                link = self.read_link(self.desk / f"My Proj's {label} campaign.lnk")
                self.assertEqual(Path(link["target"]).stem, "pwsh")
                icon = link["icon"].rsplit(",", 1)[0]
                self.assertRegex(Path(icon).name, rf"^campaign-{agent}-[0-9a-f]{{8}}\.ico$")
                self.assertTrue(Path(icon).is_file())
                self.log.unlink()
                ran = self.activate(link)
                self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
                self.assertEqual(self.calls(), [["-d", "Ubuntu", "--cd", linux, "--exec", "bash", "-lc",
                                                 self.run.start_command(agent)]])

    def test_watch_and_stop_hand_wsl_their_commands_with_their_badges(self):
        # D02 T02 §12: one Watch and one Stop link whatever the runner. The commands themselves (watch attaches, stop
        # asks first) are run in bash by test_run.py's ControlCommandTest.
        self.install()
        self.assertEqual(self.controls(), ["My Proj's - Stop run.lnk", "My Proj's - Watch run.lnk"])
        for label, kind, command in (("Watch", "watch", self.run.watch_command()),
                                     ("Stop", "stop", self.run.stop_command())):
            with self.subTest(kind):
                link = self.read_link(self.desk / f"My Proj's - {label} run.lnk")
                self.assertEqual(Path(link["target"]).stem, "pwsh")
                icon = Path(link["icon"].rsplit(",", 1)[0])
                self.assertRegex(icon.name, rf"^campaign-{kind}-[0-9a-f]{{8}}\.ico$")
                self.assertEqual(icon.read_bytes(), self.rendered_badge(kind))
                self.assertNotIn('"', command, "the Windows command line between PowerShell and wsl.exe strips it")
                self.log.unlink(missing_ok=True)
                ran = self.activate(link)
                self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
                (call,) = self.calls()
                self.assertEqual((call[:2], call[4:]), (["-d", "Ubuntu"], ["--exec", "bash", "-lc", command]))
        done = self.shortcut("-Remove")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual((self.controls(), list(self.icons.glob("campaign-*.ico"))), ([], []))

    def test_refresh_rewrites_the_links_a_kit_update_left_behind_and_status_names_them(self):
        # D02 T02 §12, seen 2026-11-18: links written before the writable-attach fix kept `attach --watch` after the
        # kit was updated, so the run's window ignored every key until the script was run again.
        record = self.project / "build" / "unattended-shortcuts.json"
        nothing = self.shortcut("-Refresh")  # no record and no link: nothing to do
        self.assertEqual(nothing.returncode, 0, nothing.stdout + nothing.stderr)
        self.assertIn("nothing to do", nothing.stdout)
        self.assertEqual((self.links(), self.controls()), ([], []))
        script = self.project / "scripts" / "unattended-shortcut.ps1"
        current = script.read_text(encoding="utf-8")
        attach = "python3 scripts/unattended-run.py attach || \" +\n    \"{ echo 'The run did not start"
        self.assertEqual(current.count(attach), 1)
        script.write_text(current.replace(attach, attach.replace("attach ||", "attach --watch ||")), encoding="utf-8")
        self.install()  # the links as the kit before the update wrote them
        said = self.run.shortcut_state(self.project)
        self.assertTrue(said.startswith("stale: My Proj's Claude campaign"), said)
        self.assertNotIn("Watch run", said)
        script.write_text(current, encoding="utf-8")  # the update
        done = self.shortcut("-Refresh")  # no -Distro, no -Icon: the record's
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertTrue(self.run.shortcut_state(self.project).startswith("current as recorded ("))
        (saved,) = json.loads(record.read_text(encoding="utf-8"))["sets"]
        self.assertEqual(saved["args"]["Distro"], "Ubuntu")
        self.log.unlink(missing_ok=True)
        ran = self.activate(self.read_link(self.desk / "My Proj's Claude campaign.lnk"))
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        (call,) = self.calls()
        self.assertEqual(call[4:], ["--exec", "bash", "-lc", self.run.start_command("claude")])
        removed = self.shortcut("-Remove")
        self.assertEqual(removed.returncode, 0, removed.stdout + removed.stderr)
        self.assertFalse(record.exists(), "-Remove left the record of links it removed")
        self.assertEqual(self.run.shortcut_state(self.project), "none recorded")

    def test_links_from_before_the_record_are_named_with_the_command_that_writes_them_again(self):
        # D02 T02 §12, the plan review: an install made before the record has links and no record. The folder has an
        # apostrophe, so the command must quote it as PowerShell does (the completion review).
        self.desk = Path(self._tmp.name) / "O'Brien desk"
        self.install()
        (self.project / "build" / "unattended-shortcuts.json").unlink()
        for option in ("-Refresh", "-Check"):
            with self.subTest(option):
                done = self.shortcut(option)
                self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
                self.assertIn("were written before this kit kept a record of them", done.stdout)
                folder = str(self.desk).replace("'", "''")
                self.assertIn("pwsh scripts/unattended-shortcut.ps1 -Distro 'Ubuntu' -Icon <the project's icon> "
                              f"-ShortcutDir '{folder}'", done.stdout)
        self.assertFalse((self.project / "build" / "unattended-shortcuts.json").exists())

    def every_file(self):
        """Each file in the desk, the icon folder and the project, with its bytes and when it was written."""
        return sorted((str(f), f.stat().st_mtime_ns, f.read_bytes())
                      for d in (self.desk, self.icons, self.project) for f in d.rglob("*") if f.is_file())

    def test_check_reads_the_links_themselves_and_writes_nothing(self):
        self.install()
        done = self.shortcut("-Check")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("is current", done.stdout)
        # A link changed by hand, its record left as it was: status cannot see it, -Check does.
        path = self.desk / "My Proj's - Watch run.lnk"
        script = ("Add-Type -Path $env:LINK_CODE; $s = [Conclave.ShellLink]::Read($env:LINK_PATH); "
                  "$s.Arguments = '-NoLogo -Command exit'; [Conclave.ShellLink]::Write($env:LINK_PATH, $s)")
        edited = run_tree([PWSH, "-NoProfile", "-NonInteractive", "-Command", script], capture_output=True,
                                text=True, timeout=60, creationflags=windowless(),
                                env={**os.environ, "LINK_PATH": str(path), "LINK_CODE": str(LINK_CODE)})
        self.assertEqual(edited.returncode, 0, edited.stderr)
        (self.desk / "My Proj's - Stop run.lnk").unlink()
        before = self.every_file()
        done = self.shortcut("-Check")
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        self.assertIn(f"stale: {path}", done.stdout)
        self.assertIn(f"missing: {self.desk / ('My Proj' + chr(39) + 's - Stop run.lnk')}", done.stdout)
        self.assertIn("current: ", done.stdout)
        self.assertEqual(self.every_file(), before, "-Check wrote something")
        self.assertTrue(self.run.shortcut_state(self.project).startswith("current as recorded"))
        self.assertEqual(self.shortcut("-Refresh").returncode, 0)
        self.assertEqual(self.shortcut("-Check").returncode, 0)
        # Round 1 of the review: a link whose mark was changed (its Comment, in Properties) is not current.
        path = self.desk / "My Proj's - Watch run.lnk"
        self.set_description(path, "my own note")
        marked = self.shortcut("-Check")
        self.assertEqual(marked.returncode, 1, marked.stdout + marked.stderr)
        self.assertIn(f"stale: {path}", marked.stdout)
        self.assertEqual(self.shortcut("-Refresh").returncode, 1, "a link whose mark is gone was taken over")

    def test_a_refresh_that_waited_for_the_lock_sees_the_record_as_it_now_is(self):
        # Round 1 of the review: a refresh read its set before the lock, so one that waited out a removal wrote the
        # removed links back from what it had read.
        self.install()
        record = self.project / "build" / "unattended-shortcuts.json"
        key = next(self.icons.glob("campaign-claude-*.ico")).stem.rsplit("-", 1)[1]
        holder = open(self.state / f"campaign-{key}.lock", "a")  # any open handle blocks the run's unshared one
        try:
            waiting = start_tree(
                [PWSH, "-NoProfile", "-NonInteractive", "-File",
                 str(self.project / "scripts" / "unattended-shortcut.ps1"),
                 "-Refresh", "-IconDir", str(self.icons), "-Desktops", str(Path(self._tmp.name) / "desktops"),
                 "-Wsl", str(self.wsl), "-Python", sys.executable],
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=self.env, creationflags=windowless())
            self.addCleanup(end_tree, waiting)
            time.sleep(1.5)  # it has read the record and waits for the lock (it tries for five seconds)
            saved = json.loads(record.read_text(encoding="utf-8"))
            saved["sets"][0]["written"] = "2000-01-01T00:00:00Z"  # another run wrote the set meanwhile
            record.write_text(json.dumps(saved), encoding="utf-8")
        finally:
            holder.close()
        out, _ = waiting.communicate(timeout=180)
        self.assertEqual(waiting.returncode, 1, out)
        self.assertIn("changed while this run waited for the lock", out)

    def test_check_makes_no_state_folder_and_takes_no_lock(self):
        # The completion review: -Check made the state folder and opened a lock file, so it needed the right to write.
        self.install()
        shutil.rmtree(self.state)
        done = self.shortcut("-Check")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertFalse(self.state.exists(), "-Check made the state folder")

    def test_a_record_that_cannot_be_read_is_left_as_it_is(self):
        # The completion review: an unreadable record (another folder's sets in it) was replaced by this folder's.
        record = self.project / "build" / "unattended-shortcuts.json"
        record.parent.mkdir()
        record.write_text("{ not json", encoding="utf-8")
        done = self.install()
        self.assertIn("cannot be read", done.stdout)
        self.assertIn("left as it is", done.stdout)
        self.assertEqual(record.read_text(encoding="utf-8"), "{ not json")
        self.assertTrue(self.links())
        removed = self.shortcut("-Remove")
        self.assertEqual(removed.returncode, 0, removed.stdout + removed.stderr)
        self.assertEqual(record.read_text(encoding="utf-8"), "{ not json")
        # The record may name another folder whose links show these badges (the reconciliation): they stay.
        self.assertEqual(self.links(), [])
        self.assertTrue(list(self.icons.glob("campaign-*.ico")), "a removal under an unreadable record took the badges")
        self.assertIn("the badges in", removed.stdout)
        # The advice works when followed (the second reconciliation): move the record aside, run again, and the new
        # record reads back as current.
        refused = self.shortcut("-Check")
        self.assertEqual(refused.returncode, 1, refused.stdout + refused.stderr)
        self.assertIn("move it aside", refused.stdout)
        record.rename(record.with_name("unattended-shortcuts.json.bad"))
        self.install()
        self.assertTrue(self.run.shortcut_state(self.project).startswith("current as recorded"))
        self.assertEqual(self.shortcut("-Check").returncode, 0)

    def test_the_script_reads_only_the_record_it_writes(self):
        # The class, generated from a record this script wrote (reconciliations 1, 7, 9 and 10): each field dropped,
        # its key in another case, its value of another type or an empty text makes -Check say the record cannot be
        # read, and status says the same of it.
        self.install()
        record = self.project / "build" / "unattended-shortcuts.json"
        whole = json.loads(record.read_text(encoding="utf-8"))
        current = self.shortcut("-Check")  # the record as written is read, so a rejection below is the variant's
        self.assertEqual(current.returncode, 0, current.stdout + current.stderr)
        variants = list(record_variants(whole))
        self.assertGreaterEqual(len(variants), 40)
        for label, damaged in variants:
            with self.subTest(label):
                record.write_text(json.dumps(damaged), encoding="utf-8")
                checked = self.shortcut("-Check")
                self.assertEqual(checked.returncode, 1, checked.stdout + checked.stderr)
                self.assertIn("cannot be read", checked.stdout)
                self.assertTrue(self.run.shortcut_state(self.project).startswith("build/unattended-shortcuts.json "
                                                                                  "cannot be read"))

    def test_the_script_refuses_a_file_that_is_not_strict_utf8(self):
        # Reconciliation 12: Get-Content turned a damaged byte into a replacement character and the record passed.
        self.install()
        record = self.project / "build" / "unattended-shortcuts.json"
        for label, data in damaged_bytes(json.loads(record.read_text(encoding="utf-8"))):
            with self.subTest(label):
                record.write_bytes(data)
                checked = self.shortcut("-Check")
                self.assertEqual(checked.returncode, 1, checked.stdout + checked.stderr)
                self.assertIn("cannot be read", checked.stdout)

    def test_a_reader_that_does_not_answer_keeps_the_record_and_the_badges(self):
        # Reconciliation 13: a reader ended without its answer read as zero sets, so an install could replace a
        # sound multi-folder record and a removal take another folder's badges. Its result is now explicit.
        self.install()
        other = Path(self._tmp.name) / "desk2"
        self.assertEqual(self.shortcut("-Distro", "Ubuntu", "-Icon", str(self.icon), desk=other).returncode, 0)
        record = self.project / "build" / "unattended-shortcuts.json"
        before = record.read_bytes()
        silent = Path(self._tmp.name) / "silent-python.cmd"
        silent.write_text("@exit /b 9\r\n", encoding="utf-8")  # a reader ended before it said anything
        removed = run_tree([PWSH, "-NoProfile", "-NonInteractive", "-File",
                            str(self.project / "scripts" / "unattended-shortcut.ps1"), "-Remove", "-ShortcutDir",
                            str(other), "-IconDir", str(self.icons),
                            "-Desktops", str(Path(self._tmp.name) / "desktops"),
                            "-Wsl", str(self.wsl), "-Python", str(silent)],
                           text=True, timeout=180, env=self.env, creationflags=windowless())
        self.assertEqual(removed.returncode, 0, removed.stdout + removed.stderr)
        self.assertIn("its reader ended with exit 9 and said nothing", removed.stdout)
        self.assertEqual(record.read_bytes(), before, "a reader that did not answer let the record be rewritten")
        self.assertTrue(list(self.icons.glob("campaign-*.ico")), "a reader that did not answer let the badges go")
        self.assertEqual(self.shortcut("-Check").returncode, 0, "the desktop's links lost their badges")
        # A missing runner is a reader that cannot answer: the advice is to restore it, not to move a sound record.
        (self.project / "scripts" / "unattended-run.py").rename(self.project / "scripts" / "unattended-run.py.away")
        checked = self.shortcut("-Check")
        self.assertEqual(checked.returncode, 1, checked.stdout + checked.stderr)
        self.assertIn("restore what reads it", checked.stdout)
        self.assertIn("python forge.py update <project> --only scripts/unattended-run.py", checked.stdout)
        self.assertNotIn("move it aside", checked.stdout)
        self.assertEqual(record.read_bytes(), before)
        (self.project / "scripts" / "unattended-run.py.away").rename(self.project / "scripts" / "unattended-run.py")
        # A runner from an older kit, which the project changed and update keeps (reconciliation 17): it has no
        # shortcut-record. The advice is the merge that brings the reader, and following it reads the record again.
        runner = self.project / "scripts" / "unattended-run.py"
        current = runner.read_bytes()
        runner.write_text("import argparse\np = argparse.ArgumentParser()\np.add_subparsers(dest='c', required=True)"
                          ".add_parser('status')\np.parse_args()\n", encoding="utf-8")
        older = self.shortcut("-Check")
        self.assertEqual(older.returncode, 1, older.stdout + older.stderr)
        self.assertIn("is from an older kit: it has no shortcut-record", older.stdout)
        self.assertIn("python forge.py update <project> --kit-new", older.stdout)
        self.assertNotIn("move it aside", older.stdout)
        self.assertEqual(record.read_bytes(), before)
        # The merge forge's --kit-new leads to (test_forge's test_an_older_changed_runner_is_brought_to_the_reader_by_the_
        # merge_the_script_advises): the kit's runner with the project's change kept. Then the -Refresh that failed.
        runner.write_bytes(current + b"# the project's own change\n")
        refreshed = self.shortcut("-Refresh")
        self.assertEqual(refreshed.returncode, 0, refreshed.stdout + refreshed.stderr)
        self.assertEqual(self.shortcut("-Check").returncode, 0, "the links were not current after the refresh")
        # A record another program holds open may be sound: the advice is to try again, not to move it.
        holder = start_tree([PWSH, "-NoProfile", "-NonInteractive", "-Command",
                             "$f = [IO.File]::Open($env:RECORD, 'Open', 'Read', 'None'); 'held'; Start-Sleep 60"],
                            stdout=subprocess.PIPE, text=True, env={**os.environ, "RECORD": str(record)},
                            creationflags=windowless())
        self.addCleanup(holder.stdout.close)
        self.addCleanup(end_tree, holder)
        self.assertEqual(holder.stdout.readline().strip(), "held")
        busy = self.shortcut("-Check")
        self.assertEqual(busy.returncode, 1, busy.stdout + busy.stderr)
        self.assertIn("run the script again once nothing holds the file", busy.stdout)
        self.assertNotIn("move it aside", busy.stdout)

    def test_the_script_reads_the_record_with_the_runners_reader(self):
        # The redesign: one reader for the script and status, so they agree on every file by construction.
        script = (SCRIPTS / "unattended-shortcut.ps1").read_text(encoding="utf-8")
        self.assertIn("shortcut-record $recordPath", script)
        self.assertNotIn("Get-Content -LiteralPath $recordPath", script)

    def test_the_script_refuses_a_file_strict_json_refuses(self):
        # Reconciliation 11: ConvertFrom-Json took a trailing comma that both Python readers refused.
        self.install()
        record = self.project / "build" / "unattended-shortcuts.json"
        for label, text in lenient_files(json.loads(record.read_text(encoding="utf-8"))):
            with self.subTest(label):
                record.write_text(text, encoding="utf-8")
                checked = self.shortcut("-Check")
                self.assertEqual(checked.returncode, 1, checked.stdout + checked.stderr)
                self.assertIn("cannot be read", checked.stdout)
                self.assertTrue(self.run.shortcut_state(self.project).startswith("build/unattended-shortcuts.json "
                                                                                  "cannot be read"))

    def test_every_damaged_record_is_kept_and_holds_the_badges(self):
        # The class, not the case (reconciliations 1 and 7): a record is judged by its shape. Each of these is
        # unreadable: an install keeps it, a removal keeps it and the badges, and -Check says to move it aside.
        record = self.project / "build" / "unattended-shortcuts.json"
        for text in DAMAGED_RECORDS:
            with self.subTest(record=text):
                record.parent.mkdir(exist_ok=True)
                record.write_text(text, encoding="utf-8")
                done = self.install()
                self.assertIn("cannot be read", done.stdout)
                self.assertEqual(record.read_text(encoding="utf-8"), text)
                checked = self.shortcut("-Check")
                self.assertEqual(checked.returncode, 1, checked.stdout + checked.stderr)
                self.assertIn("move it aside", checked.stdout)
                removed = self.shortcut("-Remove")
                self.assertEqual(removed.returncode, 0, removed.stdout + removed.stderr)
                self.assertEqual(record.read_text(encoding="utf-8"), text)
                self.assertTrue(list(self.icons.glob("campaign-*.ico")),
                                "a removal under a damaged record took the badges")
                for badge in self.icons.glob("campaign-*.ico"):
                    badge.unlink()

    def test_check_names_a_stopped_run_and_plays_no_journal(self):
        # Round 1 of the review: -Check played a stopped run's journal before reading the links.
        self.install()
        self.state.mkdir(parents=True, exist_ok=True)
        key = next(self.icons.glob("campaign-claude-*.ico")).stem.rsplit("-", 1)[1]
        journal = self.state / f"campaign-{key}.999999.journal.json"
        journal.write_text(json.dumps({"state": "writing", "started": 0, "desk": str(self.desk),
                                       "icons": str(self.icons), "temps": [], "moves": [], "aside": []}),
                           encoding="utf-8")
        before = self.every_file()
        done = self.shortcut("-Check")
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        self.assertIn(f"unfinished: {long_path(journal)}", done.stdout)
        self.assertTrue(journal.exists(), "-Check played the journal")
        self.assertEqual(self.every_file(), before)

    def test_check_plays_no_journal_a_stopped_run_leaves_after_its_scan(self):
        # Reconciliation 3 and 4: -Check scanned for journals, found none, then reached the recovery, which scans again
        # and would play a journal a stopped writer left in between. A stand-in for Get-ChildItem (a function wins over
        # the cmdlet) makes the first journal scan come back empty and leaves a partial journal for any later one.
        self.install()
        key = next(self.icons.glob("campaign-claude-*.ico")).stem.rsplit("-", 1)[1]
        partial = self.state / f"campaign-{key}.999999.journal.json.tmp"
        wrapper = (
            "$global:journalScans = 0\n"
            "function Get-ChildItem {\n"
            "  $filter = if ($args -contains '-Filter') { $args[[array]::IndexOf($args, '-Filter') + 1] }\n"
            "  if ($filter -like '*journal.json*') {\n"
            "    $global:journalScans++\n"
            "    if ($global:journalScans -eq 1) { Set-Content -LiteralPath $env:PARTIAL -Value '{'; return }\n"
            "  }\n"
            "  Microsoft.PowerShell.Management\\Get-ChildItem @args\n"
            "}\n"
            "& $env:SCRIPT -Check -ShortcutDir $env:DESK -IconDir $env:ICONS -Desktops $env:DESKTOPS -Wsl $env:WSL "
            "-Python $env:PY\n"
            "exit $LASTEXITCODE\n")
        done = run_tree([PWSH, "-NoProfile", "-NonInteractive", "-Command", wrapper], capture_output=True,
                              text=True, timeout=180, creationflags=windowless(),
                              env={**self.env, "PARTIAL": str(partial), "DESK": str(self.desk),
                                   "ICONS": str(self.icons),
                                   "DESKTOPS": str(Path(self._tmp.name) / "desktops"), "WSL": str(self.wsl),
                                   "PY": sys.executable,
                                   "SCRIPT": str(self.project / "scripts" / "unattended-shortcut.ps1")})
        self.assertTrue(partial.exists(), "-Check played a journal left after its scan: " + done.stdout + done.stderr)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)

    def test_two_folders_keep_a_set_each(self):
        self.install()
        other = Path(self._tmp.name) / "desk2"
        done = self.shortcut("-Distro", "Ubuntu", "-Icon", str(self.icon), desk=other)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        record = self.project / "build" / "unattended-shortcuts.json"
        folders = sorted(s["args"]["ShortcutDir"] for s in json.loads(record.read_text(encoding="utf-8"))["sets"])
        self.assertEqual(folders, sorted([long_path(self.desk), long_path(other)]))
        # -Refresh without a folder names a command for each; with one, it writes that one.
        both = run_tree([PWSH, "-NoProfile", "-NonInteractive", "-File",
                               str(self.project / "scripts" / "unattended-shortcut.ps1"), "-Refresh",
                               "-Desktops", str(Path(self._tmp.name) / "desktops"), "-Wsl", str(self.wsl),
                               "-Python", sys.executable], capture_output=True, text=True, timeout=180, env=self.env,
                              creationflags=windowless())
        self.assertEqual(both.returncode, 1, both.stdout + both.stderr)
        self.assertIn("recorded in 2 folders", both.stdout)
        self.assertIn(f"-ShortcutDir '{long_path(other)}'", both.stdout)
        self.assertEqual(self.shortcut("-Refresh", desk=other).returncode, 0)
        removed = self.shortcut("-Remove", desk=other)
        self.assertEqual(removed.returncode, 0, removed.stdout + removed.stderr)
        (left,) = json.loads(record.read_text(encoding="utf-8"))["sets"]
        self.assertEqual(left["args"]["ShortcutDir"], long_path(self.desk))
        self.assertTrue(self.links(), "removing one folder's links took the other's")
        for name in self.links() + self.controls():  # and their badges, which the two folders share
            with self.subTest(name):
                shown = Path(self.read_link(self.desk / name)["icon"].rsplit(",", 1)[0])
                self.assertTrue(shown.is_file(), f"removing the other folder took {shown}")
        self.assertEqual(self.shortcut("-Check").returncode, 0)
        self.assertEqual(self.shortcut("-Remove").returncode, 0)
        self.assertFalse(record.exists())

    def test_a_record_that_cannot_be_written_leaves_the_links_in_place_and_says_so(self):
        (self.project / "build").write_text("a file where the folder goes", encoding="utf-8")
        done = self.install()
        self.assertIn("could not be written", done.stdout)
        self.assertIn("-Check reads them", done.stdout)
        self.assertTrue(self.links())
        self.assertEqual(list((self.project).glob("build.*")), [])

    def rendered_badge(self, kind):
        out = Path(self._tmp.name) / f"expected-{kind}.ico"
        done = run_tree([sys.executable, "-B", str(RENDERER), "badge", str(self.icon), kind, str(out)],
                              capture_output=True, text=True, timeout=120, creationflags=windowless())
        self.assertEqual(done.returncode, 0, done.stderr)
        return out.read_bytes()

    def test_a_project_in_wsl_starts_from_its_linux_path(self):
        for label, project in (("the distro's own UNC path", "\\\\wsl.localhost\\Ubuntu\\home\\x\\demo"),
                               ("the older wsl$ form", "\\\\wsl$\\Ubuntu\\home\\x\\demo"),
                               ("a Linux path", "/home/x/demo")):
            with self.subTest(label):
                shutil.rmtree(self.desk, ignore_errors=True)
                self.log.unlink(missing_ok=True)
                done = self.shortcut("-Distro", "Ubuntu", "-Icon", str(self.icon), "-Project", project)
                self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
                self.assertFalse([c for c in self.calls() if "wslpath" in c], "a WSL path went through wslpath")
                self.log.unlink()
                ran = self.activate(self.read_link(self.desk / "demo Claude campaign.lnk"))
                self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
                self.assertEqual(self.calls()[0][:5], ["-d", "Ubuntu", "--cd", "/home/x/demo", "--exec"])

    def test_a_project_in_another_distro_is_refused(self):
        done = self.shortcut("-Distro", "Ubuntu", "-Icon", str(self.icon), "-Project",
                             "\\\\wsl.localhost\\Debian\\home\\x\\demo")
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        self.assertIn("the project is in distro 'Debian', not 'Ubuntu'", done.stdout)
        self.assertEqual(self.links(), [])

    def test_a_failure_of_wsl_itself_is_said_and_waits_for_enter(self):
        # Round 1 of D02 T02 §10: a drive gone or a distro removed fails before bash, whose own wait cannot run.
        self.install()
        link = self.read_link(self.desk / "My Proj's Claude campaign.lnk")
        waiting = start_tree(f'"{link["target"]}" {link["arguments"]}', stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                   env={**self.env, "STUB_WSL_FAIL": "1"}, creationflags=windowless())
        self.addCleanup(end_tree, waiting)
        time.sleep(5)
        self.assertIsNone(waiting.poll(), "the window closed without waiting for Enter")
        out, _ = waiting.communicate("\n", timeout=60)  # Enter: it closes
        self.assertIn("WSL could not start the run (exit 1): the reason is above. Press Enter to close.", out)
        self.assertEqual(waiting.returncode, 0)

    def test_a_link_is_kept_when_its_replacement_cannot_be_made(self):
        self.install()
        link = self.desk / "My Proj's Claude campaign.lnk"
        before = link.read_bytes()
        broken = Path(self._tmp.name) / "not-an-image.png"
        broken.write_bytes(b"not a png")
        done = self.shortcut("-Distro", "Ubuntu", "-Icon", str(broken))
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        self.assertIn("could not be rendered; its shortcut is left as it was", done.stdout)
        self.assertEqual(link.read_bytes(), before)
        self.assertEqual([p.name for p in self.desk.iterdir() if ".tmp" in p.name], [], "a temporary link was left")

    def test_remove_takes_this_projects_links_whatever_the_setup_now_is(self):
        self.install()
        other = self.desk / "Other Claude campaign.lnk"
        shutil.copyfile(self.desk / "My Proj's Claude campaign.lnk", other)
        done = run_tree([PWSH, "-NoProfile", "-NonInteractive", "-Command",
                               "Add-Type -Path $env:LINK_CODE; $s = [Conclave.ShellLink]::Read($env:LINK_PATH); "
                               "$s.Description = 'conclave-unattended: D:\\elsewhere'; "
                               "[Conclave.ShellLink]::Write($env:LINK_PATH, $s)"],
                              capture_output=True, text=True, timeout=60,
                              env={**os.environ, "LINK_PATH": str(other), "LINK_CODE": str(LINK_CODE)},
                              creationflags=windowless())
        self.assertEqual(done.returncode, 0, done.stderr)
        (self.desk / "notes.txt").write_text("mine\n", encoding="utf-8")
        another = self.icons / "campaign-claude-deadbeef.ico"  # another project's icon in a shared icon folder
        another.write_bytes(b"icon")
        self.write_profile({**self.PROFILE, "runners": {}})  # the profile dropped a runner
        self.icon.unlink()  # the icon is gone
        done = self.shortcut("-Remove", STUB_WSL_FAIL="1")  # and WSL answers nothing
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(self.links(), ["Other Claude campaign.lnk"])
        self.assertTrue((self.desk / "notes.txt").exists())
        self.assertEqual(list(self.icons.glob("campaign-*.ico")), [another])

    def test_what_cannot_make_a_working_link_is_refused_before_anything_is_written(self):
        cases = {"a distro WSL does not list": (["-Distro", "Arch", "-Icon", str(self.icon)], {}, "no distro 'Arch'"),
                 "a project WSL does not have": (["-Distro", "Ubuntu", "-Icon", str(self.icon)],
                                                 {"STUB_WSL_MISSING": "*"}, "does not exist in 'Ubuntu'"),
                 "no icon": (["-Distro", "Ubuntu"], {}, "-Icon is not a file"),
                 "a profile the run would refuse": (["-Distro", "Ubuntu", "-Icon", str(self.icon)], {},
                                                    "the run profile cannot be read")}
        for label, (args, env, said) in cases.items():
            with self.subTest(label):
                if label == "a profile the run would refuse":
                    self.write_profile({**self.PROFILE, "effort": "extreme"})
                if label == "a project WSL does not have":
                    env = {"STUB_WSL_MISSING": "|".join(self._linux_candidates())}
                done = self.shortcut(*args, **env)
                self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
                self.assertIn(said, done.stdout)
                self.assertEqual(self.links(), [])

    def _linux_candidates(self):
        """The WSL path the stand-in gives this project, for the long and the short spelling of the temp folder."""
        paths = {str(self.project), os.path.realpath(self.project)}
        return ["/mnt/" + p[0].lower() + p[2:].replace("\\", "/") for p in paths]


@unittest.skipUnless(os.name == "nt" and PWSH, "a .lnk shortcut and IShellLinkW are Windows's (PowerShell 7)")
class CampaignTitleTest(ShortcutFixture):
    """D07 T01 section 7: the campaign links carry the launcher profile's title, read an icon from a share, keep one
    identity per project, and replace a set of links whole or not at all."""

    def snapshot(self, skip=()):
        """Every file in the desktop and icon folders, by name, with its bytes (but those named in `skip`)."""
        return {p.relative_to(p.parents[1]).as_posix(): p.read_bytes()
                for folder in (self.desk, self.icons) if folder.exists() for p in folder.iterdir()
                if p.is_file() and p.suffix != ".lock"  # a run's lock is no state: it goes when the run ends
                and p.name not in skip}

    def rendered(self, agent):
        return self.rendered_from(self.icon, agent)

    def second_project(self, title):
        other = Path(self._tmp.name) / "Other"
        shutil.copytree(self.project, other)
        self.write_launchers(title, other)
        return other

    def test_a_rename_with_a_provider_qualified_icon_writes_the_titled_set_and_nothing_else(self):
        self.install()
        self.assertEqual(self.links(), ["My Proj's Claude campaign.lnk", "My Proj's Gemini campaign.lnk"])
        self.write_launchers("Demo Site")
        done = self.shortcut("-Distro", "Ubuntu", "-Icon", PROVIDER + str(self.icon))
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(self.links(), ["Demo Site Claude campaign.lnk", "Demo Site Gemini campaign.lnk"])
        self.assertEqual(len(self.runner_badges()), 2)
        self.assertEqual((len(self.runner_badges()), len(list(self.icons.iterdir()))), (2, 4))  # Watch, Stop
        for label, agent in (("Gemini", "agy"), ("Claude", "claude")):
            with self.subTest(agent):
                link = self.read_link(self.desk / f"Demo Site {label} campaign.lnk")
                self.assertEqual(Path(link["icon"].rsplit(",", 1)[0]).read_bytes(), self.rendered(agent))
                self.log.unlink()
                ran = self.activate(link)
                self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
                self.assertEqual(self.calls()[0][-1], self.run.start_command(agent))
                self.assertEqual(self.calls()[0][:2], ["-d", "Ubuntu"])

    def test_a_provider_qualified_project_is_named_by_its_plain_path(self):
        self.install()
        plain = self.read_link(self.desk / "My Proj's Claude campaign.lnk")["description"]
        shutil.rmtree(self.desk)
        self.log.unlink()
        done = self.shortcut("-Distro", "Ubuntu", "-Icon", str(self.icon), "-Project", PROVIDER + str(self.project))
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        mark = self.read_link(self.desk / "My Proj's Claude campaign.lnk")["description"]
        self.assertEqual(mark, plain)
        self.assertNotIn("::", mark)
        translated = next(c for c in self.calls() if c[3:5] == ["wslpath", "-a"])
        self.assertNotIn("::", translated[5])

    def test_a_project_by_its_short_name_is_the_same_project_as_by_its_own(self):
        # CI's Windows runner keeps its temp folder as C:\Users\RUNNER~1: the same folder by two spellings.
        import ctypes
        size = 32768
        text = ctypes.create_unicode_buffer(size)
        long_name = os.path.realpath(self.project)
        if not ctypes.windll.kernel32.GetShortPathNameW(long_name, text, size) or text.value == long_name:
            self.skipTest("this volume keeps no 8.3 short names")
        short = text.value
        self.install()
        before = self.snapshot()
        done = self.shortcut("-Distro", "Ubuntu", "-Icon", str(self.icon), "-Project", short)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(self.links(), ["My Proj's Claude campaign.lnk", "My Proj's Gemini campaign.lnk"])
        self.assertEqual(sorted(k for k in self.snapshot()), sorted(before))
        mark = self.read_link(self.desk / "My Proj's Claude campaign.lnk")["description"]
        self.assertEqual(mark.lower(), ("conclave-unattended: " + long_name).lower())

    def test_the_title_names_the_links_exactly_whatever_its_script(self):
        self.write_launchers("\u6771\u4eac")
        self.install()
        self.assertEqual(self.links(), ["\u6771\u4eac Claude campaign.lnk", "\u6771\u4eac Gemini campaign.lnk"])

    def test_a_project_named_outside_the_ansi_code_page_keeps_its_links_its_own(self):
        # A project in WSL, so no path goes through the stand-in for wsl.exe: it is a batch file, and cmd hands a
        # batch file its arguments in the ANSI code page, which the real wsl.exe does not.
        where = ["-Distro", "Ubuntu", "-Project", "/home/x/東京"]
        for _ in range(2):  # the second run finds its own links by their mark, never refused as another's
            done = self.shortcut(*where, "-Icon", str(self.icon))
            self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(self.links(), ["東京 Claude campaign.lnk", "東京 Gemini campaign.lnk"])
        link = self.read_link(self.desk / "東京 Claude campaign.lnk")
        self.assertEqual(link["description"], "conclave-unattended: \\\\wsl.localhost\\Ubuntu\\home\\x\\東京")
        self.assertTrue(Path(link["icon"].rsplit(",", 1)[0]).is_file(), link)
        self.assertIn("'/home/x/東京'", link["arguments"])
        self.log.unlink()
        ran = self.activate(link)  # the link's own arguments, as Windows reads them, start the run there
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        self.assertEqual(len(self.calls()), 1)
        done = self.shortcut("-Remove", *where)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual((self.links(), list(self.icons.glob("*.ico"))), ([], []))

    def test_an_unset_title_keeps_the_folders_name(self):
        for label, text in (("unset", None), ("the kit's template", "RAW:" + json.dumps(
                {"schema_version": 1, "title": None, "icon": None, "agents": ["claude"], "skip_permissions": False,
                 "links": []}))):
            with self.subTest(label):
                shutil.rmtree(self.desk, ignore_errors=True)
                self.write_launchers(text)
                self.install()
                self.assertEqual(self.links(), ["My Proj's Claude campaign.lnk", "My Proj's Gemini campaign.lnk"])

    def test_a_refused_profile_or_a_missing_loader_changes_nothing_and_says_why(self):
        self.install()
        before = self.snapshot()
        loader = self.project / "scripts" / "lib" / "launchers.py"
        cases = (("a bad title", lambda: self.write_launchers(" x"), "title: must not start or end with a space"),
                 ("not JSON", lambda: self.write_launchers("RAW:{"), "not JSON"),
                 ("no loader", lambda: loader.unlink(),
                  "there is no scripts/lib/launchers.py (python forge.py kit <project> unattended installs it)"))
        for label, make, said in cases:
            with self.subTest(label):
                self.write_launchers("Demo Site")
                make()
                done = self.shortcut("-Distro", "Ubuntu", "-Icon", str(self.icon))
                self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
                self.assertIn("unattended-shortcut: the launcher profile cannot be read: ", done.stdout)
                self.assertIn(said, done.stdout)
                self.assertIn("nothing was written", done.stdout)
                self.assertNotIn("Traceback", done.stdout + done.stderr)
                self.assertEqual(self.snapshot(), before)

    def test_without_the_link_code_nothing_is_written_or_removed(self):
        self.install()
        before = self.snapshot()
        (self.project / "scripts" / "lib" / "ShellLink.cs").unlink()
        for args in (["-Distro", "Ubuntu", "-Icon", str(self.icon)], ["-Remove"]):
            with self.subTest(args[0]):
                done = self.shortcut(*args)
                self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
                self.assertIn("there is no scripts/lib/ShellLink.cs (python forge.py kit <project> unattended "
                              "installs it); nothing was written", done.stdout)
                self.assertEqual(self.snapshot(), before)

    def test_remove_works_with_the_launcher_profile_broken_or_its_loader_gone(self):
        for label in ("malformed", "no loader"):
            with self.subTest(label):
                self.install()
                self.write_launchers("RAW:{")
                if label == "no loader":
                    (self.project / "scripts" / "lib" / "launchers.py").unlink()
                done = self.shortcut("-Remove")
                self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
                self.assertEqual((self.links(), list(self.icons.glob("*.ico"))), ([], []))
                shutil.copyfile(LOADER, self.project / "scripts" / "lib" / "launchers.py")
                (self.project / ".conclave" / "launchers.json").unlink()

    def test_a_dropped_runner_takes_its_link_and_badge_and_keeps_the_others(self):
        self.write_launchers("Demo Site")
        self.install()
        claude = [p for p in self.icons.iterdir() if p.name.startswith("campaign-claude-")]
        self.write_profile({**self.PROFILE, "runners": {}})
        self.install()
        self.assertEqual(self.links(), ["Demo Site Claude campaign.lnk"])
        self.assertEqual(self.runner_badges(), claude)

    def test_a_name_held_by_another_file_is_refused_and_a_retry_writes_the_whole_set(self):
        self.install()  # the folder-named set, which a rename must keep until it can replace it whole
        for label in ("another project's link", "a folder"):
            with self.subTest(label):
                blocker = self.desk / "Demo Site Gemini campaign.lnk"  # the second runner's new name
                if label == "a folder":
                    blocker.mkdir()
                else:
                    shutil.copyfile(self.desk / "My Proj's Claude campaign.lnk", blocker)
                    self.set_description(blocker, "conclave-unattended: D:\\elsewhere")
                before = self.snapshot()
                self.write_launchers("Demo Site")
                done = self.shortcut("-Distro", "Ubuntu", "-Icon", str(self.icon))
                self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
                self.assertIn("the shortcut name 'Demo Site Gemini campaign.lnk' is held by a file this project did "
                              "not write", done.stdout)
                self.assertEqual(self.snapshot(), before)
                self.assertEqual([p.name for p in self.desk.iterdir() if ".tmp" in p.name], [])
                self.assertEqual([p.name for p in self.icons.iterdir() if ".tmp" in p.name], [])
                shutil.rmtree(blocker) if blocker.is_dir() else blocker.unlink()
                (self.project / ".conclave" / "launchers.json").unlink()
        self.write_launchers("Demo Site")
        self.install()
        self.assertEqual(self.links(), ["Demo Site Claude campaign.lnk", "Demo Site Gemini campaign.lnk"])
        self.assertEqual((len(self.runner_badges()), len(list(self.icons.iterdir()))), (2, 4))  # Watch, Stop

    def test_a_failure_on_the_second_runner_leaves_the_previous_set_whole(self):
        self.install()  # the folder-named set, from the first icon
        before = self.snapshot()
        icon_module = load("campaign_icon_for_partial", RENDERER)
        other = Path(self._tmp.name) / "other.png"
        other.write_bytes(icon_module.write_png(256, 256, bytearray(bytes((200, 40, 40, 255)) * 256 * 256)))
        # A renderer that draws the first runner's badge and fails on the second's (agy, after claude).
        installed = self.project / "scripts" / "campaign-icon.py"
        installed.write_text("import runpy, sys\n"
                             "if sys.argv[1:2] == ['badge'] and sys.argv[3:4] == ['agy']:\n    sys.exit(1)\n"
                             f"sys.argv[0] = {str(RENDERER)!r}\nrunpy.run_path(sys.argv[0], run_name='__main__')\n",
                             encoding="utf-8")
        self.write_launchers("Demo Site")
        done = self.shortcut("-Distro", "Ubuntu", "-Icon", str(other))
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        self.assertIn("the icon for Demo Site Gemini campaign could not be rendered", done.stdout)
        self.assertEqual(self.snapshot(), before)
        self.assertEqual([p.name for p in [*self.desk.iterdir(), *self.icons.iterdir()] if ".tmp" in p.name], [])
        shutil.copyfile(RENDERER, installed)
        done = self.shortcut("-Distro", "Ubuntu", "-Icon", str(other))
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(self.links(), ["Demo Site Claude campaign.lnk", "Demo Site Gemini campaign.lnk"])
        self.assertEqual(sorted(p.read_bytes() for p in self.runner_badges()),
                         sorted(self.rendered_from(other, agent) for agent in ("agy", "claude")))

    def rendered_from(self, icon, agent):
        out = Path(self._tmp.name) / f"expected-{icon.stem}-{agent}.ico"
        done = run_tree([sys.executable, "-B", str(RENDERER), "badge", str(icon), agent, str(out)],
                              capture_output=True, text=True, timeout=120, creationflags=windowless())
        self.assertEqual(done.returncode, 0, done.stderr)
        return out.read_bytes()

    def test_one_linux_path_in_two_distros_is_two_projects(self):
        self.write_launchers("Alpha")
        other = self.second_project("Beta")
        linux = ["-Icon", str(self.icon), "-Project", "/home/x/demo"]
        self.assertEqual(self.shortcut("-Distro", "Ubuntu", *linux).returncode, 0)
        done = self.shortcut("-Distro", "Debian", *linux, project=other)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        beta = {k: v for k, v in self.snapshot().items() if "Beta" in k}
        self.assertEqual(len(beta), 4)  # two campaign links, Watch and Stop
        beta_icons = sorted(self.read_link(self.desk / f"Beta {r} campaign.lnk")["icon"] for r in ("Gemini", "Claude"))
        theirs = {k: v for k, v in self.snapshot().items() if k.startswith("icons/") and
                  any(Path(i.rsplit(",", 1)[0]).name == k[6:] for i in beta_icons)}
        self.assertEqual(len(theirs), 2)
        self.write_launchers("Alpha Two")
        self.write_profile({**self.PROFILE, "runners": {}})
        for step in (["-Distro", "Ubuntu", *linux], ["-Remove", "-Distro", "Ubuntu", "-Project", "/home/x/demo"]):
            with self.subTest(step[0]):
                done = self.shortcut(*step)
                self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
                now = self.snapshot()
                self.assertEqual({k: now.get(k) for k in beta}, beta)
                self.assertEqual({k: now.get(k) for k in theirs}, theirs)
        self.assertEqual(self.links(), ["Beta Claude campaign.lnk", "Beta Gemini campaign.lnk"])
        done = self.shortcut("-Remove", "-Project", "/home/x/demo")
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        self.assertIn("-Project '/home/x/demo' is a Linux path: -Distro names the distro it is in", done.stdout)

    def test_links_and_badges_under_the_old_mark_and_key_are_replaced(self):
        self.extra_env = {"STUB_WSL_MISSING": "Debian:/home/x/Demo"}  # no other distro has the path
        # What the script wrote before for `-Project /home/x/Demo` (a capital, which Linux keeps): the folder-named set
        # under the path as typed, and its badges under that path's key, which the old script took in lower case.
        linux = ["-Distro", "Ubuntu", "-Icon", str(self.icon), "-Project", "/home/x/Demo"]
        self.assertEqual(self.shortcut(*linux).returncode, 0)
        old_key = hashlib.sha256(b"/home/x/demo").hexdigest()[:8]
        for link in self.desk.glob("*.lnk"):
            badge = Path(self.read_link(link)["icon"].rsplit(",", 1)[0])
            old = badge.with_name(f"{badge.name.split('-')[0]}-{badge.name.split('-')[1]}-{old_key}.ico")
            badge.rename(old)
            self.set_description(link, "conclave-unattended: /home/x/Demo", icon=str(old))
        self.assertEqual(self.links(), ["Demo Claude campaign.lnk", "Demo Gemini campaign.lnk"])
        # A titled link under the old mark is this project's too, so it is replaced, not refused as another's.
        shutil.copyfile(self.desk / "Demo Claude campaign.lnk", self.desk / "Demo Site Claude campaign.lnk")
        self.write_launchers("Demo Site")
        done = self.shortcut(*linux)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(self.links(), ["Demo Site Claude campaign.lnk", "Demo Site Gemini campaign.lnk"])
        self.assertEqual(list(self.icons.glob(f"campaign-*-{old_key}.ico")), [])  # they go with the rest
        new_key = hashlib.sha256(b"\\\\wsl.localhost\\ubuntu\\home\\x\\Demo").hexdigest()[:8]
        mark = self.read_link(self.desk / "Demo Site Claude campaign.lnk")["description"]
        self.assertEqual(mark, "conclave-unattended: \\\\wsl.localhost\\Ubuntu\\home\\x\\Demo")
        self.assertTrue((self.icons / f"campaign-claude-{new_key}.ico").is_file())

    def test_an_old_linux_path_mark_of_another_distro_is_never_this_projects(self):
        # Round 1: the old mark of `-Project /home/x/demo` names no distro; its link's arguments do.
        debian = ["-Distro", "Debian", "-Icon", str(self.icon), "-Project", "/home/x/demo"]
        self.assertEqual(self.shortcut(*debian).returncode, 0)
        for link in self.desk.glob("*.lnk"):
            self.set_description(link, "conclave-unattended: /home/x/demo")
        before = self.snapshot()
        done = self.shortcut("-Remove", "-Distro", "Ubuntu", "-Project", "/home/x/demo")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(self.snapshot(), before)
        done = self.shortcut("-Remove", "-Distro", "Debian", "-Project", "/home/x/demo")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual((self.links(), list(self.icons.iterdir())), ([], []))

    def test_linux_paths_that_differ_only_in_case_are_two_projects(self):
        # Round 1: Linux tells /home/x/Demo from /home/x/demo, so their marks and keys differ by case.
        self.write_launchers("Alpha")
        other = self.second_project("Beta")
        upper = ["-Distro", "Ubuntu", "-Icon", str(self.icon), "-Project", "/home/x/Demo"]
        lower = ["-Distro", "Ubuntu", "-Icon", str(self.icon), "-Project", "/home/x/demo"]
        self.assertEqual(self.shortcut(*upper).returncode, 0)
        alpha = self.snapshot()
        for step in (lower, ["-Remove", "-Distro", "Ubuntu", "-Project", "/home/x/demo"]):
            with self.subTest(step[0]):
                done = self.shortcut(*step, project=other)
                self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
                now = self.snapshot()
                self.assertEqual({k: now.get(k) for k in alpha}, alpha)

    def test_a_link_held_open_while_the_set_is_replaced_leaves_the_previous_set(self):
        # Round 1: the second runner's link cannot be moved aside while another program holds it open.
        self.install()
        before = self.snapshot()
        icon_module = load("campaign_icon_for_held", RENDERER)
        other = Path(self._tmp.name) / "other.png"
        other.write_bytes(icon_module.write_png(256, 256, bytearray(bytes((200, 40, 40, 255)) * 256 * 256)))
        with open(self.desk / "My Proj's Gemini campaign.lnk", "rb"):  # open without delete sharing
            done = self.shortcut("-Distro", "Ubuntu", "-Icon", str(other))
            self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
            self.assertIn("the shortcuts could not be put in place", done.stdout)
            self.assertIn("the previous ones are kept", done.stdout)
            self.assertEqual(self.snapshot(), before)
            self.assertEqual([p.name for p in [*self.desk.iterdir(), *self.icons.iterdir()]
                              if ".tmp" in p.name or p.name.endswith(".old")], [])
        done = self.shortcut("-Distro", "Ubuntu", "-Icon", str(other))
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(sorted(p.read_bytes() for p in self.runner_badges()),
                         sorted(self.rendered_from(other, agent) for agent in ("agy", "claude")))

    def test_a_locked_stale_link_leaves_the_previous_set_and_a_retry_writes_the_new_one(self):
        # Completion gap: a rename retires the old links in the same change as it writes the new ones.
        self.install()
        before = self.snapshot()
        self.write_launchers("Demo Site")
        with open(self.desk / "My Proj's Gemini campaign.lnk", "rb"):  # a link the rename retires, held open
            done = self.shortcut("-Distro", "Ubuntu", "-Icon", str(self.icon))
            self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
            self.assertIn("the previous ones are kept", done.stdout)
            self.assertEqual(self.snapshot(), before)
            self.assertEqual(self.links(), ["My Proj's Claude campaign.lnk", "My Proj's Gemini campaign.lnk"])
            self.assertEqual([p.name for p in [*self.desk.iterdir(), *self.icons.iterdir()]
                              if ".tmp" in p.name or p.name.endswith(".old")], [])
        self.install()
        self.assertEqual(self.links(), ["Demo Site Claude campaign.lnk", "Demo Site Gemini campaign.lnk"])
        self.assertEqual((len(self.runner_badges()), len(list(self.icons.iterdir()))), (2, 4))  # Watch, Stop

    def test_a_link_another_installer_writes_while_this_one_works_is_kept(self):
        # Completion gap: the name was free at the check; another project's link takes it before the swap.
        foreign = Path(self._tmp.name) / "foreign.lnk"
        self.install()
        shutil.copyfile(self.desk / "My Proj's Gemini campaign.lnk", foreign)
        self.set_description(foreign, "conclave-unattended: D:\\elsewhere")
        self.assertEqual(self.shortcut("-Remove").returncode, 0)
        target = self.desk / "Demo Site Gemini campaign.lnk"
        installed = self.project / "scripts" / "campaign-icon.py"
        installed.write_text("import runpy, shutil, sys\n"
                             "if sys.argv[1:2] == ['badge'] and sys.argv[3:4] == ['agy']:\n"
                             f"    shutil.copyfile({str(foreign)!r}, {str(target)!r})\n"
                             f"sys.argv[0] = {str(RENDERER)!r}\nrunpy.run_path(sys.argv[0], run_name='__main__')\n",
                             encoding="utf-8")
        self.write_launchers("Demo Site")
        done = self.shortcut("-Distro", "Ubuntu", "-Icon", str(self.icon))
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        self.assertIn("was written by another project while this run was working", done.stdout)
        self.assertEqual(self.links(), ["Demo Site Gemini campaign.lnk"])
        self.assertEqual(target.read_bytes(), foreign.read_bytes())
        self.assertEqual(list(self.icons.glob("*.ico")), [])
        self.assertEqual([p.name for p in [*self.desk.iterdir(), *self.icons.iterdir()]
                          if ".tmp" in p.name or p.name.endswith(".old")], [])

    DEAD = 4000000  # beyond any process id Windows hands out: the run that wrote the journal is gone

    def stopped_rename(self, state):
        """The files and journal a rename to "Demo Site" leaves when it stops in `state`: every old file moved aside,
        the first runner's new badge and link moved in, the second's still under their temporary names."""
        shown = self.read_link(self.desk / "My Proj's Claude campaign.lnk")["icon"].rsplit(",", 1)[0]
        key = Path(shown).stem.split("-")[-1]  # this project's key, from the badge its own link shows
        aside, moves = [], []
        new_link = (self.desk / "My Proj's Claude campaign.lnk").read_bytes()  # a link of this project's
        # A run moves aside this project's links and its own key's badges; an old key's badge is never moved aside.
        for path in (*self.icons.glob(f"campaign-*-{key}.ico"), *self.desk.glob("*.lnk")):
            old = path.with_name(f"{path.name}.{self.DEAD}.old")
            path.rename(old)
            aside.append({"to": str(path), "old": str(old)})
        for n, (agent, label) in enumerate((("claude", "Claude"), ("agy", "Gemini"))):
            badge = self.icons / f"campaign-{agent}-{key}.ico"
            link = self.desk / f"Demo Site {label} campaign.lnk"
            for final, temp, data in ((badge, badge.with_name(f"{badge.name}.{self.DEAD}.tmp.ico"), b"new"),
                                      (link, link.with_name(f"{link.stem}.{self.DEAD}.tmp.lnk"), new_link)):
                (final if n == 0 else temp).write_bytes(data)  # the first runner is moved in, the second is not
                moves.append({"from": str(temp), "to": str(final)})
        journal = self.state / f"campaign-{key}.{self.DEAD}.journal.json"
        journal.write_text(json.dumps({"state": state, "temps": [m["from"] for m in moves], "moves": moves,
                                       "aside": aside}), encoding="utf-8")
        return journal

    def test_a_run_stopped_while_swapping_is_rolled_back_whole(self):
        # Reconciliation 1: a stopped rename must leave the previous set, never both titles, old key's badge included.
        self.install()
        (self.icons / "campaign-claude-0badc0de.ico").write_bytes(b"old key")
        before = self.snapshot()
        journal = self.stopped_rename("swapping")
        done = self.shortcut("-Distro", "Arch", "-Icon", str(self.icon))  # any run, even one that then refuses
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        self.assertIn("restored: ", done.stdout)
        self.assertFalse(journal.exists())
        self.assertEqual(self.snapshot(), before)

    def test_a_recovery_stopped_part_way_is_finished_by_the_next_run(self):
        # Reconciliation 2: recovery restored the original badge, then stopped before deleting the journal.
        self.install()
        before = self.snapshot()
        journal = self.stopped_rename("swapping")
        record = json.loads(journal.read_text(encoding="utf-8"))
        badge = record["moves"][0]  # the first runner's badge: moved in over the original, which is moved aside
        replaced = next(a for a in record["aside"] if a["to"] == badge["to"])
        Path(badge["to"]).unlink()                          # the first recovery took the new badge out
        Path(replaced["old"]).rename(replaced["to"])        # and put the original back, then stopped
        done = self.shortcut("-Distro", "Arch", "-Icon", str(self.icon))
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        self.assertFalse(journal.exists())
        self.assertEqual(self.snapshot(), before)

    def test_a_journal_naming_files_outside_the_projects_links_is_not_played(self):
        # Reconciliation 2: a journal is a claim; one planted in a shared icon folder cannot reach anything else.
        self.install()
        outside = Path(self._tmp.name) / "precious.txt"
        outside.write_text("keep me\n", encoding="utf-8")
        (self.icons / "campaign-claude-0badc0de.ico").write_bytes(b"another project's")
        (self.desk / f"notes.txt.{self.DEAD}.old").write_text("someone's\n", encoding="utf-8")  # not a link's
        claude = self.desk / "My Proj's Claude campaign.lnk"
        key = Path(self.read_link(claude)["icon"].rsplit(",", 1)[0]).stem.split("-")[-1]
        before = self.snapshot()
        for label, record in (
                ("a move to a file elsewhere", {"state": "swapping", "temps": [], "aside": [],
                                                "moves": [{"from": str(outside) + ".1.tmp.txt", "to": str(outside)}]}),
                ("a temporary file elsewhere", {"state": "writing", "temps": [str(outside)], "moves": [], "aside": []}),
                ("another key's badge moved in", {"state": "swapping", "temps": [], "aside": [], "moves": [
                    {"from": str(self.icons / f"campaign-claude-0badc0de.ico.{self.DEAD}.tmp.ico"),
                     "to": str(self.icons / "campaign-claude-0badc0de.ico")}]}),
                ("a file in the desktop folder that is no link", {"state": "swapping", "temps": [], "moves": [],
                 "aside": [{"to": str(self.desk / "notes.txt"),
                            "old": str(self.desk / f"notes.txt.{self.DEAD}.old")}]}),
                ("a link moved aside under another run's name", {"state": "committed", "temps": [], "moves": [],
                                                                 "aside": [{"to": str(claude), "old": str(claude)}]})):
            with self.subTest(label):
                journal = self.state / f"campaign-{key}.{self.DEAD}.journal.json"
                journal.write_text(json.dumps(record), encoding="utf-8")
                done = self.shortcut("-Distro", "Arch", "-Icon", str(self.icon))
                self.assertIn("names files outside this project's links; it is left as it is", done.stdout)
                self.assertEqual(outside.read_text(encoding="utf-8"), "keep me\n")
                self.assertTrue(journal.exists())
                journal.unlink()
                self.assertEqual(self.snapshot(), before)

    def foreign_link(self, path):
        """Another project's link at `path`."""
        shutil.copyfile(next(self.desk.glob("*.lnk.*.old"), None) or next(self.desk.glob("*.lnk")), path)
        self.set_description(path, "conclave-unattended: D:\\elsewhere")

    def test_recovery_never_takes_a_link_another_project_put_at_a_vacated_name(self):
        # Reconciliation 3: an original moved aside whose name another project's link took since.
        self.install()
        journal = self.stopped_rename("swapping")
        taken = self.desk / "My Proj's Claude campaign.lnk"  # vacated: its original is moved aside
        self.foreign_link(taken)
        foreign = taken.read_bytes()
        stopped = self.snapshot()
        # Reconciliation 4 (chain 2): the journal is held whole, so no mode can take the original its name lost.
        for args in (["-Distro", "Ubuntu", "-Icon", str(self.icon)], ["-Remove"]):
            with self.subTest(args[0]):
                done = self.shortcut(*args)
                self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
                self.assertIn(f"names {taken}, which is not this project's", done.stdout)
                self.assertEqual((self.snapshot(), journal.exists()), (stopped, True))
                self.assertEqual(taken.read_bytes(), foreign)

    def test_a_journal_naming_another_projects_file_at_a_moved_in_name_holds_every_change(self):
        # Completion reconciliation (chain 2): a run cannot tell that race from a planted record, so it plays neither.
        self.install()
        journal = self.stopped_rename("swapping")
        moved_in = self.desk / "Demo Site Claude campaign.lnk"  # moved in by the stopped run; another's is there now
        moved_in.unlink()
        self.foreign_link(moved_in)
        stopped = self.snapshot()
        for args in (["-Distro", "Ubuntu", "-Icon", str(self.icon)], ["-Remove"]):
            with self.subTest(args[0]):
                done = self.shortcut(*args)
                self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
                self.assertIn(f"names {moved_in}, which is not this project's", done.stdout)
                self.assertEqual((self.snapshot(), journal.exists()), (stopped, True))

    def test_a_rollback_that_cannot_put_an_original_back_keeps_its_journal(self):
        # Reconciliation 3: another installer takes a name this run vacated, so its move in and its rollback both fail.
        self.install()
        target = self.desk / "My Proj's Claude campaign.lnk"
        foreign = Path(self._tmp.name) / "foreign.lnk"
        shutil.copyfile(target, foreign)
        self.set_description(foreign, "conclave-unattended: D:\\elsewhere")
        for _ in range(5):  # the other installer must land between this run's move aside and its move in
            stop, won = threading.Event(), []

            def take():
                while not stop.is_set():
                    if any(self.desk.glob(f"{target.name}.*.old")):
                        try:
                            with open(target, "xb") as out:
                                out.write(foreign.read_bytes())
                            won.append(True)
                        except FileExistsError:  # the run moved in first: this attempt lost the race, and retries
                            pass
                        return
                    time.sleep(0.0005)

            watcher = threading.Thread(target=take)
            watcher.start()
            done = self.shortcut("-Distro", "Ubuntu", "-Icon", str(self.icon))
            stop.set()
            watcher.join()
            if won and done.returncode == 1:
                break
            self.install()  # it lost the race: start again from a clean set
        else:
            self.fail("the other installer never landed between the move aside and the move in")
        self.assertIn("could not all be put back", done.stdout)
        self.assertEqual(target.read_bytes(), foreign.read_bytes())
        journals = list(self.state.glob("campaign-*.journal.json"))
        self.assertEqual(len(journals), 1, done.stdout)
        self.assertTrue(any(self.desk.glob(f"{target.name}.*.old")))

    def test_a_run_stopped_after_its_swap_is_finished(self):
        self.install()
        journal = self.stopped_rename("committed")
        record = json.loads(journal.read_text(encoding="utf-8"))
        for m in record["moves"]:  # a committed run moved them all in
            if Path(m["from"]).exists():
                Path(m["from"]).rename(m["to"])
        done = self.shortcut("-Distro", "Arch", "-Icon", str(self.icon))
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        self.assertFalse(journal.exists())
        self.assertEqual(self.links(), ["Demo Site Claude campaign.lnk", "Demo Site Gemini campaign.lnk"])
        self.assertEqual([p.name for p in [*self.desk.iterdir(), *self.icons.iterdir()]
                          if ".tmp" in p.name or p.name.endswith(".old")], [])

    def started(self, pid):
        """When the process `pid` started, as the script records it in a journal."""
        done = run_tree([PWSH, "-NoProfile", "-NonInteractive", "-Command",
                               f"(Get-Process -Id {pid}).StartTime.ToUniversalTime().Ticks"],
                              capture_output=True, text=True, timeout=60, creationflags=windowless())
        return done.stdout.strip()

    def test_a_journal_of_a_run_still_going_is_left_alone(self):
        self.install()
        before = self.snapshot()
        key = next(self.icons.glob("campaign-claude-*.ico")).stem.split("-")[-1]
        journal = self.state / f"campaign-{key}.{os.getpid()}.journal.json"  # this test's own process: alive
        journal.write_text(json.dumps({"state": "swapping", "started": self.started(os.getpid()), "temps": [],
                                       "moves": [], "aside": []}), encoding="utf-8")
        done = self.shortcut("-Distro", "Arch", "-Icon", str(self.icon))
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        self.assertTrue(journal.exists())
        journal.unlink()
        self.assertEqual(self.snapshot(), before)

    def test_a_journal_whose_id_windows_gave_another_process_is_played(self):
        # Reconciliation 10: the id is this test's process, but the run that wrote the journal started long before.
        self.install()
        before = self.snapshot()
        journal = self.stopped_rename("swapping")
        live = journal.with_name(journal.name.replace(f".{self.DEAD}.", f".{os.getpid()}."))
        record = json.loads(journal.read_text(encoding="utf-8"))
        record["started"] = "631139040000000000"  # 2001-01-01, long before this process started
        journal.unlink()
        for entry in record["moves"]:
            entry["from"] = entry["from"].replace(f".{self.DEAD}.", f".{os.getpid()}.")
        for entry in record["aside"]:
            entry["old"] = entry["old"].replace(f".{self.DEAD}.", f".{os.getpid()}.")
        record["temps"] = [m["from"] for m in record["moves"]]
        for path in [*self.desk.iterdir(), *self.icons.iterdir()]:
            if f".{self.DEAD}." in path.name:
                path.rename(path.with_name(path.name.replace(f".{self.DEAD}.", f".{os.getpid()}.")))
        live.write_text(json.dumps(record), encoding="utf-8")
        done = self.shortcut("-Distro", "Arch", "-Icon", str(self.icon))
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        self.assertIn("restored: ", done.stdout)
        self.assertEqual((self.snapshot(), self.journal_of()), (before, []))

    def test_a_stopped_runs_journal_under_this_runs_own_id_is_played(self):
        # Reconciliation 11: Windows can give a stopped run's id to the run that retries it.
        self.install()
        before = self.snapshot()
        self.stopped_rename("swapping")
        # One PowerShell process gives every file of the stopped run its own id, then runs the script itself, so the
        # journal carries exactly the retrying run's id.
        script = ("$dead = '.' + $env:DEAD + '.'; $own = '.' + $PID + '.'; "
                  "foreach ($folder in @($env:DESK, $env:ICONS, $env:STATE)) { "
                  "Get-ChildItem -LiteralPath $folder -File | Where-Object { $_.Name.Contains($dead) } | "
                  "ForEach-Object { Rename-Item -LiteralPath $_.FullName -NewName $_.Name.Replace($dead, $own) } }; "
                  "$journal = Get-ChildItem -LiteralPath $env:STATE -Filter '*.journal.json' | Select-Object -First 1; "
                  "$text = [IO.File]::ReadAllText($journal.FullName).Replace($dead, $own); "
                  "[IO.File]::WriteAllText($journal.FullName, $text); "
                  "& $env:SCRIPT -ShortcutDir $env:DESK -IconDir $env:ICONS -Wsl $env:WSL "
                  "-Python $env:PY "
                  "-Distro Arch -Icon $env:ICON")
        done = run_tree([PWSH, "-NoProfile", "-NonInteractive", "-Command", script], capture_output=True,
                              text=True, timeout=180, creationflags=windowless(),
                              env={**self.env, "DEAD": str(self.DEAD), "DESK": str(self.desk), "ICONS": str(self.icons),
                                   "STATE": str(self.state),
                                   "SCRIPT": str(self.project / "scripts" / "unattended-shortcut.ps1"),
                                   "WSL": str(self.wsl), "PY": sys.executable, "ICON": str(self.icon)})
        self.assertIn("restored: ", done.stdout, done.stdout + done.stderr)
        self.assertEqual((self.snapshot(), self.journal_of()), (before, []))

    def drop_controls(self, desk):
        """The old script wrote no Watch or Stop link: take them and their badges off, to stand for its set."""
        for link in [p for p in desk.glob("*.lnk") if p.name.endswith(self.CONTROL_LINKS)]:
            Path(self.read_link(link)["icon"].rsplit(",", 1)[0]).unlink(missing_ok=True)
            link.unlink()

    def legacy_set(self, linux):
        """This project's set for `linux` as the old script wrote it: the old mark, badges under the old key."""
        self.assertEqual(self.shortcut(*linux).returncode, 0)
        self.drop_controls(self.desk)
        old_key = hashlib.sha256(b"/home/x/demo").hexdigest()[:8]
        for link in self.desk.glob("*.lnk"):
            badge = Path(self.read_link(link)["icon"].rsplit(",", 1)[0])
            old = badge.with_name(f"{badge.name.split('-')[0]}-{badge.name.split('-')[1]}-{old_key}.ico")
            badge.rename(old)
            self.set_description(link, "conclave-unattended: /home/x/Demo", icon=str(old))
        return old_key

    def test_an_old_key_badge_held_during_a_migration_fails_it_whole_and_goes_later(self):
        self.extra_env = {"STUB_WSL_MISSING": "Debian:/home/x/Demo"}  # no other distro has the path
        # Reconciliation 12 and 13: the old key's badges go in the same change as the links that show them.
        linux = ["-Distro", "Ubuntu", "-Icon", str(self.icon), "-Project", "/home/x/Demo"]
        old_key = self.legacy_set(linux)
        before = self.snapshot()
        held = self.icons / f"campaign-claude-{old_key}.ico"
        with open(held, "rb"):  # held open without delete sharing through the migration
            done = self.shortcut(*linux)
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        self.assertIn("the previous ones are kept", done.stdout)
        self.assertEqual(self.snapshot(), before)
        done = self.shortcut(*linux)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(list(self.icons.glob(f"campaign-*-{old_key}.ico")), [])

    def test_remove_with_an_old_key_badge_held_keeps_its_link_and_finishes_later(self):
        self.extra_env = {"STUB_WSL_MISSING": "Debian:/home/x/Demo"}  # no other distro has the path
        # Reconciliation 12 and 13: a badge that cannot go keeps the link through which the next -Remove finds it.
        linux = ["-Distro", "Ubuntu", "-Icon", str(self.icon), "-Project", "/home/x/Demo"]
        old_key = self.legacy_set(linux)
        held = self.icons / f"campaign-claude-{old_key}.ico"
        remove = ["-Remove", "-Distro", "Ubuntu", "-Project", "/home/x/Demo"]
        with open(held, "rb"):  # held open without delete sharing
            done = self.shortcut(*remove)
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        self.assertIn("they stay as they were; run -Remove again", done.stdout)
        self.assertEqual(self.links(), ["Demo Claude campaign.lnk", "Demo Gemini campaign.lnk"])
        done = self.shortcut(*remove)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual((self.links(), sorted(p.name for p in self.icons.iterdir())), ([], []))

    def test_a_badge_another_distros_old_link_shows_on_the_users_desktop_stays(self):
        self.extra_env = {"STUB_WSL_MISSING": "Debian:/home/x/Demo"}  # no other distro has the path
        # Reconciliation 14: two distros' old sets can share one badge from different desktop folders.
        self.other_desktop = Path(self._tmp.name) / "users-desktop"
        linux = ["-Distro", "Ubuntu", "-Icon", str(self.icon), "-Project", "/home/x/Demo"]
        old_key = self.legacy_set(linux)
        shown = self.icons / f"campaign-claude-{old_key}.ico"
        self.other_desktop.mkdir()
        theirs = self.other_desktop / "Demo Claude campaign.lnk"  # Debian's old link, on the user's desktop
        shutil.copyfile(self.desk / "Demo Claude campaign.lnk", theirs)
        self.set_description(theirs, "conclave-unattended: /home/x/Demo", icon=str(shown))
        done = self.shortcut(*linux)  # Ubuntu moves off the old name
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertTrue(shown.exists(), "the badge Debian's old link shows was removed")
        self.assertFalse((self.icons / f"campaign-agy-{old_key}.ico").exists())
        done = self.shortcut("-Remove", "-Distro", "Ubuntu", "-Project", "/home/x/Demo")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertTrue(shown.exists(), "-Remove removed the badge Debian's old link shows")

    def test_an_old_key_badge_stays_while_another_distro_has_the_same_linux_path(self):
        # Reconciliation 15: another distro's old link may sit in any folder; WSL says whether it can exist at all.
        linux = ["-Distro", "Ubuntu", "-Icon", str(self.icon), "-Project", "/home/x/Demo"]
        old_key = self.legacy_set(linux)  # the stand-in says Debian has /home/x/Demo too
        done = self.shortcut(*linux)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(len(list(self.icons.glob(f"campaign-*-{old_key}.ico"))), 2)
        self.assertIn(["-d", "Debian", "--exec", "find", "/", "-mindepth", "1", "-maxdepth", "1", "-iname", "home",
                       "-print"], self.calls())
        done = self.shortcut("-Remove", "-Distro", "Ubuntu", "-Project", "/home/x/Demo")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(len(list(self.icons.glob(f"campaign-*-{old_key}.ico"))), 2)

    def test_an_old_key_badge_stays_while_another_distro_has_the_path_in_another_case(self):
        # Reconciliation 16: the old key took the path in lower case, so /home/x/demo in Debian shared it.
        self.extra_env = {"STUB_WSL_CASE": "Debian:/home/x/demo"}  # Debian has the path, spelt otherwise
        linux = ["-Distro", "Ubuntu", "-Icon", str(self.icon), "-Project", "/home/x/Demo"]
        old_key = self.legacy_set(linux)
        done = self.shortcut(*linux)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(len(list(self.icons.glob(f"campaign-*-{old_key}.ico"))), 2)

    def test_an_old_key_badge_stays_while_wsl_cannot_say_whether_another_distro_has_the_path(self):
        self.extra_env = {"STUB_WSL_MISSING": "Debian:/home/x/Demo", "STUB_WSL_UNSURE": "Debian"}
        linux = ["-Distro", "Ubuntu", "-Icon", str(self.icon), "-Project", "/home/x/Demo"]
        old_key = self.legacy_set(linux)
        done = self.shortcut(*linux)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(len(list(self.icons.glob(f"campaign-*-{old_key}.ico"))), 2)

    def test_a_same_named_badge_in_another_icon_folder_is_not_this_ones(self):
        # Reconciliation 15: a link elsewhere showing a badge of the same name in another folder shares nothing.
        self.extra_env = {"STUB_WSL_MISSING": "Debian:/home/x/Demo"}
        self.other_desktop = Path(self._tmp.name) / "users-desktop"
        linux = ["-Distro", "Ubuntu", "-Icon", str(self.icon), "-Project", "/home/x/Demo"]
        old_key = self.legacy_set(linux)
        elsewhere = Path(self._tmp.name) / "icons2" / f"campaign-claude-{old_key}.ico"
        elsewhere.parent.mkdir()
        elsewhere.write_bytes(b"another folder's")
        self.other_desktop.mkdir()
        theirs = self.other_desktop / "Demo Claude campaign.lnk"
        shutil.copyfile(self.desk / "Demo Claude campaign.lnk", theirs)
        self.set_description(theirs, "conclave-unattended: D:\\elsewhere", icon=str(elsewhere))
        done = self.shortcut(*linux)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(list(self.icons.glob(f"campaign-*-{old_key}.ico")), [])
        self.assertTrue(elsewhere.exists())

    def test_an_old_key_badge_stays_while_this_distro_has_the_path_spelt_otherwise_too(self):
        # Plan review 2: /home/x/Demo and /home/x/demo are two projects in one distro, and the old key was both's.
        self.extra_env = {"STUB_WSL_MISSING": "Debian:/home/x/Demo", "STUB_WSL_CASE": "Ubuntu:+/home/x/demo"}
        linux = ["-Distro", "Ubuntu", "-Icon", str(self.icon), "-Project", "/home/x/Demo"]
        old_key = self.legacy_set(linux)
        done = self.shortcut(*linux)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(self.links(), ["Demo Claude campaign.lnk", "Demo Gemini campaign.lnk"])
        self.assertEqual(self.read_link(self.desk / "Demo Claude campaign.lnk")["description"],
                         "conclave-unattended: \\\\wsl.localhost\\Ubuntu\\home\\x\\Demo")  # the link migrated
        self.assertEqual(len(list(self.icons.glob(f"campaign-*-{old_key}.ico"))), 2)  # only the badges stay

    def hold_lock(self):
        """Another run of the project, holding its lock until the test ends."""
        key = Path(self.read_link(self.desk / "My Proj's Claude campaign.lnk")["icon"].rsplit(",", 1)[0]).stem
        lock = self.state / f"campaign-{key.split('-')[-1]}.lock"
        holder = start_tree([PWSH, "-NoProfile", "-NonInteractive", "-Command",
                                   "$f = [IO.FileStream]::new($env:LOCK, 'OpenOrCreate', 'ReadWrite', 'None'); "
                                   "[Console]::Out.WriteLine('held'); [Console]::Out.Flush(); Start-Sleep 120"],
                                  stdout=subprocess.PIPE, text=True, env={**os.environ, "LOCK": str(lock)},
                                  creationflags=windowless())
        self.addCleanup(holder.stdout.close)
        self.addCleanup(end_tree, holder)
        self.assertEqual(holder.stdout.readline().strip(), "held")
        return holder, lock

    def test_a_second_run_of_the_project_waits_for_the_first(self):
        # Plan review 2: two runs of one project never change its links at once.
        self.install()
        before = self.snapshot()
        holder, lock = self.hold_lock()
        for args in (["-Distro", "Ubuntu", "-Icon", str(self.icon)], ["-Remove"]):
            with self.subTest(args[0]):
                done = self.shortcut(*args)
                self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
                self.assertIn(f"another run of this project holds {lock}", done.stdout)
                self.assertEqual(self.snapshot(), before)
        subprocess.run(["taskkill", "/T", "/F", "/PID", str(holder.pid)], capture_output=True,
                       creationflags=windowless())
        holder.wait(30)
        self.install()
        self.assertEqual(self.shortcut("-Remove").returncode, 0)
        self.assertFalse(lock.exists())

    def test_a_journal_that_cannot_be_read_holds_every_change(self):
        # Plan review 2: an unread journal may name a swap not finished; nothing is written over it.
        self.install()
        key = Path(self.read_link(self.desk / "My Proj's Claude campaign.lnk")["icon"].rsplit(",", 1)[0]).stem
        journal = self.state / f"campaign-{key.split('-')[-1]}.{self.DEAD}.journal.json"
        journal.write_text("{not json", encoding="utf-8")
        before = self.snapshot()
        for args in (["-Distro", "Ubuntu", "-Icon", str(self.icon)], ["-Remove"]):
            with self.subTest(args[0]):
                done = self.shortcut(*args)
                self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
                self.assertIn(f"{journal.name} cannot be read", done.stdout)  # the folder may be spelt long
                self.assertEqual(self.snapshot(), before)

    def test_a_renamed_link_is_still_this_projects(self):
        # Plan review 2: a link is found by its mark, whatever the operator named it.
        self.install()
        (self.desk / "My Proj's Claude campaign.lnk").rename(self.desk / "Start work.lnk")
        self.foreign_link(self.desk / "Other.lnk")
        self.install()
        self.assertEqual(self.links(), ["My Proj's Claude campaign.lnk", "My Proj's Gemini campaign.lnk", "Other.lnk"])
        (self.desk / "My Proj's Claude campaign.lnk").rename(self.desk / "Start work.lnk")
        done = self.shortcut("-Remove")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual((self.links(), list(self.icons.glob("*.ico"))), (["Other.lnk"], []))

    def test_a_remove_stopped_after_its_first_move_is_put_back_by_the_next_run(self):
        # Plan review 2: -Remove keeps a journal, so a stop never leaves a link without its icon unrecorded.
        self.install()
        before = self.snapshot()
        key = Path(self.read_link(self.desk / "My Proj's Claude campaign.lnk")["icon"].rsplit(",", 1)[0]).stem
        files = [*sorted(self.desk.glob("*.lnk")), *sorted(self.icons.glob("campaign-*.ico"))]
        aside = [{"to": str(f), "old": f"{f}.{self.DEAD}.old"} for f in files]
        files[0].rename(aside[0]["old"])  # the first link is moved aside, then the run stops
        journal = self.state / f"campaign-{key.split('-')[-1]}.{self.DEAD}.journal.json"
        journal.write_text(json.dumps({"state": "swapping", "temps": [], "moves": [], "aside": aside}),
                           encoding="utf-8")
        done = self.shortcut("-Distro", "Arch", "-Icon", str(self.icon))  # any next run, even a refused one
        self.assertIn(f"restored: {files[0]}", done.stdout)
        self.assertEqual((self.snapshot(), self.journal_of()), (before, []))

    def project_key(self):
        shown = self.read_link(self.desk / "My Proj's Claude campaign.lnk")["icon"].rsplit(",", 1)[0]
        return Path(shown).stem.split("-")[-1]

    def test_a_link_held_open_stops_the_run_before_anything_changes(self):
        # Round 1 (chain 2): a link that cannot be read may be this project's; nothing is taken around it.
        self.install()
        before = self.snapshot()
        held = self.desk / "My Proj's Gemini campaign.lnk"
        script = ("$f = [IO.FileStream]::new($env:HELD, 'Open', 'Read', 'None'); [Console]::Out.WriteLine('held'); "
                  "[Console]::Out.Flush(); Start-Sleep 120")
        holder = start_tree([PWSH, "-NoProfile", "-NonInteractive", "-Command", script], stdout=subprocess.PIPE,
                                  text=True, env={**os.environ, "HELD": str(held)}, creationflags=windowless())
        self.addCleanup(holder.stdout.close)
        self.addCleanup(end_tree, holder)
        self.assertEqual(holder.stdout.readline().strip(), "held")
        for args in (["-Remove"], ["-Distro", "Ubuntu", "-Icon", str(self.icon)]):
            with self.subTest(args[0]):
                done = self.shortcut(*args)
                self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
                self.assertIn(f"{held.name} cannot be read now (held open by another program)", done.stdout)
                self.assertEqual(self.snapshot(skip=(held.name,)),
                                 {k: v for k, v in before.items() if k != f"desk/{held.name}"})
                self.assertTrue(held.exists())

    def test_recovery_never_restores_another_projects_link(self):
        # Round 1 (chain 2): a journal naming another project's moved-aside link cannot put it back.
        self.install()
        key = self.project_key()
        theirs = self.desk / "Other campaign.lnk"
        self.foreign_link(theirs)
        aside = theirs.with_name(f"{theirs.name}.{self.DEAD}.old")
        theirs.rename(aside)  # another project's run stopped with it moved aside
        journal = self.state / f"campaign-{key}.{self.DEAD}.journal.json"
        self.state.mkdir(exist_ok=True)
        journal.write_text(json.dumps({"state": "swapping", "temps": [], "moves": [],
                                       "aside": [{"to": str(theirs), "old": str(aside)}]}), encoding="utf-8")
        self.shortcut("-Distro", "Arch", "-Icon", str(self.icon))
        self.assertTrue(aside.exists())
        self.assertFalse(theirs.exists())

    def test_one_unreadable_journal_holds_a_valid_one_too(self):
        # Round 1 (chain 2): every journal is checked before any is played.
        self.install()
        journal = self.stopped_rename("swapping")
        stopped = self.snapshot()
        truncated = journal.with_name(journal.name.replace(f".{self.DEAD}.", f".{self.DEAD + 4}."))
        truncated.write_text('{"state": "swapping", "aside": [', encoding="utf-8")
        for args in (["-Remove"], ["-Distro", "Ubuntu", "-Icon", str(self.icon)]):
            with self.subTest(args[0]):
                done = self.shortcut(*args)
                self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
                self.assertIn(f"{truncated.name} cannot be read", done.stdout)
                self.assertEqual(self.snapshot(), stopped)
                self.assertTrue(journal.exists())

    def test_a_stopped_run_is_found_whatever_icon_folder_the_next_run_is_given(self):
        # Round 1 (chain 2): the lock and the journals are the project's, not one icon folder's.
        self.install()
        before = self.snapshot()
        journal = self.stopped_rename("swapping")
        record = json.loads(journal.read_text(encoding="utf-8"))
        record.update(desk=str(self.desk), icons=str(self.icons))  # where the stopped run worked
        journal.write_text(json.dumps(record), encoding="utf-8")
        other = Path(self._tmp.name) / "other-icons"
        script = str(self.project / "scripts" / "unattended-shortcut.ps1")
        command = [PWSH, "-NoProfile", "-NonInteractive", "-File", script, "-ShortcutDir", str(self.desk),
                   "-IconDir", str(other), "-Desktops",
                   str(Path(self._tmp.name) / "desktops"), "-Wsl", str(self.wsl), "-Python", sys.executable,
                   "-Distro", "Arch", "-Icon", str(self.icon)]
        done = run_tree(command, capture_output=True, text=True, timeout=180, env=self.env,
                              creationflags=windowless())
        self.assertIn("restored: ", done.stdout, done.stdout + done.stderr)
        self.assertEqual((self.snapshot(), self.journal_of()), (before, []))

    def test_a_hidden_or_oddly_named_link_is_still_this_projects(self):
        # Round 1 (chain 2): every .lnk is looked at, hidden ones too, whatever its name.
        self.install()
        claude = self.desk / "My Proj's Claude campaign.lnk"
        odd = self.desk / "Work.123.tmp.lnk"
        claude.rename(odd)
        carbon = self.desk / "My Proj's Gemini campaign.lnk"
        subprocess.run(["attrib", "+h", str(carbon)], check=True, capture_output=True, creationflags=windowless())
        done = self.shortcut("-Remove")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual((sorted(p.name for p in self.desk.iterdir()), list(self.icons.glob("*.ico"))), ([], []))

    def test_a_lock_that_cannot_be_opened_is_said_as_it_is(self):
        # Round 1 (chain 2): only another run holding it means another run; any other failure is its own message.
        self.install()
        lock = self.state / f"campaign-{self.project_key()}.lock"
        lock.mkdir(parents=True)  # a folder where the lock goes: it cannot be opened as a file
        started = time.monotonic()
        done = self.shortcut("-Remove")
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        self.assertIn(f"the lock '{lock}' cannot be opened", done.stdout)
        self.assertLess(time.monotonic() - started, 20, "it waited as if another run held the lock")

    def test_remove_keeps_the_journal_it_wrote_while_a_file_it_moved_aside_is_held(self):
        # Round 1 (chain 2): the journal -Remove itself writes names every file it moved aside.
        self.install()
        key = self.project_key()
        for _ in range(5):  # the hold must land between the file's move aside and its deletion
            stop, held = threading.Event(), []

            def hold():
                while not stop.is_set():
                    found = list(self.icons.glob("*.old"))
                    if found:
                        try:
                            with open(found[0], "rb"):
                                held.append(found[0])
                                stop.wait(60)
                        except OSError:  # it went first: this attempt lost the race, and retries
                            pass
                        return
                    time.sleep(0.0005)

            watcher = threading.Thread(target=hold)
            watcher.start()
            done = self.shortcut("-Remove")
            stop.set()
            watcher.join()
            if held and done.returncode == 1:
                break
            self.shortcut("-Remove")
            self.install()
        else:
            self.fail("the hold never landed between a file's move aside and its deletion")
        (journal,) = self.journal_of()
        record = json.loads(journal.read_text(encoding="utf-8"))
        self.assertEqual(record["state"], "committed")
        self.assertEqual(sorted(Path(a["to"]).name for a in record["aside"]),
                         sorted(["My Proj's Claude campaign.lnk", "My Proj's Gemini campaign.lnk",
                                 "My Proj's - Watch run.lnk", "My Proj's - Stop run.lnk",
                                 f"campaign-watch-{key}.ico", f"campaign-stop-{key}.ico",
                                 f"campaign-agy-{key}.ico", f"campaign-claude-{key}.ico"]))
        done = self.shortcut("-Remove")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual((sorted(p.name for p in self.desk.iterdir()), self.journal_of()), ([], []))

    def test_a_foreign_link_named_like_a_badge_is_never_taken_by_recovery(self):
        # Completion review (chain 2): `campaign-claude-<key>.ico.lnk` is a link, this project's only by its mark.
        self.install()
        key = self.project_key()
        theirs = self.desk / f"campaign-claude-{key}.ico.lnk"
        self.foreign_link(theirs)
        before = self.snapshot()
        self.state.mkdir(parents=True, exist_ok=True)
        for n, args in enumerate((["-Distro", "Ubuntu", "-Icon", str(self.icon)], ["-Remove"])):
            with self.subTest(args[0]):
                owner = self.DEAD + 4 * n  # one planted per mode, each well formed for its own id
                temp = self.desk / f"campaign-claude-{key}.ico.{owner}.tmp.lnk"
                if n:  # the run stopped before its move in: its own file is still there under the temporary name
                    shutil.copyfile(self.desk / "My Proj's Claude campaign.lnk", temp)
                moves = [{"from": str(temp), "to": str(theirs)}]
                journal = self.state / f"campaign-{key}.{owner}.journal.json"
                journal.write_text(json.dumps({"state": "swapping", "temps": [], "moves": moves, "aside": []}),
                                   encoding="utf-8")
                done = self.shortcut(*args)
                self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
                self.assertIn("which is not this project's", done.stdout)
                self.assertEqual((self.snapshot(skip=(temp.name,)), journal.exists()), (before, True))
                journal.unlink()
                temp.unlink(missing_ok=True)

    def test_a_journal_naming_another_projects_file_as_its_temporary_file_holds_every_change(self):
        # Reconciliation 3 (chain 2): a temporary file is checked for its owner like every file a journal names.
        self.install()
        key = self.project_key()
        self.state.mkdir(parents=True, exist_ok=True)
        for n, args in enumerate((["-Distro", "Ubuntu", "-Icon", str(self.icon)], ["-Remove"])):
            with self.subTest(args[0]):
                owner = self.DEAD + 4 * n
                theirs = self.desk / f"Demo Site Claude campaign.{owner}.tmp.lnk"  # another project's link
                self.foreign_link(theirs)
                before = self.snapshot()
                moves = [{"from": str(theirs), "to": str(self.desk / "Demo Site Claude campaign.lnk")}]
                journal = self.state / f"campaign-{key}.{owner}.journal.json"
                journal.write_text(json.dumps({"state": "swapping", "temps": [str(theirs)], "moves": moves,
                                               "aside": []}), encoding="utf-8")
                done = self.shortcut(*args)
                self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
                self.assertIn(f"names {theirs}, which is not this project's", done.stdout)
                self.assertEqual((self.snapshot(), journal.exists()), (before, True))
                journal.unlink()
                theirs.unlink()

    def test_a_stopped_remove_of_a_hidden_link_is_finished_by_the_next_run(self):
        # Reconciliation 5 (chain 2): a hidden link of this project's is its own in recovery too.
        self.install()
        claude = self.desk / "My Proj's Claude campaign.lnk"
        subprocess.run(["attrib", "+h", str(claude)], check=True, capture_output=True, creationflags=windowless())
        key = self.project_key()
        files = [*sorted(self.desk.glob("*.lnk")), *sorted(self.icons.glob("campaign-*.ico"))]
        aside = [{"to": str(f), "old": f"{f}.{self.DEAD}.old"} for f in files]
        self.state.mkdir(parents=True, exist_ok=True)
        journal = self.state / f"campaign-{key}.{self.DEAD}.journal.json"
        journal.write_text(json.dumps({"state": "swapping", "temps": [], "moves": [], "aside": aside}),
                           encoding="utf-8")  # -Remove wrote it, then stopped before its first move
        done = self.shortcut("-Remove")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual((sorted(p.name for p in self.desk.iterdir()), self.journal_of()), ([], []))

    def test_remove_in_one_distro_leaves_an_old_key_badge_another_distros_link_shows(self):
        # Reconciliation 11: one Linux path in two distros shared the old key; their desktops may differ.
        other_desk = Path(self._tmp.name) / "desk2"
        linux = ["-Icon", str(self.icon), "-Project", "/home/x/Demo"]
        self.assertEqual(self.shortcut("-Distro", "Debian", *linux, desk=other_desk).returncode, 0)
        old_key = hashlib.sha256(b"/home/x/demo").hexdigest()[:8]
        self.drop_controls(other_desk)
        for link in other_desk.glob("*.lnk"):  # Debian's set, as the old script wrote it
            badge = Path(self.read_link(link)["icon"].rsplit(",", 1)[0])
            old = badge.with_name(f"{badge.name.split('-')[0]}-{badge.name.split('-')[1]}-{old_key}.ico")
            badge.rename(old)
            self.set_description(link, "conclave-unattended: /home/x/Demo", icon=str(old))
        theirs = sorted(p.name for p in self.icons.glob(f"campaign-*-{old_key}.ico"))
        self.assertEqual(len(theirs), 2)
        done = self.shortcut("-Remove", "-Distro", "Ubuntu", "-Project", "/home/x/Demo")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(sorted(p.name for p in self.icons.glob(f"campaign-*-{old_key}.ico")), theirs)

    def journal_of(self):
        return list(self.state.glob("campaign-*.journal.json"))

    def test_a_moved_in_link_held_open_keeps_the_journal_until_it_can_go(self):
        # Reconciliation 4: recovery restored every original, but the new link it could not take out stayed.
        self.install()
        before = self.snapshot()
        journal = self.stopped_rename("swapping")
        with open(self.desk / "Demo Site Claude campaign.lnk", "rb"):  # held open without delete sharing
            done = self.shortcut("-Distro", "Arch", "-Icon", str(self.icon))
        self.assertIn("the journal is kept and the next run tries again", done.stdout)
        self.assertTrue(journal.exists())
        done = self.shortcut("-Distro", "Arch", "-Icon", str(self.icon))
        self.assertFalse(journal.exists())
        self.assertEqual(self.snapshot(), before)

    def test_a_retry_that_fails_after_a_stop_leaves_the_previous_set(self):
        # Reconciliation 5 and 6: the previous set is back before the retry does anything that can fail.
        self.install()
        before = self.snapshot()
        self.stopped_rename("swapping")
        installed = self.project / "scripts" / "campaign-icon.py"
        installed.write_text("import sys\nsys.exit(1)\n", encoding="utf-8")  # every render fails
        self.write_launchers("Demo Site")
        done = self.shortcut("-Distro", "Ubuntu", "-Icon", str(self.icon))
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        self.assertEqual({k: v for k, v in self.snapshot().items() if not k.endswith("campaign-icon.py")}, before)
        self.assertEqual(self.journal_of(), [])

    def test_remove_discards_a_stopped_run_and_nothing_comes_back(self):
        # Reconciliation 4: -Remove takes the set off, the stopped run's originals with it; no later run restores one.
        self.install()
        self.stopped_rename("swapping")
        done = self.shortcut("-Remove")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual((self.links(), sorted(p.name for p in self.icons.iterdir())), ([], []))
        self.assertEqual(sorted(p.name for p in self.desk.iterdir()), [])
        self.shortcut("-Distro", "Arch", "-Icon", str(self.icon))  # a later run, even a refused one
        self.assertEqual((self.links(), sorted(p.name for p in self.icons.iterdir())), ([], []))

    def test_remove_with_an_original_held_open_says_so_and_fails(self):
        # Reconciliation 5: automation must not read success while a stopped run's file stays.
        self.install()
        journal = self.stopped_rename("swapping")
        held = next(self.desk.glob("*.old"))
        with open(held, "rb"):  # held open without delete sharing
            done = self.shortcut("-Remove")
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        self.assertIn("a journal is kept (see above); run -Remove again", done.stdout)
        self.assertTrue(journal.exists())
        done = self.shortcut("-Remove")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual((sorted(p.name for p in self.desk.iterdir()), self.journal_of()), ([], []))

    def test_a_removal_kept_by_a_held_file_never_comes_back(self):
        # Reconciliation 7: after a -Remove a held original kept, a later install, even a refused one, restores nothing.
        self.install()
        self.stopped_rename("swapping")
        held = next(self.desk.glob("*.old"))
        with open(held, "rb"):  # held open without delete sharing
            self.assertEqual(self.shortcut("-Remove").returncode, 1)
        done = self.shortcut("-Distro", "Arch", "-Icon", str(self.icon))
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        self.assertNotIn("restored: ", done.stdout)
        self.assertEqual(self.links(), [])
        self.assertEqual((sorted(p.name for p in self.desk.iterdir()), self.journal_of()), ([], []))

    def test_a_temporary_link_held_open_keeps_the_journal_and_fails_remove(self):
        # Reconciliation 7: a stopped run's temporary link that cannot go yet keeps its journal.
        self.install()
        journal = self.stopped_rename("swapping")
        temp = next(self.desk.glob("*.tmp.lnk"))
        with open(temp, "rb"):  # held open without delete sharing
            done = self.shortcut("-Remove")
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        self.assertTrue(journal.exists())
        self.assertTrue(temp.exists())
        done = self.shortcut("-Remove")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual((sorted(p.name for p in self.desk.iterdir()), self.journal_of()), ([], []))

    def test_a_failed_run_whose_temporary_link_is_held_keeps_its_journal(self):
        # Reconciliation 8: a live run that fails cannot drop the record of a temporary file it could not remove.
        installed = self.project / "scripts" / "campaign-icon.py"
        installed.write_text("import runpy, sys, time\n"
                             "if sys.argv[1:2] == ['badge'] and sys.argv[3:4] == ['agy']:\n"
                             "    time.sleep(2)\n    sys.exit(1)\n"  # time for the watcher to take the link
                             f"sys.argv[0] = {str(RENDERER)!r}\nrunpy.run_path(sys.argv[0], run_name='__main__')\n",
                             encoding="utf-8")
        stop, held = threading.Event(), []

        def hold():
            while not stop.is_set():
                found = list(self.desk.glob("*.tmp.lnk")) if self.desk.exists() else []
                if found:
                    with open(found[0], "rb"):  # held open without delete sharing until the run has ended
                        held.append(found[0])
                        stop.wait(60)
                    return
                time.sleep(0.005)

        watcher = threading.Thread(target=hold)
        watcher.start()
        done = self.shortcut("-Distro", "Ubuntu", "-Icon", str(self.icon))
        stop.set()
        watcher.join()
        self.assertTrue(held, "the watcher never found the run's temporary link")
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        self.assertIn(f"{held[0]} stays", done.stdout)
        self.assertEqual(len(self.journal_of()), 1)
        shutil.copyfile(RENDERER, installed)
        done = self.shortcut("-Remove")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual((sorted(p.name for p in self.desk.iterdir()), self.journal_of()), ([], []))

    def test_an_install_waits_while_a_stopped_run_is_not_finished(self):
        # Reconciliation 9: a set written over a kept journal would be undone when that journal plays again.
        self.install()
        before = self.snapshot()
        journal = self.stopped_rename("swapping")
        held = next(self.desk.glob("*.old"))
        with open(held, "rb"):  # an original that cannot go back yet
            done = self.shortcut("-Distro", "Ubuntu", "-Icon", str(self.icon))
            self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
            self.assertIn("a stopped run is not finished (see above); free what it names and run the script again",
                          done.stdout)
            self.assertEqual(self.journal_of(), [journal])  # the stopped run's journal only: this run wrote nothing
        done = self.shortcut("-Distro", "Ubuntu", "-Icon", str(self.icon))  # released: the stop finishes, then the set
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(self.journal_of(), [])
        self.assertEqual(self.links(), ["My Proj's Claude campaign.lnk", "My Proj's Gemini campaign.lnk"])
        self.assertEqual(sorted(self.snapshot()), sorted(before))
        self.shortcut("-Distro", "Arch", "-Icon", str(self.icon))  # a later run changes nothing it wrote
        self.assertEqual(self.links(), ["My Proj's Claude campaign.lnk", "My Proj's Gemini campaign.lnk"])

    def test_an_old_key_badge_another_link_shows_stays(self):
        self.extra_env = {"STUB_WSL_MISSING": "Debian:/home/x/Demo"}  # no other distro has the path
        # Reconciliation 6: the old script keyed badges by the -Project form alone, so another link may show one.
        linux = ["-Distro", "Ubuntu", "-Icon", str(self.icon), "-Project", "/home/x/Demo"]
        self.assertEqual(self.shortcut(*linux).returncode, 0)
        old_key = hashlib.sha256(b"/home/x/demo").hexdigest()[:8]
        for link in self.desk.glob("*.lnk"):  # this project's set, as the old script wrote it
            badge = Path(self.read_link(link)["icon"].rsplit(",", 1)[0])
            old = badge.with_name(f"{badge.name.split('-')[0]}-{badge.name.split('-')[1]}-{old_key}.ico")
            badge.rename(old)
            self.set_description(link, "conclave-unattended: /home/x/Demo", icon=str(old))
        shown = self.icons / f"campaign-claude-{old_key}.ico"
        theirs = self.desk / "Other Claude campaign.lnk"  # another project's link that shows the same old badge
        shutil.copyfile(self.desk / "Demo Claude campaign.lnk", theirs)
        self.set_description(theirs, "conclave-unattended: D:\\elsewhere", icon=str(shown))
        for hidden in (theirs, shown):  # reconciliation 6 (chain 2): a hidden link and badge are seen too
            subprocess.run(["attrib", "+h", str(hidden)], check=True, capture_output=True, creationflags=windowless())
        done = self.shortcut(*linux)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertTrue(shown.exists(), "a badge another link shows was removed")
        self.assertFalse((self.icons / f"campaign-agy-{old_key}.ico").exists())
        done = self.shortcut("-Remove", "-Distro", "Ubuntu", "-Project", "/home/x/Demo")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertTrue(shown.exists(), "-Remove removed a badge another link shows")

    def test_the_default_icon_folder_is_named_by_the_folder_never_the_title(self):
        local = Path(self._tmp.name) / "local"
        local.mkdir()
        env = {"LOCALAPPDATA": str(local)}
        for title in (None, "Demo Site"):
            self.write_launchers(title)
            done = self.shortcut("-Distro", "Ubuntu", "-Icon", str(self.icon), icon_dir=False, **env)
            self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
            self.assertEqual(len(self.runner_badges(local / "My Proj's")), 2)
        done = self.shortcut("-Remove", icon_dir=False, **env)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(list((local / "My Proj's").iterdir()), [])
        self.assertFalse((local / "Demo Site").exists())


if __name__ == "__main__":
    unittest.main()
