"""Tests of `scripts/project-shortcuts.ps1`: the desktop links that open the project in an agent (D07 T01 section 5).

Every link is written to a scratch folder and then run, not only read back: its target and arguments are executed
windowless against recording stand-ins (a compiled `wt.exe`, a `wsl.exe` script, an agent script), so what each
program would receive, and where, is what the tests hold. Windows only, with PowerShell 7; the `bash` tests run
wherever a real bash is.
"""
from __future__ import annotations

import base64
import ctypes
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
PWSH = shutil.which("pwsh")
NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)
SUSPENDED = 0x4  # CREATE_SUSPENDED: the child joins its job object before it can start anything


class Interactive:
    """A child started as run_tree starts it (its whole tree ends with it), whose stdin stays open: a test reads its
    output as it comes, sees whether it is still waiting, and sends Enter when it chooses."""

    def __init__(self, command, **kwargs):
        self.job = None
        if os.name == "nt":
            self.kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
            self.kernel32.CreateJobObjectW.restype = ctypes.c_void_p
            self.kernel32.SetInformationJobObject.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p,
                                                              ctypes.c_uint32]
            self.kernel32.AssignProcessToJobObject.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
            self.kernel32.TerminateJobObject.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
            self.kernel32.CloseHandle.argtypes = [ctypes.c_void_p]
            self.job = self.kernel32.CreateJobObjectW(None, None)
            limits = _ExtendedLimits()
            limits.BasicLimitInformation.LimitFlags = 0x2000
            self.kernel32.SetInformationJobObject(self.job, 9, ctypes.byref(limits), ctypes.sizeof(limits))
            self.proc = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                         stderr=subprocess.STDOUT, text=True, creationflags=NO_WINDOW | SUSPENDED,
                                         **kwargs)
            handle = ctypes.c_void_p(int(self.proc._handle))
            self.kernel32.AssignProcessToJobObject(self.job, handle)
            ntdll = ctypes.WinDLL("ntdll")
            ntdll.NtResumeProcess.argtypes = [ctypes.c_void_p]
            ntdll.NtResumeProcess(handle)
        else:
            self.proc = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                         stderr=subprocess.STDOUT, text=True, start_new_session=True,
                                         creationflags=NO_WINDOW, **kwargs)
        self.output = []
        self.reader = threading.Thread(target=lambda: self.output.extend(iter(lambda: self.proc.stdout.read(1), "")),
                                       daemon=True)
        self.reader.start()

    def text(self):
        return "".join(self.output)

    def wait_for(self, needle, timeout=60):
        end = time.monotonic() + timeout
        while needle not in self.text():
            if time.monotonic() > end or (self.proc.poll() is not None and not self.reader.is_alive()):
                return False
            time.sleep(0.1)
        return True

    def enter(self):
        self.proc.stdin.write("\n")
        self.proc.stdin.flush()
        self.proc.stdin.close()

    def close(self):
        if self.job is not None:
            self.kernel32.TerminateJobObject(self.job, 1)
            self.kernel32.CloseHandle(self.job)
        elif self.proc.poll() is None:
            os.killpg(self.proc.pid, 9)
        self.proc.wait(timeout=30)
        # The pipes close with the child, once the reader has taken the last of its output.
        self.reader.join(timeout=10)
        for pipe in (self.proc.stdin, self.proc.stdout):
            if not pipe.closed:
                pipe.close()


class _Limits(ctypes.Structure):
    _fields_ = [("PerProcessUserTimeLimit", ctypes.c_int64), ("PerJobUserTimeLimit", ctypes.c_int64),
                ("LimitFlags", ctypes.c_uint32), ("MinimumWorkingSetSize", ctypes.c_size_t),
                ("MaximumWorkingSetSize", ctypes.c_size_t), ("ActiveProcessLimit", ctypes.c_uint32),
                ("Affinity", ctypes.c_size_t), ("PriorityClass", ctypes.c_uint32), ("SchedulingClass", ctypes.c_uint32)]


class _ExtendedLimits(ctypes.Structure):
    _fields_ = [("BasicLimitInformation", _Limits), ("IoInfo", ctypes.c_uint64 * 6),
                ("ProcessMemoryLimit", ctypes.c_size_t), ("JobMemoryLimit", ctypes.c_size_t),
                ("PeakProcessMemoryUsed", ctypes.c_size_t), ("PeakJobMemoryUsed", ctypes.c_size_t)]


def run_tree(command, timeout, **kwargs):
    """subprocess.run that ends the child's whole tree, never only the child: on Windows the child starts suspended
    inside a job object that kills every process in it when closed, elsewhere in a session of its own whose group is
    killed. The owner's rule: a child process never outlives its test (an orphaned child filled C: on 2026-11-08)."""
    stdin = kwargs.pop("input", None)
    encoding = kwargs.pop("encoding", "utf-8")
    errors = kwargs.pop("errors", "replace")
    if os.name != "nt":
        proc = subprocess.Popen(command, start_new_session=True, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True, encoding=encoding, errors=errors,
                                creationflags=NO_WINDOW, **kwargs)
        try:
            out, err = proc.communicate(stdin, timeout=timeout)
        finally:
            if proc.poll() is None:
                os.killpg(proc.pid, 9)
                proc.wait()
        return subprocess.CompletedProcess(command, proc.returncode, out, err)
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.CreateJobObjectW.restype = ctypes.c_void_p
    kernel32.SetInformationJobObject.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_uint32]
    kernel32.AssignProcessToJobObject.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    kernel32.TerminateJobObject.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
    kernel32.CloseHandle.argtypes = [ctypes.c_void_p]
    ntdll = ctypes.WinDLL("ntdll")
    ntdll.NtResumeProcess.argtypes = [ctypes.c_void_p]
    job = kernel32.CreateJobObjectW(None, None)
    limits = _ExtendedLimits()
    limits.BasicLimitInformation.LimitFlags = 0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
    kernel32.SetInformationJobObject(job, 9, ctypes.byref(limits), ctypes.sizeof(limits))
    try:
        proc = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                text=True, encoding=encoding, errors=errors,
                                creationflags=NO_WINDOW | SUSPENDED, **kwargs)
        try:
            handle = ctypes.c_void_p(int(proc._handle))
            if not kernel32.AssignProcessToJobObject(job, handle):
                raise ctypes.WinError(ctypes.get_last_error())
            ntdll.NtResumeProcess(handle)
            out, err = proc.communicate(stdin, timeout=timeout)
        finally:
            kernel32.TerminateJobObject(job, 1)
            proc.wait()
    finally:
        kernel32.CloseHandle(job)
    return subprocess.CompletedProcess(command, proc.returncode, out, err)
CSC = Path(os.environ.get("WINDIR", r"C:\Windows")) / "Microsoft.NET/Framework64/v4.0.30319/csc.exe"
# The stand-in for wsl.exe: it lists two distros, says whether a folder exists (all do but those in STUB_WSL_MISSING),
# fails everything with STUB_WSL_FAIL set, and records every call.
STUB_WSL = r'''import json, os, sys
args = sys.argv[1:]
with open(os.environ["STUB_WSL_LOG"], "a", encoding="utf-8") as log:
    log.write(json.dumps(args) + "\n")
if os.environ.get("STUB_WSL_FAIL"):
    print("the stand-in WSL failed", file=sys.stderr)
    sys.exit(4)
if args[:2] == ["-l", "-q"]:
    distros = os.environ.get("STUB_WSL_DISTROS")
    if distros is None:
        print("Ubuntu\nDebian")
    elif distros:
        print(distros)
    sys.exit(0)
rest = args[args.index("--exec") + 1:] if "--exec" in args else []
if rest[:2] == ["test", "-d"]:
    sys.exit(1 if rest[2] in os.environ.get("STUB_WSL_MISSING", "").split("|") else 0)
'''
# The stand-in for Windows Terminal: a real .exe (the link's target must be one), recording its working folder and
# each argument it was given, base64 per line, and starting nothing.
STUB_WT = r'''using System; using System.IO; using System.Text;
class Recorder {
  static int Main(string[] args) {
    var lines = new StringBuilder(Convert.ToBase64String(Encoding.UTF8.GetBytes(Environment.CurrentDirectory)));
    foreach (var a in args) lines.Append(' ').Append(Convert.ToBase64String(Encoding.UTF8.GetBytes(a)));
    File.AppendAllText(Environment.GetEnvironmentVariable("STUB_WT_LOG"), lines + "\n");
    return 0;
  }
}
'''
PROFILE = {"schema_version": 1, "title": "Example Site", "icon": "assets/icon.png", "agents": ["claude"],
           "skip_permissions": False, "links": []}
LABELS = {"claude": "Claude", "codex": "Codex", "agy": "Antigravity"}
FLAGS = {"claude": "--dangerously-skip-permissions", "codex": "--dangerously-bypass-approvals-and-sandbox",
         "agy": "--dangerously-skip-permissions"}


def folder(path):
    """A folder as a folder: Windows reports one by its long name or an 8.3 short one (RUNNER~1 on GitHub's runners,
    PROGRA~1), so working folders are compared resolved, never by spelling."""
    return Path(path).resolve()


def bash_command(agent, skip=False):
    command = f"{agent} {FLAGS[agent]}" if skip else agent
    return (f"{command} || (echo '{LABELS[agent]} did not start: the reason is above. Press Enter to close.'"
            " && read -r _)")


def find_bash():
    """A real bash: Git's on Windows (System32's starts WSL instead), the system's elsewhere."""
    if os.name == "nt":
        git = shutil.which("git")
        candidates = [Path(git).resolve().parents[1] / "bin/bash.exe"] if git else []
        return next((str(c) for c in candidates if c.is_file()), None)
    return shutil.which("bash")


BASH = find_bash()


def declared_bash(label):
    """A WSL `run` entry's Bash supervisor, which gets the command as its first argument: the label quoted for Bash
    inside the failure message (D07 T01 section 6)."""
    message = f"{label} did not start: the reason is above. Press Enter to close.".replace("'", "'\\''")
    return f"(eval \"$1\") || {{ echo '{message}'; read -r _; }}"


DECLARED = [{"label": "Owner's Dashboard", "mark": "wordpress", "run": "wp admin"},
            {"label": "Site", "mark": "site", "url": "https://example.com/wp-admin/?a=1&b=2"}]


def renderer():
    spec = importlib.util.spec_from_file_location("campaign_icon_for_links", SCRIPTS / "campaign-icon.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


@unittest.skipUnless(os.name == "nt" and PWSH and CSC.is_file(),
                     "a .lnk link, WScript.Shell, PowerShell 7 and the .NET Framework compiler are Windows's")
class ProjectShortcutsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._shared = tempfile.TemporaryDirectory()
        cls.wt_dir = Path(cls._shared.name) / "terminal"
        cls.wt_dir.mkdir()
        (cls.wt_dir / "recorder.cs").write_text(STUB_WT, encoding="utf-8")
        built = run_tree([str(CSC), "/nologo", f"/out:{cls.wt_dir / 'wt.exe'}", str(cls.wt_dir / "recorder.cs")],
                         timeout=120)
        assert built.returncode == 0, built.stdout + built.stderr
        cls.icon_png = renderer().write_png(64, 64, bytearray(b"\x10\x22\x1a\xff" * 64 * 64))

    @classmethod
    def tearDownClass(cls):
        cls._shared.cleanup()

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.base = Path(self._tmp.name)
        self.project = self.make_project("My Site's")   # a space and a quote
        self.desk, self.icons = self.base / "desk", self.base / "icons"
        stub = self.base / "stub_wsl.py"
        stub.write_text(STUB_WSL, encoding="utf-8")
        self.wsl = self.base / "wsl.cmd"
        self.wsl.write_text(f'@"{sys.executable}" "{stub}" %*\r\n', encoding="utf-8")
        self.wsl_log, self.wt_log, self.agent_log = self.base / "wsl.log", self.base / "wt.log", self.base / "agent.log"
        # An agent stand-in for the Windows form: it records where it ran and what it was given.
        self.agents = self.base / "agents"
        self.agents.mkdir()
        for agent in LABELS:
            (self.agents / f"{agent}.cmd").write_text(f'@echo {agent}^|%CD%^|%*>>"%STUB_AGENT_LOG%"\r\n',
                                                     encoding="utf-8")
        self.plain_path = os.pathsep.join(p for p in os.environ.get("PATH", "").split(os.pathsep)
                                          if p and not (Path(p) / "wt.exe").exists())

    # --- fixtures ---------------------------------------------------------------------------------------------------

    def make_project(self, name, profile=PROFILE):
        project = self.base / name
        for rel in ("project-shortcuts.ps1", "campaign-icon.py", "lib/launchers.py", "lib/ShellLink.cs"):
            (project / "scripts" / rel).parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(SCRIPTS / rel, project / "scripts" / rel)
        (project / "assets").mkdir()
        (project / "assets/icon.png").write_bytes(self.icon_png)
        (project / ".conclave").mkdir()
        self.write_profile(project, profile)
        return project

    @staticmethod
    def write_profile(project, profile):
        (project / ".conclave/launchers.json").write_text(json.dumps(profile), encoding="utf-8")

    def env(self, terminal=True, **extra):
        path = (str(self.wt_dir) + os.pathsep if terminal else "") + self.plain_path
        return {**os.environ, "PATH": path, "STUB_WSL_LOG": str(self.wsl_log), "STUB_WT_LOG": str(self.wt_log),
                "STUB_AGENT_LOG": str(self.agent_log), **extra}

    def run_script(self, *args, project=None, terminal=True, desk=None, icons=None, **extra):
        project = project or self.project
        command = [PWSH, "-NoProfile", "-NonInteractive", "-File", str(project / "scripts/project-shortcuts.ps1"),
                   "-ShortcutDir", str(desk or self.desk), "-IconDir", str(icons or self.icons), "-Wsl", str(self.wsl),
                   "-Python", sys.executable, *args]
        return run_tree(command, timeout=300, env=self.env(terminal, **extra))

    def read_link(self, title="Example Site", agent="claude", desk=None):
        return self.read_named(f"{title} ({LABELS[agent]})", desk=desk)

    def run_link(self, link, terminal=True, stdin="", **extra):
        """Start the link's target with its arguments, as the shell would, from its working folder."""
        return run_tree(f'"{link["target"]}" {link["arguments"]}', timeout=120, cwd=link["workdir"], input=stdin,
                        env=self.env(terminal, **extra))

    def terminal_calls(self):
        calls = []
        for line in self.wt_log.read_text(encoding="utf-8").splitlines():
            words = [base64.b64decode(w).decode("utf-8") for w in line.split(" ")]
            calls.append({"cwd": words[0], "argv": words[1:]})
        return calls

    def links(self):
        return sorted(p.name for p in self.desk.glob("*.lnk")) if self.desk.exists() else []

    def leftovers(self, desk=None, icons=None):
        desk, icons = desk or self.desk, icons or self.icons
        found = []
        if desk.exists():
            found.extend([*desk.glob("*.tmp.*"), *desk.glob("*.old")])
        if icons.exists():
            found.extend([*icons.glob("*.tmp.*"), *icons.glob("*.old")])
        return [p.name for p in found]

    # --- refusals ---------------------------------------------------------------------------------------------------

    def test_a_project_folder_that_does_not_exist_is_refused(self):
        done = self.run_script("-Project", str(self.base / "nowhere"))
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        self.assertRegex(done.stdout.strip(), r"the project folder does not exist: .*; nothing was written$")
        self.assertEqual(self.links(), [])

    def test_a_linux_path_needs_a_distro(self):
        done = self.run_script("-Project", "/home/me/site")
        self.assertEqual(done.returncode, 1)
        self.assertIn("a Linux path needs -Distro", done.stdout)
        self.assertEqual(self.links(), [])

    def test_a_malformed_profile_is_refused_with_the_loaders_message(self):
        self.write_profile(self.project, dict(PROFILE, title=None))
        done = self.run_script()
        self.assertEqual(done.returncode, 1)
        self.assertIn("the launcher profile cannot be read: launchers:", done.stdout)
        self.assertIn("title: must be set", done.stdout)
        self.assertTrue(done.stdout.strip().endswith("nothing was written"))
        self.assertEqual(self.links(), [])

    def test_a_wsl_folder_wsl_does_not_have_is_refused(self):
        done = self.run_script("-Project", "/srv/gone", "-Distro", "Ubuntu", STUB_WSL_MISSING="/srv/gone")
        self.assertEqual(done.returncode, 1)
        self.assertIn("does not exist in 'Ubuntu'", done.stdout)
        self.assertEqual(self.links(), [])

    def test_a_name_another_project_holds_is_refused_and_left_alone(self):
        other = self.make_project("other", PROFILE)   # the same title
        self.assertEqual(self.run_script(project=other).returncode, 0)
        before = (self.desk / "Example Site (Claude).lnk").read_bytes()
        done = self.run_script()
        self.assertEqual(done.returncode, 1, done.stdout)
        self.assertIn("is already there and is not this project's link; nothing was written", done.stdout)
        self.assertEqual((self.desk / "Example Site (Claude).lnk").read_bytes(), before)
        mark, _, where = self.read_link()["description"].partition(": ")
        self.assertEqual((mark, folder(where)), ("conclave-launcher", folder(other)))

    # --- the WSL form -----------------------------------------------------------------------------------------------

    def test_a_wsl_project_opens_in_windows_terminal_with_an_interactive_login_shell(self):
        done = self.run_script("-Project", r"\\wsl.localhost\Debian\home\me\site")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(self.links(), ["Example Site (Claude).lnk"])
        link = self.read_link()
        self.assertEqual(folder(link["target"]), folder(self.wt_dir / "wt.exe"))
        self.assertEqual(link["workdir"], os.environ["USERPROFILE"])
        self.assertEqual(link["description"], "conclave-launcher: wsl:debian:/home/me/site")
        icon = Path(link["icon"])
        self.assertEqual(link["icon_index"], 0)
        self.assertRegex(icon.name, r"^launcher-claude-[0-9a-f]{8}\.ico$")
        self.assertEqual(icon.parent, self.icons)
        self.assertTrue(icon.is_file())
        self.assertEqual(self.run_link(link).returncode, 0)
        (call,) = self.terminal_calls()
        self.assertEqual(folder(call["cwd"]), folder(os.environ["USERPROFILE"]))
        self.assertEqual(call["argv"], [str(self.wsl), "-d", "Debian", "--cd", "/home/me/site", "--exec", "bash",
                                        "-lic", bash_command("claude")])

    def test_a_linux_path_with_a_space_and_a_quote_reaches_wsl_whole(self):
        done = self.run_script("-Project", "/srv/my site's", "-Distro", "Ubuntu")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.run_link(self.read_link())
        self.assertEqual(self.terminal_calls()[0]["argv"][1:5], ["-d", "Ubuntu", "--cd", "/srv/my site's"])

    def test_without_windows_terminal_powershell_runs_wsl_and_holds_its_failure(self):
        done = self.run_script("-Project", "/srv/my site's", "-Distro", "Ubuntu", terminal=False)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        link = self.read_link()
        self.assertEqual(Path(link["target"]).name.lower(), "pwsh.exe")
        self.wsl_log.unlink()
        ran = self.run_link(link, terminal=False)
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        self.assertEqual([json.loads(line) for line in self.wsl_log.read_text(encoding="utf-8").splitlines()],
                         [["-d", "Ubuntu", "--cd", "/srv/my site's", "--exec", "bash", "-lic", bash_command("claude")]])
        waiting = Interactive(f'"{link["target"]}" {link["arguments"]}', cwd=link["workdir"],
                              env=self.env(False, STUB_WSL_FAIL="1"))
        try:
            self.assertTrue(waiting.wait_for("WSL could not open the project (exit 4): the reason is above. "
                                             "Press Enter to close."), waiting.text())
            time.sleep(2)
            self.assertIsNone(waiting.proc.poll(), "the window closed before Enter")
            waiting.enter()
            self.assertEqual(waiting.proc.wait(timeout=30), 0)
        finally:
            waiting.close()

    # --- the Windows form -------------------------------------------------------------------------------------------

    def test_a_windows_project_opens_in_windows_terminal_in_its_folder(self):
        done = self.run_script()
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        link = self.read_link()
        self.assertEqual(folder(link["target"]), folder(self.wt_dir / "wt.exe"))
        self.assertEqual(folder(link["workdir"]), folder(self.project))
        self.run_link(link)
        (call,) = self.terminal_calls()
        self.assertEqual(folder(call["cwd"]), folder(self.project))
        self.assertEqual((call["argv"][0], folder(call["argv"][1])), ("-d", folder(self.project)))
        self.assertEqual(Path(call["argv"][2]).name.lower(), "pwsh.exe")
        self.assertEqual(call["argv"][3:], ["-NoLogo", "-NoExit", "-Command", "claude"])

    def test_without_windows_terminal_powershell_starts_the_agent_in_the_project(self):
        done = self.run_script(terminal=False)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        link = self.read_link()
        self.assertEqual(Path(link["target"]).name.lower(), "pwsh.exe")
        self.assertEqual(link["arguments"], "-NoLogo -NoExit -Command claude")
        env_path = str(self.agents) + os.pathsep + self.plain_path
        self.run_link(link, terminal=False, PATH=env_path)
        agent, cwd = self.agent_log.read_text(encoding="utf-8").split("|")[:2]
        # The same folder, compared as a folder: cmd reports its long name where the test's path may hold a short
        # 8.3 one (CI red at 92a02a1: RUNNER~1 against runneradmin).
        self.assertEqual((agent, folder(cwd)), ("claude", folder(self.project)))

    # --- the agent command ------------------------------------------------------------------------------------------

    def test_the_permission_flag_is_added_only_when_the_profile_asks(self):
        for skip in (False, True):
            with self.subTest(skip_permissions=skip):
                self.write_profile(self.project, dict(PROFILE, agents=list(LABELS), skip_permissions=skip))
                if self.wt_log.exists():
                    self.wt_log.unlink()
                done = self.run_script("-Project", "/srv/site", "-Distro", "Ubuntu")
                self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
                for agent in LABELS:
                    self.run_link(self.read_link(agent=agent))
                received = [call["argv"][-1] for call in self.terminal_calls()]
                self.assertEqual(received, [bash_command(agent, skip) for agent in LABELS])
        # The Windows form too, under its own title (the WSL identity above holds "Example Site").
        self.write_profile(self.project, dict(PROFILE, title="Windows Site", agents=["codex"], skip_permissions=True))
        self.assertEqual(self.run_script(terminal=False).returncode, 0)
        self.run_link(self.read_link(title="Windows Site", agent="codex"), terminal=False,
                      PATH=str(self.agents) + os.pathsep + self.plain_path)
        self.assertEqual(self.agent_log.read_text(encoding="utf-8").split("|")[2].strip(),
                         "--dangerously-bypass-approvals-and-sandbox")

    def test_every_form_runs_the_agent_with_the_flag_only_when_asked(self):
        # Completion review: WSL and Windows projects, with and without Windows Terminal, the permission flag both
        # ways, each link run and what it handed on read where it arrived.
        agent_path = str(self.agents) + os.pathsep + self.plain_path
        for form, terminal, skip in [(f, t, s) for f in ("wsl", "windows") for t in (True, False) for s in (False, True)]:
            with self.subTest(form=form, terminal=terminal, skip_permissions=skip):
                title = f"{form} {'wt' if terminal else 'plain'} {'skip' if skip else 'ask'}"
                project = self.make_project(title, dict(PROFILE, title=title, agents=["codex"], skip_permissions=skip))
                for log in (self.wt_log, self.wsl_log, self.agent_log):
                    if log.exists():
                        log.unlink()
                where = ("-Project", f"/srv/{title.replace(' ', '-')}", "-Distro", "Ubuntu") if form == "wsl" else ()
                done = self.run_script(*where, project=project, terminal=terminal)
                self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
                link = self.read_link(title=title, agent="codex")
                ran = self.run_link(link, terminal=terminal, PATH=agent_path if not terminal else self.env()["PATH"])
                self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
                if form == "wsl":
                    expected = ["-d", "Ubuntu", "--cd", f"/srv/{title.replace(' ', '-')}", "--exec", "bash", "-lic",
                                bash_command("codex", skip)]
                    if terminal:
                        (call,) = self.terminal_calls()
                        self.assertEqual((folder(call["cwd"]), call["argv"]),
                                         (folder(os.environ["USERPROFILE"]), [str(self.wsl), *expected]))
                    else:
                        calls = [json.loads(line) for line in self.wsl_log.read_text(encoding="utf-8").splitlines()]
                        self.assertEqual(calls[-1], expected)   # after the script's own -l and test -d calls
                else:
                    command = ["codex", FLAGS["codex"]] if skip else ["codex"]
                    if terminal:
                        (call,) = self.terminal_calls()
                        self.assertEqual(folder(call["cwd"]), folder(project))
                        self.assertEqual((call["argv"][0], folder(call["argv"][1])), ("-d", folder(project)))
                        self.assertEqual(call["argv"][3:], ["-NoLogo", "-NoExit", "-Command", " ".join(command)])
                    else:
                        agent, cwd, args = self.agent_log.read_text(encoding="utf-8").strip().split("|")
                        self.assertEqual((agent, folder(cwd), args.strip()),
                                         ("codex", folder(project), " ".join(command[1:])))

    def test_the_flags_are_the_ones_the_unattended_run_passes(self):
        run = SCRIPTS.parents[2] / "unattended/templates/scripts/unattended-run.py"
        if not run.is_file():
            self.skipTest("an installed project: the unattended kit's launcher is not beside this one")
        text = run.read_text(encoding="utf-8")
        script = (SCRIPTS / "project-shortcuts.ps1").read_text(encoding="utf-8")
        for agent in LABELS:
            block = re.search(rf'if agent == "{agent}":(.*?)(?:\n    if agent ==|\n    raise RunError)', text, re.S)
            flags = re.findall(r'"(--dangerously-[a-z-]+)"', block.group(1))
            self.assertEqual(flags, [FLAGS[agent]], agent)
            self.assertRegex(script, rf"{agent} = '{re.escape(FLAGS[agent])}'")

    # --- identity, removal and publication --------------------------------------------------------------------------

    def test_one_path_in_two_distros_is_two_projects_and_each_spelling_is_one(self):
        debian = self.make_project("debian", dict(PROFILE, title="Debian Site"))
        self.assertEqual(self.run_script("-Project", "/srv/site", "-Distro", "Ubuntu").returncode, 0)
        self.assertEqual(self.run_script("-Project", r"\\wsl.localhost\Debian\srv\site", project=debian).returncode, 0)
        icons = sorted(p.name for p in self.icons.iterdir())
        self.assertEqual(len(icons), 2, "the two distros' icons share a key")
        done = self.run_script("-Project", "/srv/site/", "-Distro", "Ubuntu", "-Remove")
        self.assertIn("1 link(s) and 1 icon(s)", done.stdout)
        self.assertEqual(self.links(), ["Debian Site (Claude).lnk"])
        self.assertEqual(len(list(self.icons.iterdir())), 1)
        done = self.run_script("-Project", "\\\\wsl$\\Debian\\srv\\site\\", "-Remove")
        self.assertIn("1 link(s) and 1 icon(s) of wsl:debian:/srv/site removed", done.stdout)
        self.assertEqual(self.links(), [])
        self.assertEqual(list(self.icons.iterdir()), [])

    def test_two_linux_paths_that_differ_in_case_are_two_projects(self):
        # Round 1 P1: PowerShell's -eq ignored case, so /srv/Site pruned and removed /srv/site's links.
        upper = self.make_project("upper", dict(PROFILE, title="Upper Site"))
        self.assertEqual(self.run_script("-Project", "/srv/site", "-Distro", "Ubuntu").returncode, 0)
        done = self.run_script("-Project", "/srv/Site", "-Distro", "ubuntu", project=upper)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(self.links(), ["Example Site (Claude).lnk", "Upper Site (Claude).lnk"])
        gone = self.run_script("-Project", "/srv/Site", "-Distro", "Ubuntu", "-Remove", project=upper)
        self.assertIn("1 link(s) and 1 icon(s)", gone.stdout)
        self.assertEqual(self.links(), ["Example Site (Claude).lnk"])
        same = self.make_project("same", PROFILE)   # the same title as /srv/site's, in another case
        refused = self.run_script("-Project", "/srv/SITE", "-Distro", "Ubuntu", project=same)
        self.assertEqual(refused.returncode, 1, refused.stdout)
        self.assertIn("is not this project's link", refused.stdout)

    def test_a_folder_spelled_another_way_still_writes_and_keeps_its_links(self):
        # CI red on 2026-11-17 (62801e0): the runner's temp folder resolved under a short 8.3 name, so the script's
        # own temporary links, compared by full path, were listed as stale; once renamed into place they could not be
        # moved aside, and the whole run rolled back.
        self.desk.mkdir()
        self.icons.mkdir()
        desk, icons = self.desk / ".." / "desk", self.icons / ".." / "icons"
        for terminal in (True, False):
            with self.subTest(terminal=terminal):
                self.write_profile(self.project, dict(PROFILE, agents=["claude", "codex"]))
                done = self.run_script(terminal=terminal, desk=desk, icons=icons)
                self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
                self.assertEqual(self.links(), ["Example Site (Claude).lnk", "Example Site (Codex).lnk"])
                self.assertEqual(self.leftovers(), [])
        self.write_profile(self.project, dict(PROFILE, agents=["claude"]))
        done = self.run_script(desk=desk, icons=icons)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(self.links(), ["Example Site (Claude).lnk"], "a stale link is still found and removed")

    def test_remove_takes_this_projects_links_and_icons_and_nothing_else(self):
        other = self.make_project("other-site", dict(PROFILE, title="Other Site"))
        self.assertEqual(self.run_script().returncode, 0)
        self.assertEqual(self.run_script(project=other).returncode, 0)
        campaign = self.desk / "My Site's Claude campaign.lnk"
        shutil.copyfile(self.desk / "Example Site (Claude).lnk", campaign)
        run_tree([PWSH, "-NoProfile", "-NonInteractive", "-Command",
                        "$s = (New-Object -ComObject WScript.Shell).CreateShortcut($env:LINK_PATH); "
                        "$s.Description = 'conclave-unattended: ' + $env:PROJECT; $s.Save()"],
                       env={**os.environ, "LINK_PATH": str(campaign), "PROJECT": str(self.project)}, timeout=60)
        count = len(list(self.icons.iterdir()))
        done = self.run_script("-Remove")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("1 link(s) and 1 icon(s)", done.stdout)
        self.assertEqual(self.links(), ["My Site's Claude campaign.lnk", "Other Site (Claude).lnk"])
        self.assertEqual(len(list(self.icons.iterdir())), count - 1)

    def test_remove_needs_no_profile_icon_python_or_wsl_and_runs_twice(self):
        self.assertEqual(self.run_script("-Project", "/srv/site", "-Distro", "Ubuntu").returncode, 0)
        self.write_profile(self.project, {"broken": True})
        (self.project / "assets/icon.png").unlink()
        done = self.run_script("-Project", "/srv/site", "-Distro", "Ubuntu", "-Remove", STUB_WSL_FAIL="1",
                               PATH="")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("1 link(s) and 1 icon(s)", done.stdout)
        again = self.run_script("-Project", "/srv/site", "-Distro", "Ubuntu", "-Remove")
        self.assertEqual(again.returncode, 0)
        self.assertIn("0 link(s) and 0 icon(s)", again.stdout)
        gone = self.run_script("-Project", str(self.base / "deleted-folder"), "-Remove")
        self.assertEqual((gone.returncode, "0 link(s) and 0 icon(s)" in gone.stdout), (0, True), gone.stdout)

    def test_a_link_the_profile_no_longer_names_is_removed_with_its_icon(self):
        self.write_profile(self.project, dict(PROFILE, agents=["claude", "codex"]))
        self.assertEqual(self.run_script().returncode, 0)
        self.assertEqual(self.links(), ["Example Site (Claude).lnk", "Example Site (Codex).lnk"])
        self.write_profile(self.project, dict(PROFILE, title="Renamed Site"))
        done = self.run_script()
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(self.links(), ["Renamed Site (Claude).lnk"])
        self.assertEqual([p.name.split("-")[1] for p in self.icons.iterdir()], ["claude"])

    def test_a_failed_render_or_move_keeps_every_earlier_link_and_icon(self):
        self.write_profile(self.project, dict(PROFILE, agents=["claude", "codex"]))
        self.assertEqual(self.run_script().returncode, 0)
        before = {p.name: p.read_bytes() for p in [*self.desk.iterdir(), *self.icons.iterdir()]}
        self.write_profile(self.project, dict(PROFILE, agents=["claude", "codex", "agy"]))
        (self.project / "assets/icon.png").write_bytes(b"\x89PNG not an image")
        done = self.run_script()
        self.assertEqual(done.returncode, 1, done.stdout)
        self.assertIn("could not be rendered", done.stdout)
        self.assertEqual({p.name: p.read_bytes() for p in [*self.desk.iterdir(), *self.icons.iterdir()]}, before)
        # A link that cannot be replaced: held open without delete sharing, as an editor or a scan would.
        (self.project / "assets/icon.png").write_bytes(self.icon_png)
        with open(self.desk / "Example Site (Codex).lnk", "rb"):
            done = self.run_script()
        self.assertEqual(done.returncode, 1, done.stdout)
        self.assertIn("could not be put in place", done.stdout)
        self.assertEqual({p.name: p.read_bytes() for p in [*self.desk.iterdir(), *self.icons.iterdir()]}, before)
        self.assertEqual(self.leftovers(), [])
        # A stale link that cannot be removed (round 1 P2): the new links are put back with the rest.
        self.write_profile(self.project, dict(PROFILE, title="Renamed Site"))
        with open(self.desk / "Example Site (Codex).lnk", "rb"):
            done = self.run_script()
        self.assertEqual(done.returncode, 1, done.stdout)
        self.assertIn("could not be put in place or removed", done.stdout)
        self.assertEqual({p.name: p.read_bytes() for p in [*self.desk.iterdir(), *self.icons.iterdir()]}, before)
        self.assertEqual(self.leftovers(), [])

    # --- declared links (D07 T01 section 6) -------------------------------------------------------------------------

    def read_named(self, name, desk=None):
        path = (desk or self.desk) / f"{name}.lnk"
        script = (
            "[Console]::OutputEncoding = [Text.Encoding]::UTF8; "
            "Add-Type -Path $env:SHELL_LINK_CS; "
            "$s = [Conclave.ShellLink]::Read($env:LINK_PATH); "
            "@{target = $s.Target; arguments = $s.Arguments; icon = $s.IconPath; icon_index = $s.IconIndex; "
            "description = $s.Description; workdir = $s.WorkingDirectory} | ConvertTo-Json -Compress"
        )
        done = run_tree([PWSH, "-NoProfile", "-NonInteractive", "-Command", script], timeout=60,
                        encoding="utf-8",
                        env={**os.environ, "LINK_PATH": str(path),
                             "SHELL_LINK_CS": str(SCRIPTS / "lib/ShellLink.cs")})
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        return json.loads(done.stdout)

    def declared_profile(self, links):
        self.write_profile(self.project, dict(PROFILE, links=links))

    def test_a_declared_run_link_in_wsl_runs_its_command_through_wsl_and_holds_a_failure(self):
        self.declared_profile(DECLARED[:1])
        done = self.run_script("-Project", "/srv/my site's", "-Distro", "Ubuntu")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        link = self.read_named("Example Site (Owner's Dashboard)")
        self.assertEqual(Path(link["target"]).name.lower(), "pwsh.exe")
        self.assertEqual(folder(link["workdir"]), folder(os.environ["USERPROFILE"]))
        self.wsl_log.unlink()
        ran = self.run_link(link)
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        self.assertEqual([json.loads(line) for line in self.wsl_log.read_text(encoding="utf-8").splitlines()],
                         [["-d", "Ubuntu", "--cd", "/srv/my site's", "--exec", "bash", "-lc",
                           declared_bash("Owner's Dashboard"), "bash", "wp admin"]])
        waiting = Interactive(f'"{link["target"]}" {link["arguments"]}', cwd=link["workdir"],
                              env=self.env(STUB_WSL_FAIL="1"))
        try:
            self.assertTrue(waiting.wait_for("Owner's Dashboard: WSL could not open the project (exit 4): the reason "
                                             "is above. Press Enter to close."), waiting.text())
            time.sleep(2)
            self.assertIsNone(waiting.proc.poll(), "the window closed before Enter")
            waiting.enter()
            self.assertEqual(waiting.proc.wait(timeout=30), 0)
        finally:
            waiting.close()

    @unittest.skipUnless(BASH, "the declared command is run by a real bash (Git's on Windows)")
    def test_the_declared_bash_command_runs_whole_in_a_real_bash_with_an_apostrophe_in_its_label(self):
        # Commands whose own syntax a splice would break (round 1): a trailing `;`, a comment, an `exit`.
        self.declared_profile([dict(DECLARED[0], run="printf 'ran-fine\\n'; # done;"),
                               {"label": "Bob's Failing Tool", "mark": "site", "run": "conclave-no-such-tool"},
                               {"label": "Exits", "mark": "site", "run": "exit 3"}])
        self.assertEqual(self.run_script("-Project", "/srv/site", "-Distro", "Ubuntu").returncode, 0)
        self.wsl_log.unlink()
        for name in ("Example Site (Owner's Dashboard)", "Example Site (Bob's Failing Tool)", "Example Site (Exits)"):
            self.assertEqual(self.run_link(self.read_named(name)).returncode, 0)
        calls = [json.loads(line) for line in self.wsl_log.read_text(encoding="utf-8").splitlines()]
        words = [call[call.index("-lc") + 1:] for call in calls]
        env = {**os.environ, "HOME": str(self.base), "BASH_ENV": ""}
        fine = run_tree([BASH, "-lc", *words[0]], timeout=60, input="", cwd=self.base, env=env)
        self.assertEqual((fine.returncode, fine.stdout.strip().splitlines()[-1]), (0, "ran-fine"), fine.stderr)
        self.assertNotIn("did not start", fine.stdout)
        for label, said in (("Bob's Failing Tool", words[1]), ("Exits", words[2])):
            with self.subTest(label=label):
                waiting = Interactive([BASH, "-lc", *said], cwd=self.base, env=env)
                try:
                    self.assertTrue(waiting.wait_for(f"{label} did not start: the reason is above. Press Enter to "
                                                     "close."), waiting.text())
                    time.sleep(2)
                    self.assertIsNone(waiting.proc.poll(), "the shell ended before Enter")
                    waiting.enter()
                    self.assertEqual(waiting.proc.wait(timeout=30), 0, "read -r took the Enter")
                finally:
                    waiting.close()

    def test_a_declared_run_link_on_a_windows_drive_runs_in_the_project_and_waits_on_a_failure(self):
        log = self.base / "ran.log"
        self.declared_profile([
            {"label": "Where", "mark": "site",
             "run": f"(Get-Location).ProviderPath | Out-File -LiteralPath '{log}' # a trailing comment (round 1)"},
            {"label": "Exits", "mark": "site", "run": "cmd /c exit 3"},
            {"label": "Throws", "mark": "site", "run": "throw 'boom'"},
            {"label": "Quits", "mark": "site", "run": "exit 3"}])
        done = self.run_script(terminal=False)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        where = self.read_named("Example Site (Where)")
        self.assertEqual(Path(where["target"]).name.lower(), "pwsh.exe")
        self.assertTrue(where["arguments"].startswith("-NoLogo -NoProfile -Command "), where["arguments"])
        self.assertEqual(folder(where["workdir"]), folder(self.project))
        # Success closes without waiting: no Enter is sent.
        quiet = Interactive(f'"{where["target"]}" {where["arguments"]}', cwd=where["workdir"], env=self.env(False))
        try:
            self.assertEqual(quiet.proc.wait(timeout=60), 0, quiet.text())
            self.assertNotIn("did not start", quiet.text())
        finally:
            quiet.close()
        self.assertEqual(folder(log.read_text(encoding="utf-8-sig").strip()), folder(self.project))
        for label in ("Exits", "Throws", "Quits"):
            with self.subTest(label=label):
                link = self.read_named(f"Example Site ({label})")
                waiting = Interactive(f'"{link["target"]}" {link["arguments"]}', cwd=link["workdir"],
                                      env=self.env(False))
                try:
                    self.assertTrue(waiting.wait_for(f"{label} did not start: the reason is above. Press Enter to "
                                                     "close."), waiting.text())
                    time.sleep(2)
                    self.assertIsNone(waiting.proc.poll(), "the window closed before Enter")
                    waiting.enter()
                    waiting.proc.wait(timeout=30)
                finally:
                    waiting.close()

    def test_a_declared_url_link_opens_through_rundll32_and_is_never_started_here(self):
        self.declared_profile(DECLARED[1:])
        done = self.run_script()
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        link = self.read_named("Example Site (Site)")
        self.assertEqual(folder(link["target"]), folder(Path(os.environ["SystemRoot"]) / "System32/rundll32.exe"))
        self.assertEqual(link["arguments"], "url.dll,FileProtocolHandler https://example.com/wp-admin/?a=1&b=2")
        self.assertTrue(link["description"].startswith("conclave-launcher: "))

    def test_declared_icons_are_each_the_renderer_output_for_their_own_mark(self):
        self.declared_profile(DECLARED)
        done = self.run_script()
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        icons = {}
        for name, mark in (("Owner's Dashboard", "wordpress"), ("Site", "site"), ("Claude", "claude")):
            link = self.read_named(f"Example Site ({name})")
            path = link["icon"]
            self.assertEqual(link["icon_index"], 0)
            expected = self.base / f"expected-{mark}.ico"
            made = run_tree([sys.executable, "-B", str(SCRIPTS / "campaign-icon.py"), "overlay",
                             str(self.project / "assets/icon.png"), mark, str(expected)], timeout=120)
            self.assertEqual(made.returncode, 0, made.stdout + made.stderr)
            self.assertEqual(Path(path).read_bytes(), expected.read_bytes(), name)
            icons[name] = Path(path).name
        self.assertTrue(icons["Owner's Dashboard"].startswith("launcher-owner-s-dashboard-"), icons)
        self.assertTrue(icons["Site"].startswith("launcher-site-"), icons)
        self.assertEqual(len(set(icons.values())), 3)

    def test_a_declared_link_dropped_from_the_profile_goes_with_its_icon_and_the_rest_stay(self):
        self.declared_profile(DECLARED)
        self.assertEqual(self.run_script().returncode, 0)
        self.assertEqual(self.links(), ["Example Site (Claude).lnk", "Example Site (Owner's Dashboard).lnk",
                                        "Example Site (Site).lnk"])
        kept = {name: self.read_named(f"Example Site ({name})")["icon"]
                for name in ("Claude", "Site")}
        self.declared_profile(DECLARED[1:])
        done = self.run_script()
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(self.links(), ["Example Site (Claude).lnk", "Example Site (Site).lnk"])
        self.assertEqual(sorted(p.name for p in self.icons.iterdir()), sorted(Path(p).name for p in kept.values()))
        self.assertEqual(self.leftovers(), [])
        removed = self.run_script("-Remove")
        self.assertEqual(removed.returncode, 0, removed.stdout)
        self.assertEqual((self.links(), list(self.icons.iterdir())), ([], []))

    # --- Unicode links, ownership errors, and missing ShellLink.cs (D07 T01 section 9) ------------------------------

    def test_unicode_title_and_fields_persist_rerun_cleanup_rollback_and_remove(self):
        # Bracketed Unicode checkout path also verifies Add-Type -LiteralPath does not treat '[' as a wildcard.
        project = self.make_project("プロジェクト [東京]", {
            "schema_version": 1, "title": "東京", "icon": "assets/icon.png",
            "agents": ["claude"], "skip_permissions": False,
            "links": [{"label": "Site 東京", "mark": "site", "url": "https://example.com/?q=東京"}],
        })
        icons = self.base / "icons-東京"
        foreign = self.desk / "Foreign.lnk"
        self.desk.mkdir(parents=True, exist_ok=True)
        foreign.write_bytes(b"foreign")
        done = self.run_script(project=project, icons=icons)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(self.links(), ["Foreign.lnk", "東京 (Claude).lnk", "東京 (Site 東京).lnk"])
        self.assertEqual(self.leftovers(icons=icons), [])
        identity = str(project.resolve()).lower()
        key = hashlib.sha256(identity.encode("utf-8")).hexdigest()[:8]
        expected_mark = f"conclave-launcher: {identity}"
        agent = self.read_named("東京 (Claude)")
        self.assertEqual(folder(agent["target"]), folder(self.wt_dir / "wt.exe"))
        self.assertEqual(folder(agent["workdir"]), folder(project))
        self.assertEqual(agent["description"].lower(), expected_mark)
        self.assertEqual(folder(Path(agent["icon"]).parent), folder(icons))
        self.assertEqual(Path(agent["icon"]).name, f"launcher-claude-{key}.ico")
        self.assertEqual(agent["icon_index"], 0)
        self.assertTrue(Path(agent["icon"]).is_file())
        self.assertEqual(self.run_link(agent).returncode, 0)
        (call,) = self.terminal_calls()
        self.assertEqual(folder(call["cwd"]), folder(project))
        self.assertEqual((call["argv"][0], folder(call["argv"][1])), ("-d", folder(project)))
        self.assertEqual(Path(call["argv"][2]).name.lower(), "pwsh.exe")
        self.assertEqual(call["argv"][3:], ["-NoLogo", "-NoExit", "-Command", "claude"])
        url_link = self.read_named("東京 (Site 東京)")
        self.assertEqual(folder(url_link["target"]), folder(Path(os.environ["SystemRoot"]) / "System32/rundll32.exe"))
        self.assertEqual(url_link["arguments"], "url.dll,FileProtocolHandler https://example.com/?q=東京")
        self.assertEqual(folder(url_link["workdir"]), folder(os.environ["USERPROFILE"]))
        self.assertEqual(url_link["description"].lower(), expected_mark)
        self.assertEqual(folder(Path(url_link["icon"]).parent), folder(icons))
        self.assertEqual(Path(url_link["icon"]).name, f"launcher-site-{key}.ico")
        self.assertEqual(url_link["icon_index"], 0)
        self.assertTrue(Path(url_link["icon"]).is_file())
        # Rerun recognises its own Unicode links (not refused as another's).
        again = self.run_script(project=project, icons=icons)
        self.assertEqual(again.returncode, 0, again.stdout + again.stderr)
        self.assertEqual(self.links(), ["Foreign.lnk", "東京 (Claude).lnk", "東京 (Site 東京).lnk"])
        # Rename title and drop declared link: old Unicode filenames and obsolete icon are removed.
        self.write_profile(project, {
            "schema_version": 1, "title": "大阪", "icon": "assets/icon.png",
            "agents": ["claude"], "skip_permissions": False, "links": [],
        })
        renamed = self.run_script(project=project, icons=icons)
        self.assertEqual(renamed.returncode, 0, renamed.stdout + renamed.stderr)
        self.assertEqual(self.links(), ["Foreign.lnk", "大阪 (Claude).lnk"])
        self.assertEqual(len(list(icons.iterdir())), 1)
        self.assertEqual(self.leftovers(icons=icons), [])
        # Locked-move rollback on a Unicode installation keeps old bytes and foreign link, then retry succeeds.
        before = {p.name: p.read_bytes() for p in [*self.desk.iterdir(), *icons.iterdir()]}
        self.write_profile(project, {
            "schema_version": 1, "title": "東京", "icon": "assets/icon.png",
            "agents": ["claude"], "skip_permissions": False,
            "links": [{"label": "Site 東京", "mark": "site", "url": "https://example.com/?q=東京"}],
        })
        with open(self.desk / "大阪 (Claude).lnk", "rb"):
            failed = self.run_script(project=project, icons=icons)
        self.assertEqual(failed.returncode, 1, failed.stdout + failed.stderr)
        self.assertIn("could not be put in place or removed", failed.stdout)
        self.assertEqual({p.name: p.read_bytes() for p in [*self.desk.iterdir(), *icons.iterdir()]}, before)
        self.assertEqual(self.leftovers(icons=icons), [])
        retried = self.run_script(project=project, icons=icons)
        self.assertEqual(retried.returncode, 0, retried.stdout + retried.stderr)
        self.assertEqual(self.links(), ["Foreign.lnk", "東京 (Claude).lnk", "東京 (Site 東京).lnk"])
        self.assertEqual(len(list(icons.iterdir())), 2)
        # -Remove removes both Unicode links and icons and keeps the foreign link.
        removed = self.run_script("-Remove", project=project, icons=icons)
        self.assertEqual(removed.returncode, 0, removed.stdout + removed.stderr)
        self.assertEqual(self.links(), ["Foreign.lnk"])
        self.assertEqual(list(icons.iterdir()), [])
        self.assertEqual(foreign.read_bytes(), b"foreign")

    def test_wscript_created_everyday_link_upgrades_cleanly(self):
        self.assertEqual(self.run_script().returncode, 0)
        mark = self.read_link()["description"]
        link_path = self.desk / "Example Site (Claude).lnk"
        link_path.unlink()
        made = run_tree([PWSH, "-NoProfile", "-NonInteractive", "-Command",
                         "$s = (New-Object -ComObject WScript.Shell).CreateShortcut($env:LINK_PATH); "
                         "$s.TargetPath = $env:TARGET; $s.Description = $env:MARK; $s.Save()"],
                        timeout=60, env={**os.environ, "LINK_PATH": str(link_path),
                                         "TARGET": str(self.wt_dir / "wt.exe"), "MARK": mark})
        self.assertEqual(made.returncode, 0, made.stdout + made.stderr)
        upgraded = self.run_script()
        self.assertEqual(upgraded.returncode, 0, upgraded.stdout + upgraded.stderr)
        self.assertEqual(self.links(), ["Example Site (Claude).lnk"])
        self.assertEqual(self.leftovers(), [])

    def test_ownership_scan_skips_unrelated_corrupt_and_refuses_corrupt_destination_or_locked_link(self):
        self.declared_profile(DECLARED[1:])
        self.assertEqual(self.run_script().returncode, 0)
        # Unrelated corrupt .lnk is skipped on rerun and -Remove.
        junk = self.desk / "Not A Shortcut.lnk"
        junk.write_bytes(b"corrupt-bytes")
        rerun = self.run_script()
        self.assertEqual(rerun.returncode, 0, rerun.stdout + rerun.stderr)
        self.assertTrue(junk.is_file())
        # Corrupt .lnk at a planned destination refuses before staging and leaves earlier links untouched.
        before = {p.name: p.read_bytes() for p in [*self.desk.iterdir(), *self.icons.iterdir()]}
        corrupt_dest = self.desk / "Example Site (Codex).lnk"
        corrupt_dest.write_bytes(b"corrupt-dest")
        before_with_dest = {**before, corrupt_dest.name: b"corrupt-dest"}
        self.write_profile(self.project, dict(PROFILE, agents=["claude", "codex"], links=DECLARED[1:]))
        bad = self.run_script()
        self.assertEqual(bad.returncode, 1, bad.stdout + bad.stderr)
        self.assertIn("is not this project's link; nothing was written", bad.stdout)
        self.assertEqual({p.name: p.read_bytes() for p in [*self.desk.iterdir(), *self.icons.iterdir()]},
                         before_with_dest)
        self.assertEqual(self.leftovers(), [])
        corrupt_dest.unlink()
        self.write_profile(self.project, dict(PROFILE, agents=["claude"], links=DECLARED[1:]))
        # Exclusively locked owned .lnk refuses both create and -Remove before any change, then succeeds after unlock.
        locked = self.desk / "Example Site (Site).lnk"
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel32.CreateFileW.restype = ctypes.c_void_p
        kernel32.CreateFileW.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p,
                                         ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p]
        kernel32.CloseHandle.argtypes = [ctypes.c_void_p]
        handle = kernel32.CreateFileW(str(locked), 0x80000000, 0, None, 3, 0x80, None)
        self.assertNotEqual(handle, ctypes.c_void_p(-1).value)
        try:
            for args in ((), ("-Remove",)):
                with self.subTest(args=args):
                    refused = self.run_script(*args)
                    self.assertEqual(refused.returncode, 1, refused.stdout + refused.stderr)
                    self.assertIn(locked.name, refused.stdout)
                    self.assertIn("nothing was written", refused.stdout)
                    self.assertEqual(self.leftovers(), [])
                    self.assertEqual(len(list(self.icons.iterdir())), 2)
        finally:
            kernel32.CloseHandle(handle)
        self.assertEqual({p.name: p.read_bytes() for p in [*self.desk.iterdir(), *self.icons.iterdir()]}, before)
        # Unreadable/non-directory ShortcutDir refuses -Remove and preserves IconDir.
        not_a_dir = self.base / "desk-file"
        not_a_dir.write_text("not a folder", encoding="utf-8")
        bad_dir = self.run_script("-Remove", desk=not_a_dir)
        self.assertEqual(bad_dir.returncode, 1, bad_dir.stdout + bad_dir.stderr)
        self.assertIn("the shortcut folder cannot be read", bad_dir.stdout)
        self.assertEqual(len(list(self.icons.iterdir())), 2)
        # Hidden owned link is still found and removed once unlocked.
        self.assertTrue(kernel32.SetFileAttributesW(str(locked), 0x02))
        removed = self.run_script("-Remove")
        self.assertEqual(removed.returncode, 0, removed.stdout + removed.stderr)
        self.assertEqual(self.links(), ["Not A Shortcut.lnk"])
        self.assertEqual(list(self.icons.iterdir()), [])

    def test_missing_shelllink_refuses_create_inspect_and_remove_and_recovers_on_restore(self):
        self.declared_profile(DECLARED[1:])
        self.assertEqual(self.run_script("-Project", "/srv/site", "-Distro", "Ubuntu").returncode, 0)
        before = {p.name: p.read_bytes() for p in [*self.desk.iterdir(), *self.icons.iterdir()]}
        helper = self.project / "scripts/lib/ShellLink.cs"
        saved = helper.read_bytes()
        helper.unlink()
        # Conflicting switches still refuse ahead of the common ShellLink.cs load.
        both = self.run_script("-Project", "/srv/site", "-Distro", "Ubuntu", "-Remove", "-Inspect")
        self.assertEqual(both.returncode, 1, both.stdout + both.stderr)
        self.assertIn("pass either -Remove or -Inspect, not both", both.stdout)
        for args in ((), ("-Inspect",), ("-Remove",)):
            with self.subTest(args=args):
                refused = self.run_script("-Project", "/srv/site", "-Distro", "Ubuntu", *args)
                self.assertEqual(refused.returncode, 1, refused.stdout + refused.stderr)
                self.assertIn("ShellLink.cs", refused.stdout)
                self.assertIn("python forge.py kit <project> launchers", refused.stdout)
                self.assertEqual({p.name: p.read_bytes() for p in [*self.desk.iterdir(), *self.icons.iterdir()]},
                                 before)
        # Restoring ShellLink.cs recovers -Remove even with a malformed profile and unavailable WSL.
        helper.write_bytes(saved)
        self.write_profile(self.project, {"broken": True})
        removed = self.run_script("-Project", "/srv/site", "-Distro", "Ubuntu", "-Remove",
                                  STUB_WSL_FAIL="1", PATH="")
        self.assertEqual(removed.returncode, 0, removed.stdout + removed.stderr)
        self.assertEqual((self.links(), list(self.icons.iterdir())), ([], []))

    def test_foreign_link_placed_during_staging_is_not_overwritten_and_restores_cleanly(self):
        # Completion P2: if a foreign .lnk appears at a planned destination after the pre-staging ownership scan,
        # the commit loop verifies the moved backup's mark, aborts with exit 1, and restores the foreign bytes.
        target_link = self.desk / "Example Site (Claude).lnk"
        foreign_bytes = b"foreign-link-placed-during-staging"
        wrapper_py = self.base / "inject_python.py"
        wrapper_py.write_text(
            "import os, runpy, sys\n"
            "args = sys.argv[1:]\n"
            "if args and args[0] == '-B':\n"
            "    args = args[1:]\n"
            "sys.argv = args\n"
            "try:\n"
            "    runpy.run_path(args[0], run_name='__main__')\n"
            "except SystemExit as exc:\n"
            "    code = exc.code if isinstance(exc.code, int) else (0 if exc.code is None else 1)\n"
            "else:\n"
            "    code = 0\n"
            "if code == 0 and 'overlay' in args:\n"
            "    os.makedirs(os.path.dirname(os.environ['INJECT_LINK']), exist_ok=True)\n"
            "    with open(os.environ['INJECT_LINK'], 'wb') as fh:\n"
            "        fh.write(b'foreign-link-placed-during-staging')\n"
            "sys.exit(code)\n",
            encoding="utf-8",
        )
        wrapper_cmd = self.base / "inject_python.cmd"
        wrapper_cmd.write_text(f'@"{sys.executable}" "{wrapper_py}" %*\r\n', encoding="utf-8")
        command = [PWSH, "-NoProfile", "-NonInteractive", "-File",
                   str(self.project / "scripts/project-shortcuts.ps1"),
                   "-ShortcutDir", str(self.desk), "-IconDir", str(self.icons),
                   "-Wsl", str(self.wsl), "-Python", str(wrapper_cmd)]
        done = run_tree(command, timeout=120,
                        env=self.env(INJECT_LINK=str(target_link)))
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        self.assertIn("changed during the run and is no longer this project's link", done.stdout,
                      done.stdout + done.stderr)
        self.assertEqual(target_link.read_bytes(), foreign_bytes)
        self.assertEqual(list(self.icons.iterdir()), [])
        self.assertEqual(self.leftovers(), [])

    def test_wsl_marked_run_entry_uses_wsl_on_windows_drive_and_wsl_checkouts_and_refuses_without_wsl(self):
        wp_link = [{"label": "WordPress", "mark": "wordpress", "run": "python3 scripts/open-dashboard.py", "wsl": True}]
        self.declared_profile(wp_link)
        # 1. Windows-drive checkout with wsl: true invokes wsl.exe --cd <project> --exec bash -lc ...
        done = self.run_script()
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        link = self.read_named("Example Site (WordPress)")
        ran = self.run_link(link)
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        calls = [json.loads(line) for line in self.wsl_log.read_text(encoding="utf-8").splitlines()]
        self.assertEqual(len(calls), 2, calls)
        self.assertEqual(calls[0], ["-l", "-q"])
        self.assertEqual(calls[1][0], "--cd")
        self.assertEqual(folder(calls[1][1]), folder(self.project))
        self.assertEqual(calls[1][2:5], ["--exec", "bash", "-lc"])
        self.assertEqual(calls[1][5], declared_bash("WordPress"))
        self.assertEqual(calls[1][6:], ["bash", "python3 scripts/open-dashboard.py"])
        # 2. Windows-drive checkout with wsl: true refuses cleanly when wsl.exe is unavailable on PATH.
        no_wsl_desk = self.base / "desk-no-wsl"
        no_wsl_icons = self.base / "icons-no-wsl"
        no_wsl_path = os.pathsep.join(
            p for p in self.plain_path.split(os.pathsep) if p and not (Path(p) / "wsl.exe").exists()
        )
        missing = run_tree(
            [PWSH, "-NoProfile", "-NonInteractive", "-File", str(self.project / "scripts/project-shortcuts.ps1"),
             "-ShortcutDir", str(no_wsl_desk), "-IconDir", str(no_wsl_icons), "-Python", sys.executable],
            timeout=120, env={**self.env(terminal=False), "PATH": no_wsl_path},
        )
        self.assertEqual(missing.returncode, 1, missing.stdout + missing.stderr)
        self.assertIn("wsl.exe is not on PATH", missing.stdout + missing.stderr)
        self.assertFalse(no_wsl_desk.exists() and list(no_wsl_desk.iterdir()))
        # 2b. Windows-drive checkout with wsl: true refuses cleanly when wsl.exe -l -q fails OR succeeds with no distros.
        for label, extra in (("wsl_fail", {"STUB_WSL_FAIL": "1"}), ("empty_distros", {"STUB_WSL_DISTROS": ""})):
            with self.subTest(case=label):
                no_distro = self.run_script(desk=no_wsl_desk, icons=no_wsl_icons, **extra)
                self.assertEqual(no_distro.returncode, 1, no_distro.stdout + no_distro.stderr)
                self.assertIn("WSL has no installed distro", no_distro.stdout + no_distro.stderr)
                self.assertFalse(no_wsl_desk.exists() and list(no_wsl_desk.iterdir()))
        # 3. WSL checkout with wsl: true invokes wsl.exe -d Ubuntu --cd /home/me/site --exec bash -lc ...
        self.wsl_log.unlink(missing_ok=True)
        wsl_desk = self.base / "desk-wsl"
        wsl_icons = self.base / "icons-wsl"
        done_wsl = self.run_script("-Distro", "Ubuntu", "-Project", "/home/me/site", desk=wsl_desk, icons=wsl_icons)
        self.assertEqual(done_wsl.returncode, 0, done_wsl.stdout + done_wsl.stderr)
        wsl_link = self.read_named("Example Site (WordPress)", desk=wsl_desk)
        ran_wsl = self.run_link(wsl_link)
        self.assertEqual(ran_wsl.returncode, 0, ran_wsl.stdout + ran_wsl.stderr)
        wsl_calls = [c for c in (json.loads(line) for line in self.wsl_log.read_text(encoding="utf-8").splitlines())
                     if "--exec" in c and "bash" in c]
        self.assertEqual(len(wsl_calls), 1, wsl_calls)
        self.assertEqual(wsl_calls[0][:7], ["-d", "Ubuntu", "--cd", "/home/me/site", "--exec", "bash", "-lc"])
        self.assertEqual(wsl_calls[0][7], declared_bash("WordPress"))
        self.assertEqual(wsl_calls[0][8:], ["bash", "python3 scripts/open-dashboard.py"])



@unittest.skipUnless(BASH, "the shell tests need a real bash (Git's on Windows)")
class InteractiveShellTest(unittest.TestCase):
    """Why the WSL link runs `bash -lic`: an owner's wrapper for the agent lives in ~/.bashrc after its "not
    interactive: return" guard, which a login shell reaches through ~/.profile (Ubuntu's own shape) only when it is
    interactive. And what an agent that does not start leaves on screen."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.home = Path(self._tmp.name)
        (self.home / ".profile").write_text('if [ -n "$BASH_VERSION" ] && [ -f "$HOME/.bashrc" ]; then\n'
                                            '  . "$HOME/.bashrc"\nfi\n', encoding="utf-8", newline="\n")
        (self.home / ".bashrc").write_text("case $- in\n  *i*) ;;\n  *) return;;\nesac\n"
                                           "conclave_wrapper() { echo wrapped; }\n", encoding="utf-8", newline="\n")

    def bash(self, flags, command, stdin=""):
        return run_tree([BASH, flags, command], timeout=60, input=stdin, cwd=self.home,
                        env={**os.environ, "HOME": str(self.home), "BASH_ENV": ""})

    def test_the_wrapper_is_reached_by_an_interactive_login_shell_only(self):
        self.assertIn("wrapped", self.bash("-lic", "conclave_wrapper").stdout)
        self.assertNotEqual(self.bash("-lc", "conclave_wrapper").returncode, 0)

    def test_an_agent_that_does_not_start_says_so_and_waits_for_enter(self):
        command = bash_command("claude").replace("claude ||", "conclave-no-such-agent ||", 1)
        waiting = Interactive([BASH, "-lic", command], cwd=self.home,
                              env={**os.environ, "HOME": str(self.home), "BASH_ENV": ""})
        try:
            self.assertTrue(waiting.wait_for("Claude did not start: the reason is above. Press Enter to close."),
                            waiting.text())
            time.sleep(2)
            self.assertIsNone(waiting.proc.poll(), "the shell ended before Enter")
            waiting.enter()
            self.assertEqual(waiting.proc.wait(timeout=30), 0, "read -r took the Enter")
        finally:
            waiting.close()


if __name__ == "__main__":
    unittest.main()
