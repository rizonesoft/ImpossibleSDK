#!/usr/bin/env python3
"""Run one independent review round with the reviewer pinned in .conclave/reviewers.toml.

    python3 scripts/review-dispatch.py --writer claude --prompt review-prompt.md --record docs/reviews/x.md
    python3 scripts/review-dispatch.py --kind completion --candidate <40-char SHA> --writer claude \
        --prompt completion-prompt.md --record docs/reviews/x.md     the one-pass completion and excellence review
    python3 scripts/review-dispatch.py --check-pin [--json] [--pin <path>]     the pin's verdict; runs no reviewer
    python3 scripts/review-dispatch.py --list-rounds [--json] [--records <dir>]  recorded rounds; runs no reviewer

`--writer` is the agent whose work is under review (claude, codex or agy, with gemini as another name for agy, so a
route under either name serves both); its route in the pin names exactly one
reviewer, of a different model family, with no fallback. A reviewer that is not installed, exits non-zero, times out,
returns nothing, or ends its reply `OVERALL: blocked` (it could not reach the candidate, for example a sandbox that
cannot read the repository) returned no review, and the reason is recorded under `failover`: exit 1, which never
counts as approval, and no other reviewer runs. A review that finds problems is still a review (exit 0).

`--check-pin` loads the pin with `load_pin`, the only place the rules live, and reports without running anything:
with `--json`, `{"ok": true, "schema_version", "reviewers", "routes", "schedule", "clis": {cli: on PATH}}` (the
`schedule` key only when the pin has one) and exit 0, or `{"ok": false, "error": "<the Refused message>"}` and exit 2;
without `--json`, `ok` or `refused: <message>`.

Reviewers run read-only in the repository root: Codex in its read-only sandbox, Antigravity inside its sandbox with
its file-writing and MCP tools denied by session-only settings (never plan mode, which refuses it every command),
Claude Code in plan mode. The script prints a JSON summary; `--record` also appends a `## Round N` section to
the findings file with an atomic write, and writes nothing (exit 2, the summary still printed) to a findings file it
cannot read back. Works the same from PowerShell and from WSL. Stdlib only.

`--kind completion` runs the one-pass completion and excellence review (`docs/completion-review-prompt.md`) on the
built section before its regular rounds. With `--record` it appends a `## Completion review` section naming the
reviewer, its model and the `--candidate` commit instead of a `## Round N` section, and it is not counted as a round.
A findings file holds one returned completion review: a second is refused (exit 2) and nothing is appended, while a
pass that returned no review (`## Completion review: none`) may be followed by another.

`--kind plan` runs the one-pass plan review (`docs/plan-review-prompt.md`) on the section as written, before the build:
it asks whether building the section exactly as planned produces a 100% complete feature. It is recorded as a
`## Plan review` section with the same one-pass rule, and it is not counted as a round.

When the pin has a `[schedule]`, the dispatcher owns each pass's effort and lenses: the
plan and completion passes take their scheduled effort, and a round's place in `[[schedule.rounds]]` comes from the
rounds that returned a review already recorded in the `--record` file. Rounds past that list rotate one lens per round
through `[schedule.rotation]` (the design lens only with `--surface`), and a low-effort round whose review reads
`HIGHEST: P1` is followed by the `[schedule.escalation]` step. The scheduled lenses are appended to the prompt and
recorded on an `- Lenses:` line. The JSON summary carries `position`, `escalated` and `severity_stop` (true when a
returned round past `severity_stop_after` reads `HIGHEST: P3`: the loop may stop there; with 0, from round 1).

With `policy = "three-pass"` in the pin's `[schedule]` a section takes one plan review before the build, one round
after it, one completion review of the final candidate, and then `--kind reconciliation`
(`docs/reconciliation-review-prompt.md`) only after a completion that returned gaps, again after each reconciliation
that returned gaps. For a findings file named for a section (`<receipts>/<NN-domain>/D<NN>-T<NN>-s<N>.md`, `<receipts>` being `todo/plan.json`'s `layout.receipts`, `docs/reviews` by default) that
order is checked before a reviewer runs, each candidate must be a commit descending from the pass before, and a
reconciliation is refused once the section has spent its `pass_cap` returned passes, after `no_progress_cap`
reconciliations in a row with no less severe a worst finding, after a pass that reports one stamp-holding finding
open in `recurrence_cap` passes (its `RECURRING:` line, backed by the earlier passes its mark cites), and after a pass
whose worst finding is below the pin's `blocking` levels. A three-pass section runs under the pin committed at its plan review's candidate, schedule
and route both; when that three-pass pin no longer loads the pass is refused, never run under the working tree's,
and a plan review is refused while the working tree's three-pass pin is not committed at its candidate. A rotation
section follows the working tree's pin as it always did, with one exception that keeps it on the rules it began
under: once the working tree's pin has become three-pass, it finishes under the rotation pin committed at its plan
review's candidate, and is refused when that commit holds no pin.

Under three-pass every scheduled lens is appended with its question and owes one line `LENS <name>: clean` or
`LENS <name>: findings`. A reply missing a lens line, its `HIGHEST:` line or the final verdict lines of its kind, or
one that ends complete while declaring a severity that holds the stamp, is recorded with no
reviewer (`## Round N: none`, the reason on its `- Skipped:` line, its text kept), exits 1 and never counts as a pass;
the JSON summary names the lenses in `incomplete`. A `[[schedule.conditional]]` entry's lenses join a pass when a file
changed between the plan review's candidate and the pass's candidate starts with one of its paths; a plan review, and
a review outside a section, take the files from `--paths`. The summary of a returned three-pass review carries
`highest` and `blocking` (whether that severity holds the stamp).
"""
import argparse
import atexit
import ctypes
from datetime import datetime, timezone
import hashlib
import html
import importlib.util
import json
import os
from pathlib import Path
import re
import select
import shutil
import signal
import subprocess
import sys
import tempfile
import unicodedata
import threading
import time

ROOT = Path(__file__).resolve().parents[1]


def _load_beside(name):
    """scripts/<name>.py, loaded from this file's own folder by path: the dispatcher is itself loaded by path (its
    name is not importable), and nothing on sys.path decides which copy runs."""
    path = Path(__file__).resolve().with_name(f"{name}.py")
    if not path.is_file():
        sys.exit(f"review-dispatch: {path} is missing; the reviewer kit installs it beside this script")
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _load_review_pin():
    """scripts/review_pin.py, the one place the pin's rules live."""
    return _load_beside("review_pin")


review_pin = _load_review_pin()
review_layout = _load_beside("review_layout")  # the findings folder, todo/plan.json's layout.receipts
# The pin's rules, under the names this module has always offered them by (the todo kit's stamp reads them here).
Refused, load_pin, PIN = review_pin.Refused, review_pin.load_pin, review_pin.PIN
WRITERS, CLIS, FAMILY, WRITER_FAMILY = review_pin.WRITERS, review_pin.CLIS, review_pin.FAMILY, review_pin.WRITER_FAMILY
EFFORTS, LENSES, THREE_PASS = review_pin.EFFORTS, review_pin.LENSES, review_pin.THREE_PASS
highest, returned_review, scheduled = review_pin.highest, review_pin.returned_review, review_pin.scheduled
lens_block, check_schedule, check_step = review_pin.lens_block, review_pin.check_schedule, review_pin.check_step
declared_highest = review_pin.declared_highest
# Antigravity takes the prompt only as the -p value. Past this size it gets a pointer to the prompt file instead.
# Windows caps a whole command line at 32,767 characters; Linux caps one argument at 128 KiB.
ARGV_PROMPT_LIMIT = 24_000 if os.name == "nt" else 100_000
# A review is prose. Output past this size is a runaway reviewer, not a review.
MAX_OUTPUT_BYTES = 2_000_000
# The prompt's reply format ends with an OVERALL line; `blocked` means the reviewer never saw the work.
BLOCKED = re.compile(r"^\s*(?:OVERALL|COMPLETION):\s*blocked\b", re.IGNORECASE)
POLL_SECONDS = 0.2
# A section's earlier review chain, kept beside its findings file (`D<NN>-T<NN>-s<N>-chain<k>.md`, the todo kit's
# `replan`): closed to new passes.
KEPT_CHAIN = re.compile(r"-s[1-9]\d*-chain[1-9]\d*\.md$", re.IGNORECASE)  # Windows folds the case
CLEANUP_SECONDS = 10
# The last lines each one-pass review owes, by --kind: what makes a reply a returned review of that kind.
FOOTERS = {"completion": ("COMPLETION: complete", "COMPLETION: gaps"),
           "reconciliation": ("RECONCILIATION: complete", "RECONCILIATION: gaps")}
OVERALL = ("OVERALL: approve", "OVERALL: needs-attention")


def valid_one_pass_footer(text, kind):
    """True when a one-pass reply (`completion`, `reconciliation` or `plan`) ends with the verdict lines its prompt
    asks for. The todo kit's stamp requires the same lines."""
    lines = [x.strip() for x in (text or "").splitlines() if x.strip()]
    if kind == "plan":
        return len(lines) >= 2 and lines[-2] in ("PLAN: complete", "PLAN: gaps") and lines[-1] in OVERALL
    return bool(lines) and lines[-1] in FOOTERS.get(kind, ())


def returned_one_pass(entry, kind):
    """A recorded completion, reconciliation or plan pass that is a review: returned by a reviewer of another model
    family than the writer and ending in that kind's verdict lines. The todo kit's stamp uses this same predicate."""
    return bool(entry.get("reviewer")) and entry.get("writer") in WRITER_FAMILY \
        and FAMILY.get(entry.get("cli")) not in (None, WRITER_FAMILY[entry["writer"]]) \
        and valid_one_pass_footer(entry.get("review"), kind)


def ended_gaps(entry):
    """True for a completion or reconciliation whose final line is its `gaps` verdict."""
    lines = [x.strip() for x in (entry.get("review") or "").splitlines() if x.strip()]
    return bool(lines) and lines[-1] in ("COMPLETION: gaps", "RECONCILIATION: gaps")


def decode_reply(data):
    """A reviewer's reply as text: strict UTF-8 when it is, otherwise Windows-1252, the code page a CLI on Windows
    may write its file in, so a curly quote is kept instead of becoming a replacement character. The same on every
    platform, and it never raises: the five bytes Windows-1252 leaves undefined become replacement characters."""
    try:
        return data.decode("utf-8")
    except UnicodeDecodeError:
        return data.decode("cp1252", errors="replace")


EXTENSION = re.compile(r"\.[A-Za-z0-9]+")


def find_cli(name):
    """A reviewer CLI's path, from the absolute directories on PATH only, or None.

    Reviews run from the reviewed repository's root, and Windows `shutil.which` looks in the working directory first
    (always on Python 3.11, and on later ones unless NoDefaultCurrentDirectoryInExePath is set), so a `codex.cmd`
    committed to that repository would be reported as installed and run as the reviewer. Relative PATH entries are
    dropped for the same reason. On Windows the search is done here, by PATHEXT, so no Python version adds the directory.
    """
    directories = [d for d in os.environ.get("PATH", "").split(os.pathsep) if fully_qualified(d)]
    if os.name != "nt":
        return shutil.which(name, path=os.pathsep.join(directories))
    # An empty PATHEXT gets the defaults too, as shutil.which does. The bare name is the last candidate in each
    # directory: npm puts an extensionless shell script beside codex.cmd, and Windows can run only the .cmd.
    # Only entries shaped like an extension: one that holds a separator or ".." would lead out of the PATH directory.
    extensions = [e for e in os.environ.get("PATHEXT", "").split(os.pathsep) if EXTENSION.fullmatch(e)] \
        or [".COM", ".EXE", ".BAT", ".CMD"]
    for directory in directories:
        for candidate in [os.path.join(directory, name + e) for e in extensions] + [os.path.join(directory, name)]:
            if os.path.isfile(candidate):
                return candidate
    return None


def fully_qualified(directory):
    """True for a PATH entry that names one place whatever the working directory: on Windows a drive and a root
    (`C:\\x`) or a UNC share, never `\\x` (the current drive's) or `C:x` (that drive's current directory), which
    os.path.isabs accepts on Python 3.11 and 3.12."""
    if os.name != "nt":
        return os.path.isabs(directory)
    drive, rest = os.path.splitdrive(directory)
    return drive.startswith(("\\\\", "//")) or (len(drive) == 2 and rest[:1] in ("\\", "/"))


def _agy_uses_prompt_file(executable, prompt):
    """Whether an Antigravity invocation passes a prompt-file pointer rather than inline `-p=` text."""
    shim = Path(executable).suffix.lower() in (".cmd", ".bat")
    return shim or len(prompt.encode()) > ARGV_PROMPT_LIMIT or len(prompt) > ARGV_PROMPT_LIMIT


def command(reviewer, executable, prompt, prompt_path, output_path, timeout, effort=None):
    """The argv and stdin for one read-only review. The model comes only from the pin, and the effort from the pin's
    reviewer or, when the pin has a [schedule], from the step it schedules for this pass (Antigravity takes none).
    When neither sets one, no effort argument is passed and the CLI's own configuration decides."""
    cli, model = reviewer["cli"], reviewer["model"]
    if cli == "codex":
        return [executable, "exec", "-m", model, *(["-c", f"model_reasoning_effort={effort}"] if effort else []),
                "--sandbox", "read-only", "--ephemeral", "--skip-git-repo-check", "-o", str(output_path), "-"], prompt
    if cli == "claude":
        return [executable, "-p", "--model", model, *(["--effort", effort] if effort else []),
                "--permission-mode", "plan"], prompt
    text = prompt
    # A Windows .cmd or .bat shim re-parses its arguments through cmd.exe, so prose there could become commands:
    # it only ever gets a pointer to the prompt file. So does a prompt too long for a command line.
    if _agy_uses_prompt_file(executable, prompt):
        text = f"Follow the review instructions in the file {prompt_path} exactly. Do not modify any file."
    model_flag, sandbox_flag, settings_flag, timeout_flag, prompt_flag = AGY_FLAGS
    return [executable, model_flag, model, sandbox_flag, settings_flag, str(agy_settings_path(output_path)),
            timeout_flag, f"{timeout}s", f"{prompt_flag}={text}"], None


# Every flag the dispatcher passes to the Antigravity CLI, in the order `command` uses them. `forge.py doctor` holds
# each against the installed CLI's own --help (D00 T02 §2), so a release that renames one is seen at once instead of
# turning every Antigravity review into "no review".
AGY_FLAGS = ("--model", "--sandbox", "--settings", "--print-timeout", "-p")


def agy_flags_missing(help_text):
    """The flags of AGY_FLAGS the CLI's --help text does not list as whole words (`--print` is not `--print-timeout`)."""
    return [flag for flag in AGY_FLAGS
            if re.search(r"(?<![\w-])" + re.escape(flag) + r"(?![\w-])", help_text) is None]


# An Antigravity reviewer is read-only by its sandbox and these session-only rules, never by plan mode: headless plan
# mode refuses every shell command, `git show` included, whatever allow-rule, trusted workspace or hook says (measured
# on agy 1.5.8, D00 T02 §1), so a reviewer there can read no commit and prints nothing. In the sandbox a command runs
# but cannot write inside or outside the repository (a commit, a tag and a file write were each refused; only the temp
# folder takes a write), and the deny rules refuse the agent's own file-writing, file-editing and MCP tools.
# `--settings` is never saved; it is a file, so a Windows .cmd shim never re-parses JSON as commands.
AGY_REVIEW_SETTINGS = {"permissions": {"deny": ["write_file(*)", "edit_file(*)", "mcp(*)"]}}


def agy_settings_path(output_path):
    """Where the dispatcher writes AGY_REVIEW_SETTINGS for one review: beside the reviewer's other scratch files."""
    return Path(output_path).with_name("agy-settings.json")


# Win32 job objects, by ctypes (standard library): a reviewer started suspended and assigned to a job set to kill on
# close has every descendant it starts in the job, so ending the job ends the whole tree, even a child that outlives
# a reviewer which exited by itself.
CREATE_NEW_PROCESS_GROUP = getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0x00000200)
CREATE_SUSPENDED = 0x00000004
JOB_OBJECT_EXTENDED_LIMIT_INFORMATION_CLASS = 9
JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE = 0x00002000


class _IO_COUNTERS(ctypes.Structure):
    _fields_ = [("ReadOperationCount", ctypes.c_uint64), ("WriteOperationCount", ctypes.c_uint64),
                ("OtherOperationCount", ctypes.c_uint64), ("ReadTransferCount", ctypes.c_uint64),
                ("WriteTransferCount", ctypes.c_uint64), ("OtherTransferCount", ctypes.c_uint64)]


class _JOBOBJECT_BASIC_LIMIT_INFORMATION(ctypes.Structure):
    _fields_ = [("PerProcessUserTimeLimit", ctypes.c_int64), ("PerJobUserTimeLimit", ctypes.c_int64),
                ("LimitFlags", ctypes.c_uint32), ("MinimumWorkingSetSize", ctypes.c_size_t),
                ("MaximumWorkingSetSize", ctypes.c_size_t), ("ActiveProcessLimit", ctypes.c_uint32),
                ("Affinity", ctypes.c_size_t), ("PriorityClass", ctypes.c_uint32),
                ("SchedulingClass", ctypes.c_uint32)]


class _JOBOBJECT_EXTENDED_LIMIT_INFORMATION(ctypes.Structure):
    _fields_ = [("BasicLimitInformation", _JOBOBJECT_BASIC_LIMIT_INFORMATION), ("IoInfo", _IO_COUNTERS),
                ("ProcessMemoryLimit", ctypes.c_size_t), ("JobMemoryLimit", ctypes.c_size_t),
                ("PeakProcessMemoryUsed", ctypes.c_size_t), ("PeakJobMemoryUsed", ctypes.c_size_t)]


_WIN32 = None


def _win32_apis():
    """kernel32 and ntdll with every function this module calls declared, loaded once (Windows only)."""
    global _WIN32
    if _WIN32 is None:
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel32.CreateJobObjectW.argtypes = [ctypes.c_void_p, ctypes.c_wchar_p]
        kernel32.CreateJobObjectW.restype = ctypes.c_void_p
        kernel32.SetInformationJobObject.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_uint32]
        kernel32.SetInformationJobObject.restype = ctypes.c_int
        kernel32.AssignProcessToJobObject.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
        kernel32.AssignProcessToJobObject.restype = ctypes.c_int
        kernel32.TerminateJobObject.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
        kernel32.TerminateJobObject.restype = ctypes.c_int
        kernel32.CloseHandle.argtypes = [ctypes.c_void_p]
        kernel32.CloseHandle.restype = ctypes.c_int
        ntdll = ctypes.WinDLL("ntdll")
        ntdll.NtResumeProcess.argtypes = [ctypes.c_void_p]
        ntdll.NtResumeProcess.restype = ctypes.c_int32
        _WIN32 = kernel32, ntdll
    return _WIN32


def create_windows_job():
    """A new, unconfigured Win32 job object handle; raises OSError."""
    kernel32, _ = _win32_apis()
    job = kernel32.CreateJobObjectW(None, None)
    if not job:
        raise ctypes.WinError(ctypes.get_last_error())
    return job


def configure_windows_job(job):
    """Set JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE on `job`, so every process in it ends when the job ends or its last
    handle closes; raises OSError."""
    kernel32, _ = _win32_apis()
    info = _JOBOBJECT_EXTENDED_LIMIT_INFORMATION()
    info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
    if not kernel32.SetInformationJobObject(job, JOB_OBJECT_EXTENDED_LIMIT_INFORMATION_CLASS, ctypes.byref(info),
                                            ctypes.sizeof(info)):
        raise ctypes.WinError(ctypes.get_last_error())


def assign_windows_job(job, process):
    """Put the suspended `process` in `job`, before it has run an instruction; raises OSError."""
    kernel32, _ = _win32_apis()
    if not kernel32.AssignProcessToJobObject(job, ctypes.c_void_p(int(process._handle))):
        raise ctypes.WinError(ctypes.get_last_error())


def resume_windows_process(process):
    """Let the suspended, already assigned `process` run; raises OSError."""
    _, ntdll = _win32_apis()
    status = ntdll.NtResumeProcess(ctypes.c_void_p(int(process._handle)))
    if status != 0:
        raise OSError(f"NtResumeProcess returned 0x{status & 0xFFFFFFFF:08x}")


def close_windows_job(job):
    """Terminate every process in `job` and close its handle. Called once per job."""
    kernel32, _ = _win32_apis()
    kernel32.TerminateJobObject(job, 1)
    kernel32.CloseHandle(job)


def kill_tree(process, job=None):
    """End a reviewer and everything it started, whether it is still running or has already exited. On Windows that is
    the job the reviewer was started in (see `run`), ended here and its handle closed; on Linux and WSL it is the
    reviewer's whole session. `process` is None when the reviewer never started."""
    if os.name == "nt":
        if job is not None:
            close_windows_job(job)
        if process is not None:
            try:
                process.kill()
            except OSError:
                pass  # already exited
    elif process is not None:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass  # nothing in the group is left to kill


def remove_scratch(path):
    """Remove a reviewer's scratch directory. The reviewer's whole process tree has ended by now, but on Windows a
    file lock can lag a moment behind a process's exit, so a failed removal is retried for a bounded time. A directory
    that still cannot be removed is left behind with a note: the round's outcome is already decided."""
    deadline = time.monotonic() + CLEANUP_SECONDS
    while True:
        try:
            shutil.rmtree(path)
            return
        except FileNotFoundError:
            return
        except OSError as exc:
            if time.monotonic() > deadline:
                print(f"review-dispatch: left {path} behind ({exc.strerror or type(exc).__name__})", file=sys.stderr)
                return
            time.sleep(POLL_SECONDS)


def feed(pipe, text):
    try:
        pipe.write(text.encode("utf-8"))
        pipe.close()
    except (BrokenPipeError, OSError):
        pass  # the reviewer stopped reading or exited; its exit code and output decide the round


# The launcher a POSIX reviewer starts as: it waits for "go" on its gate pipe, then becomes the reviewer. Its status
# pipe closes when the exec succeeds and carries the error when it fails, as subprocess does for its own children.
REVIEWER_GATE = ("import os, sys\n"
                 "gate, status = int(sys.argv[1]), int(sys.argv[2])\n"
                 "go = os.read(gate, 2) == b'go'\n"
                 "os.close(gate)\n"
                 "if not go:\n"
                 "    sys.exit(125)\n"
                 "os.set_inheritable(status, False)\n"
                 "try:\n"
                 "    os.execvp(sys.argv[3], sys.argv[3:])\n"
                 "except OSError as exc:\n"
                 "    os.write(status, (exc.strerror or str(exc)).encode('utf-8', 'replace'))\n"
                 "    sys.exit(127)\n")


def read_exec_status(fd, limit=CLEANUP_SECONDS):
    """What the gate wrote on its status pipe: empty once the reviewer's exec succeeded (the pipe closed on exec),
    the error text when it failed. A launcher that neither execs nor writes within `limit` reads as a failure."""
    chunks, deadline = [], time.monotonic() + limit
    while True:
        left = deadline - time.monotonic()
        if left <= 0 or not select.select([fd], [], [], left)[0]:
            return "the reviewer's launcher did not start it in time"
        chunk = os.read(fd, 4096)
        if not chunk:
            return b"".join(chunks).decode("utf-8", "replace")
        chunks.append(chunk)


def run(argv, stdin_text, timeout, watched):
    """Run one reviewer in its own process group (on Windows, suspended inside a kill-on-close job object and resumed
    once it is in the job), watching the size of every file it writes while it runs, so neither a hang nor a flood of
    output can take the dispatcher or the disk down. The group or job, with every descendant, is ended afterwards
    whatever happened. Returns (exit code, None) or (None, the reason it returned no review)."""
    windows = os.name == "nt"
    group = {"creationflags": CREATE_NEW_PROCESS_GROUP | CREATE_SUSPENDED} if windows else {"start_new_session": True}
    stdout_path = watched[0]
    # stderr is kept only when the caller watches a third file for it (bounded like the others); otherwise discarded.
    errors = open(watched[2], "wb") if len(watched) > 2 else None
    with open(stdout_path, "wb") as output:
        job = process = gate = status = None
        step = "create job"
        try:
            try:
                if windows:
                    job = create_windows_job()
                    step = "configure job"
                    configure_windows_job(job)
                step = "start"
                launch, passed = argv, ()
                if not windows:
                    # The reviewer starts behind a gate: a launcher that waits on a pipe and runs the reviewer only
                    # once its group is in the section lock. A dispatcher killed before then closes the pipe, and the
                    # launcher exits without running a reviewer nobody would account for.
                    if shutil.which(argv[0]) is None:
                        raise FileNotFoundError(2, "No such file or directory", argv[0])
                    gate_read, gate = os.pipe()
                    status, status_write = os.pipe()
                    launch = [sys.executable, "-c", REVIEWER_GATE, str(gate_read), str(status_write), *argv]
                    passed = (gate_read, status_write)
                try:
                    process = subprocess.Popen(launch, cwd=ROOT,
                                               stdin=subprocess.PIPE if stdin_text is not None else subprocess.DEVNULL,
                                               stdout=output, stderr=errors or subprocess.DEVNULL, pass_fds=passed,
                                               **group)
                finally:
                    for fd in passed:
                        os.close(fd)
                if not windows:
                    step = "record the reviewer's group"
                    note_reviewer_group(process.pid)  # start_new_session: its process group id is its pid
                    os.write(gate, b"go")
                    os.close(gate)
                    gate = None
                    step = "start"
                    failure = read_exec_status(status)
                    if failure:
                        raise OSError(failure)
                if windows:
                    step = "assign to job"
                    assign_windows_job(job, process)
                    step = "resume"
                    resume_windows_process(process)
            except (OSError, ValueError) as exc:
                why = getattr(exc, "strerror", None) or str(exc) or type(exc).__name__
                return None, f"could not start: {why}" if step == "start" else f"could not start: {step}: {why}"
            if stdin_text is not None:
                # Fed from a thread: a reviewer that never reads would otherwise block this write past any timeout.
                threading.Thread(target=feed, args=(process.stdin, stdin_text), daemon=True).start()
            deadline = time.monotonic() + timeout
            while process.poll() is None:
                if any(path.is_file() and path.stat().st_size > MAX_OUTPUT_BYTES for path in watched):
                    return None, f"returned more than {MAX_OUTPUT_BYTES} bytes"
                if time.monotonic() > deadline:
                    return None, "timed out"
                try:
                    # Wakes the moment the reviewer exits, and otherwise after one poll interval.
                    process.wait(timeout=POLL_SECONDS)
                except subprocess.TimeoutExpired:
                    continue
        finally:
            if gate is not None:
                os.close(gate)  # the launcher reads no "go" and exits without running the reviewer
            if status is not None:
                os.close(status)
            # The job is ended and closed here exactly once, on every path, then the reviewer itself is reaped.
            kill_tree(process, job)
            if process is not None:
                process.wait()
                if process.stdin is not None:
                    try:
                        process.stdin.close()
                    except OSError:
                        pass
            if errors is not None:
                errors.close()
    if process.returncode != 0:
        return None, f"exited {process.returncode}"
    if any(path.is_file() and path.stat().st_size > MAX_OUTPUT_BYTES for path in watched):
        return None, f"returned more than {MAX_OUTPUT_BYTES} bytes"
    return process.returncode, None


def review(pin, writer, prompt_path, timeout, plan=None, kind="round"):
    """Run the writer's one reviewer. Returns the summary dict: `reviewer` is None when no review came back, and
    `failover` then names the reviewer and why (there is no fallback). `plan` is the scheduled step (see `scheduled`):
    its effort replaces the reviewer's, and its lenses are appended to the prompt and recorded.

    Under the three-pass policy a reply is a review only when it is complete: it ends with the verdict lines of its
    kind and carries one `LENS <name>:` line per scheduled lens. An incomplete reply is returned with `reviewer` None
    like an outage, the reason in `failover`, the missing lenses in `incomplete` and its text kept in `review`, so it
    is recorded and can be read but never counts as a pass."""
    name = review_pin.route_for(pin, writer)
    if name is None:
        raise Refused(f"the reviewer pin has no route for writer {writer!r}")
    try:
        prompt = Path(prompt_path).read_text(encoding="utf-8")
    except UnicodeDecodeError:
        raise Refused(f"the review prompt {prompt_path} is not UTF-8 text; save it as UTF-8 and run again") from None
    if not prompt.strip():
        raise Refused("the review prompt is empty")
    if "\0" in prompt:
        raise Refused("the review prompt contains a NUL byte; no reviewer can be given it")
    reviewer = pin["reviewers"][name]
    # With a schedule the step alone decides, so the record always shows what the schedule says and the stamp can
    # replay it: a step without an effort passes none, whatever the reviewer's own. Without a schedule the reviewer's
    # effort runs. With neither, no effort is passed and the CLI's own configuration decides.
    effort = plan["effort"] if plan else reviewer.get("effort")
    if reviewer["cli"] == "agy":
        # Antigravity takes no effort argument: it runs at the effort its model name carries. That is the scheduled
        # effort whenever the name ends with it (a three-pass pin loads only so), and then the record says so, as the
        # stamp's replay of the schedule expects; otherwise nothing is claimed.
        effort = effort if effort and reviewer["model"].endswith(f"-{effort}") else None
    lenses = plan["lenses"] if plan else []
    schedule = pin.get("schedule", {})
    three_pass = schedule.get("policy") == THREE_PASS
    # Under three-pass every scheduled pass gets the block, also one with no lenses: it asks for the HIGHEST line.
    appended = bool(lenses) or (three_pass and plan is not None)
    if appended:
        prompt = prompt.rstrip("\n") + lens_block(plan, schedule)
    # A reconciliation under a recurrence_cap pin also says how often its worst repeat has been reported open.
    recurrence = three_pass and kind == "reconciliation" and review_pin.recurrence_cap(schedule) is not None
    if recurrence:
        prompt = prompt.rstrip("\n") + review_pin.recurrence_block(schedule)
    # Every dispatched pass receives the single likelihood-weighted severity rubric once, outside the schedule gate.
    prompt = prompt.rstrip("\n") + review_pin.SEVERITY_RUBRIC
    scheduled_fields = {"effort": effort, "lenses": lenses} if plan else {}

    def none(reason):
        return {"writer": writer, "reviewer": None, "failover": [{"reviewer": name, "reason": reason}], "review": None,
                **scheduled_fields}

    # The resolved path, not the bare name: on Windows a CLI is often a .cmd shim that Popen cannot find by name.
    executable = find_cli(reviewer["cli"])
    if executable is None:
        return none(f"{reviewer['cli']} is not installed")
    text = None
    tmp = Path(tempfile.mkdtemp())
    try:
        stdout_path, message_path = tmp / "stdout.txt", tmp / "last-message.md"
        prompt_file = Path(prompt_path).resolve()
        if reviewer["cli"] == "agy" and _agy_uses_prompt_file(executable, prompt):
            # Only file-pointer Antigravity invocations read prompt_file; write it atomically with the rubric.
            prompt_file = tmp / "prompt.md"
            stage = tmp / "prompt.md.tmp"
            stage.write_text(prompt, encoding="utf-8", newline="\n")
            os.replace(stage, prompt_file)
        argv, stdin_text = command(reviewer, executable, prompt, prompt_file, message_path, timeout, effort)
        if reviewer["cli"] == "agy":
            agy_settings_path(message_path).write_text(json.dumps(AGY_REVIEW_SETTINGS), encoding="utf-8")
        stderr_path = tmp / "stderr.txt"
        _, problem = run(argv, stdin_text, timeout, [stdout_path, message_path, stderr_path])
        said = ""
        if problem is None:
            # Codex writes its final message to the -o file; stdout carries its progress, not the review.
            source = message_path if reviewer["cli"] == "codex" else stdout_path
            text = decode_reply(source.read_bytes()) if source.is_file() else ""
            if not text.strip() and stderr_path.is_file():
                # A CLI that printed no review often says why on stderr (a denied tool, a sign-in): its first line.
                lines = [x.strip() for x in stderr_path.read_bytes().decode("utf-8", "replace").splitlines() if x.strip()]
                said = lines[0][:300] if lines else ""
    finally:
        remove_scratch(tmp)
    if problem is None and not (text or "").strip():
        problem = "returned no review" + (f": {said}" if said else "")
    if problem is None:
        problem = blocked(text)
    if problem:
        return none(problem)
    if three_pass:
        final = valid_one_pass_footer(text, kind) if kind in PASSES else \
            ([x.strip() for x in text.splitlines() if x.strip()] or [""])[-1] in OVERALL
        missing = review_pin.missing_lens_verdicts(lenses, text)
        # A completion or reconciliation that ends complete while declaring a finding that holds the stamp
        # contradicts itself: it is not a verdict the section can be closed on.
        worst = declared_highest(text)
        contradicts = kind in FOOTERS and final and not ended_gaps({"review": text}) \
            and worst in review_pin.SEVERITIES and review_pin.blocks(schedule, worst)
        # The severity line is what blocking levels, the no-progress count and the stamp read: without it the reply
        # says nothing they can act on.
        undeclared = worst is None
        # Without its RECURRING line a reconciliation would hide a repeating finding from the recurrence stop.
        unrecurring = recurrence and review_pin.declared_recurring(text) is None
        if not final or missing or contradicts or undeclared or unrecurring:
            why = ([f"no final {kind} verdict line"] if not final else []) \
                + ([f"no LENS verdict line for {', '.join(missing)}"] if missing else []) \
                + (["no HIGHEST line"] if undeclared else []) \
                + (["no single RECURRING line"] if unrecurring else []) \
                + ([f"it ends {kind.upper()}: complete but declares HIGHEST: {worst}, which holds the stamp"]
                   if contradicts else [])
            return {**none(f"incomplete: {' and '.join(why)}"), "review": text.strip(), "incomplete": missing}
    return {"writer": writer, "reviewer": name, "cli": reviewer["cli"], "model": reviewer["model"],
            "effort": effort, "failover": [], "review": text.strip(), **({"lenses": lenses} if plan else {})}


def blocked(text):
    """Why a reply is not a review, when its last line is `OVERALL: blocked` (or `COMPLETION: blocked`); None for a
    review."""
    lines = [line for line in text.strip().splitlines() if line.strip()]
    if not lines or not BLOCKED.match(lines[-1]):
        return None
    reason = next((line.strip() for line in lines[:-1] if not line.lstrip().startswith("#")), "no reason given")
    return f"could not review: {reason[:300]}"


def atomic_write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(dir=path.parent)
    try:
        # newline="": the text is written as given, so a review's own "\r" bytes are stored and nothing is translated.
        with os.fdopen(fd, "w", encoding="utf-8", newline="") as handle:
            handle.write(text)
        os.chmod(temporary, 0o644)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


# A findings file is read back by `list_rounds`, so the envelope `record` writes around each review is unambiguous:
# the metadata states how many lines the review has (`- Lines: N`, lines split on "\n" only) and a digest of them
# (`- Digest:`, the first 16 hex digits of the SHA-256 of the canonical lines joined with "\n"). The reader skips
# exactly N lines and checks the digest, so neither a quoted heading nor an edit of the same length passes for a round.
# The review is stored byte for byte; a line's trailing "\r" is dropped only to compare, so a CRLF checkout reads the
# same. Rounds recorded before the count have no Lines field and run to the next round heading; rounds recorded
# before the digest have Lines without Digest and are read by their count alone.
# Numbers are ASCII digits of bounded length, so the reader's int() can never raise (`\d` also takes "²", and a number
# past Python's digit limit raises). A review is at most MAX_OUTPUT_BYTES, so it has at most MAX_OUTPUT_BYTES + 1 lines,
# and the count's width follows from that: whatever `record` writes, the reader reads.
ROUND_HEADING = re.compile(r"^## Round ([0-9]{1,6}): (none|([a-z0-9][a-z0-9-]*) \(`([^`]+)` via `([a-z]+)`\))$")
ROUND_LINE = re.compile(r"^- (Writer|Run|Candidate|Effort|Lenses|Lines|Digest|Skipped): (\S.*)$")
# The one-pass completion review, recorded beside the rounds and never counted as one. It always carries Lines, Digest
# and the Candidate commit it reviewed; the writer's notes after it (its `- Filed:` lines) run to the next `## ` line.
COMPLETION_HEADING = re.compile(r"^## Completion review: (none|([a-z0-9][a-z0-9-]*) \(`([^`]+)` via `([a-z]+)`\))$")
PLAN_HEADING = re.compile(r"^## Plan review: (none|([a-z0-9][a-z0-9-]*) \(`([^`]+)` via `([a-z]+)`\))$")
RECONCILIATION_HEADING = re.compile(
    r"^## Reconciliation review: (none|([a-z0-9][a-z0-9-]*) \(`([^`]+)` via `([a-z]+)`\))$")
COMPLETION_LINE = re.compile(r"^- (Writer|Run|Candidate|Effort|Lenses|Lines|Digest|Skipped): (\S.*)$")
# The one-pass reviews, by --kind: the heading each is recorded under and the key read_findings lists them under.
PASSES = {"completion": ("## Completion review", COMPLETION_HEADING, "completions"),
          "plan": ("## Plan review", PLAN_HEADING, "plans"),
          "reconciliation": ("## Reconciliation review", RECONCILIATION_HEADING, "reconciliations")}
RESERVED = ("## Round", *(prefix for prefix, _, _ in PASSES.values()))
RESERVED_NAMES = ("headings starting `## Round`, `## Completion review`, `## Plan review` or "
                  "`## Reconciliation review` are reserved for --record")
LENS_NAME = re.compile(r"[a-z0-9][a-z0-9-]*")
CANDIDATE = re.compile(r"[0-9a-f]{40}")
SKIPPED = re.compile(r"^([a-z0-9][a-z0-9-]*) \((.*)\)$")
COUNT = re.compile(r"[0-9]{1,%d}" % len(str(MAX_OUTPUT_BYTES + 1)))
DIGEST = re.compile(r"[0-9a-f]{16}")
IMPLEMENTED_LINE = re.compile(r"^- Implemented:\s*(.+)$")
COMMIT_TOKEN = re.compile(r"[0-9a-fA-F]{4,40}")
REVSPEC_SUFFIX = re.compile(r"(?:[~^](?:[0-9]+|[@!]|-[0-9]*)?)+$")
REPO_QUALIFIED_REF = re.compile(r"^(?:[A-Za-z0-9_.-]+/)+[A-Za-z0-9_.-]+@(.+)$")
REF_LABEL_START = re.compile(r"^[ ]{0,3}\[")
FENCE_OPEN = re.compile(r"^[ ]{0,3}(`{3,}|~{3,})(.*)$")
BLOCKQUOTE_ONE = re.compile(r"^[ ]{0,3}>")
LIST_ITEM_ONE = re.compile(r"^[ ]{0,3}(?:[-*+]|\d{1,9}[.)])")
LIST_PARA_INTERRUPT = re.compile(r"^[ ]{0,3}(?:[-*+]|0{0,8}1[.)])(?:[ \t]+[^ \t].*)$")
THEMATIC_BREAK = re.compile(r"^[ ]{0,3}(?:(?:-[ \t]*){3,}|(?:\*[ \t]*){3,}|(?:_[ \t]*){3,})$")
SETEXT_UNDERLINE = re.compile(r"^[ ]{0,3}(?:=+|-+)[ \t]*$")
ATX_HEADING = re.compile(r"^[ ]{0,3}#{1,6}(?:[ \t]|$)")
ASCII_PUNCT = frozenset("!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~")
CM_ENTITY = re.compile(r"&(?:#[0-9]{1,7}|#[xX][0-9a-fA-F]{1,6}|[A-Za-z][A-Za-z0-9]{1,31});")
CM_URI_AUTOLINK = re.compile(r"^<[A-Za-z][A-Za-z0-9+.-]{1,31}:[^\s<>]*>$")
HTML_TYPE1_OPEN = re.compile(r"^[ ]{0,3}<(?:pre|script|style|textarea)(?:[ \t>]|$)", re.IGNORECASE)
HTML_TYPE1_CLOSE = re.compile(r"</(?:pre|script|style|textarea)>", re.IGNORECASE)
HTML_TYPE2_OPEN = re.compile(r"^[ ]{0,3}<!--")
HTML_TYPE3_OPEN = re.compile(r"^[ ]{0,3}<\?")
HTML_TYPE4_OPEN = re.compile(r"^[ ]{0,3}<![A-Za-z]")
HTML_TYPE5_OPEN = re.compile(r"^[ ]{0,3}<!\[CDATA\[")
HTML_TYPE6_OPEN = re.compile(
    r"^[ ]{0,3}</?(?:address|article|aside|base|basefont|blockquote|body|caption|center|col|colgroup|dd|details|"
    r"dialog|dir|div|dl|dt|fieldset|figcaption|figure|footer|form|frame|frameset|h1|h2|h3|h4|h5|h6|head|header|hr|"
    r"html|iframe|legend|li|link|main|menu|menuitem|nav|noframes|ol|optgroup|option|p|param|search|section|summary|"
    r"table|tbody|td|tfoot|th|thead|title|tr|track|ul)(?:[ \t]|$|/?>)",
    re.IGNORECASE,
)
HTML_TYPE7_OPEN = re.compile(
    r"^[ ]{0,3}(?!<(?:/)?(?:pre|script|style|textarea)(?:[ \t/>]|$))(?:<[A-Za-z][A-Za-z0-9-]*"
    r"(?:[ \t]+[A-Za-z_:][A-Za-z0-9_.:-]*(?:[ \t]*=[ \t]*(?:[^\s\"'=<>`]+|'[^']*'|\"[^\"]*\"))?)*[ \t]*/?>"
    r"|</[A-Za-z][A-Za-z0-9-]*[ \t]*>)[ \t]*$",
    re.IGNORECASE,
)
HTML_INLINE_TAG = re.compile(
    r"<(?:(?P<transparent>/?(?:a|abbr|b|bdi|bdo|cite|code|data|del|dfn|em|i|ins|kbd|mark|q|s|samp|small|span|strong|"
    r"sub|sup|time|u|var)(?:(?:[ \t]+[A-Za-z_:][A-Za-z0-9_.:-]*"
    r"(?:[ \t]*=[ \t]*(?:[^\s\"'=<>`]+|'[^']*'|\"[^\"]*\"))?)*[ \t]*/?)?"
    r"|!-->|!--->|!--[\s\S]*?--)"
    r"|[A-Za-z][A-Za-z0-9-]*(?:[ \t]+[A-Za-z_:][A-Za-z0-9_.:-]*"
    r"(?:[ \t]*=[ \t]*(?:[^\s\"'=<>`]+|'[^']*'|\"[^\"]*\"))?)*[ \t]*/?"
    r"|/[A-Za-z][A-Za-z0-9-]*[ \t]*"
    r"|\?[\s\S]*?\?"
    r"|![A-Za-z][^>]*"
    r"|!\[CDATA\[[\s\S]*?\]\])>",
    re.IGNORECASE,
)
_CM_WS = r"[ \t\n\r\f]"
_CM_HTML_ATTRS = (
    rf"(?:{_CM_WS}+[A-Za-z_:][A-Za-z0-9_.:-]*"
    rf"(?:{_CM_WS}*={_CM_WS}*(?:[^ \t\n\r\f\"'=<>`]+|'[^']*'|\"[^\"]*\"))?)*"
)
CM_HTML_TAG = re.compile(
    rf"<(?:"
    rf"[A-Za-z][A-Za-z0-9-]*{_CM_HTML_ATTRS}{_CM_WS}*/?"
    rf"|/[A-Za-z][A-Za-z0-9-]*{_CM_WS}*"
    rf"|\?[\s\S]*?\?"
    rf"|![A-Za-z][^>]*"
    rf"|!\[CDATA\[[\s\S]*?\]\])>"
)
HTML_SCRIPT_STYLE_TAG = re.compile(
    rf"<(script|style){_CM_HTML_ATTRS}{_CM_WS}*>",
    re.IGNORECASE,
)
HTML_SCRIPT_STYLE_BLOCK = re.compile(
    rf"<(script|style){_CM_HTML_ATTRS}{_CM_WS}*>[\s\S]*?(?:</\1{_CM_WS}*>|$)",
    re.IGNORECASE,
)
FILED_LINE = re.compile(r"^- Filed: D\d{2} T\d{2} §[1-9]\d*(?![\w§]|\.\d)")
WRITER_NOTE = re.compile(r"^## Writer note:(.*)$")


def raw_lines(text):
    """Lines split on "\n" alone, the one boundary `record` and the reader share, every other byte kept. `str.splitlines`
    also splits on "\r", U+2028, U+0085 and others, which a review may contain."""
    return text.split("\n")


def canonical(line):
    """A line as compared and digested: a CRLF checkout's trailing "\r" dropped."""
    return line[:-1] if line.endswith("\r") else line


def digest(lines):
    return hashlib.sha256("\n".join(canonical(line) for line in lines).encode("utf-8")).hexdigest()[:16]


def _read_findings(text):
    """The rounds and completion reviews `record` and `record_completion` wrote in one findings file, as
    `{"rounds": [...], "completions": [...]}`, or None when anything they write is not as they write it.

    A round is a `## Round` heading, a blank line, and a metadata block to the next blank line in which every line is
    one of `record`'s fields (Writer and Run exactly once, Effort, Lines and Digest at most once, any number of Skipped;
    Digest only with Lines). With Lines, the next N lines are the review, skipped unread, and a blank line or the end of
    the file must follow them; with Digest, they must also hash to it, so a hand edit inside a review shows as
    unreadable even when it keeps the count. Without Lines (a round recorded before the count), the round runs to the
    next reserved heading. A round may carry the Candidate commit it reviewed. A completion review, a plan review
    (`## Plan review`) and a reconciliation review (`## Reconciliation review`) are read the same way, with
    Writer, Run, Candidate, Lines and Digest required; the lines after it up to the next `## ` line are its `notes`,
    the writer's own record of what was done with each gap. Lenses, when present, is the comma-separated list the
    schedule gave the pass. Anything else makes the file unreadable rather than losing a field."""
    found = {"rounds": [], "completions": [], "plans": [], "reconciliations": []}
    raw = raw_lines(text)
    lines = [canonical(line) for line in raw]
    i = 0
    while i < len(lines):
        kind = next((k for k, (prefix, _, _) in PASSES.items() if lines[i].startswith(prefix)), None)
        completion = kind is not None
        if not lines[i].startswith("## Round") and not completion:
            i += 1
            continue
        head = i
        heading = (PASSES[kind][1] if completion else ROUND_HEADING).match(lines[i])
        if not heading or i + 1 >= len(lines) or lines[i + 1] != "":
            return None, head
        if completion:
            entry = {"reviewer": heading.group(2), "model": heading.group(3), "cli": heading.group(4), "writer": None,
                     "run": None, "candidate": None, "effort": None, "lenses": None, "skipped": []}
        else:
            entry = {"number": int(heading.group(1)), "reviewer": heading.group(3), "model": heading.group(4),
                     "cli": heading.group(5), "writer": None, "run": None, "candidate": None, "effort": None,
                     "lenses": None, "skipped": []}
        count = sealed = None
        i += 2
        while i < len(lines) and lines[i] != "":
            field = (COMPLETION_LINE if completion else ROUND_LINE).match(lines[i])
            if not field:
                return None, head
            key, value = field.group(1).lower(), field.group(2)
            if key == "skipped":
                skipped = SKIPPED.match(value)
                if not skipped:
                    return None, head
                entry["skipped"].append({"reviewer": skipped.group(1), "reason": skipped.group(2)})
            elif key == "lines":
                # ASCII digits only: "²" passes str.isdigit and int() raises on it.
                if count is not None or not COUNT.fullmatch(value):
                    return None, head
                count = int(value)
            elif key == "digest":
                if sealed is not None or not DIGEST.fullmatch(value):
                    return None, head
                sealed = value
            elif entry[key] is not None or (key == "candidate" and not CANDIDATE.fullmatch(value)):
                return None, head
            elif key == "lenses":
                names = value.split(", ")
                # Any well-formed name: a pin may declare lenses of its own, and the reader has no pin. Whether
                # the names are the scheduled ones is the stamp's check.
                if not all(LENS_NAME.fullmatch(name) for name in names) or len(set(names)) != len(names):
                    return None, head
                entry[key] = names
            else:
                entry[key] = value
            i += 1
        if entry["writer"] is None or entry["run"] is None or (sealed is not None and count is None):
            return None, head
        if completion and (entry["candidate"] is None or sealed is None):
            return None, head
        if count is not None:
            start = i + 1  # the blank line after the metadata, then the review, unread
            i = start + count
            if i > len(lines) or (i < len(lines) and lines[i] != ""):
                return None, head
            if sealed is not None and digest(raw[start:i]) != sealed:
                return None, head
            entry["review"] = "\n".join(lines[start:i])
        else:
            end = next((j for j in range(i, len(lines)) if lines[j].startswith(RESERVED)), len(lines))
            entry["review"] = "\n".join(lines[i:end])
            i = end
        if completion:
            end = next((j for j in range(i, len(lines)) if lines[j].startswith("## ")), len(lines))
            entry["notes"] = [line for line in lines[i:end] if line.strip()]
            tail_end = next((j for j in range(i, len(lines)) if lines[j].startswith(RESERVED)), len(lines))
            entry["writer_raw"] = lines[i:tail_end]
            entry["writer_lines"] = [line for line in lines[i:tail_end] if line.strip()]
            i = tail_end
            found[PASSES[kind][2]].append(entry)
        else:
            found["rounds"].append(entry)
    return found, None


def read_findings(text):
    """See `_read_findings`: the parsed rounds, completion and plan reviews, or None when the file cannot be read."""
    return _read_findings(text)[0]


def unreadable_heading(text):
    """The line where a findings file stops being readable, as `line N: <text>`, so a refusal can name it. A writer's
    own note under a heading that starts `## Round`, `## Completion review` or `## Plan review` is the usual cause:
    those prefixes are reserved for what `--record` writes."""
    found, head = _read_findings(text)
    if found is not None or head is None:
        return ""
    lines = raw_lines(text)
    return f"; the first unreadable record starts at line {head + 1}: {canonical(lines[head])!r}" if head < len(lines) else ""


def read_rounds(text):
    """The rounds `record` wrote in one findings file (completion reviews are not rounds), or None when the file is not
    as `record` and `record_completion` write it (see read_findings)."""
    found = read_findings(text)
    return None if found is None else found["rounds"]


def read_completions(text):
    """The completion reviews `record_completion` wrote in one findings file, or None when it cannot be read back."""
    found = read_findings(text)
    return None if found is None else found["completions"]


def read_plans(text):
    """The plan reviews `--kind plan` recorded in one findings file, or None when it cannot be read back."""
    found = read_findings(text)
    return None if found is None else found["plans"]


class Unrecorded(Exception):
    """The findings file cannot be read back, so a round is not appended to it."""


def existing_findings(path, what):
    """A findings file's text as stored, or `Unrecorded` when it cannot be read."""
    try:
        # Bytes, not read_text: universal newlines would turn a stored lone "\r" into a line break and move the
        # line count of an earlier round.
        return path.read_bytes().decode("utf-8") if path.exists() else ""
    except (OSError, UnicodeDecodeError) as exc:
        raise Unrecorded(f"{path} cannot be read back ({exc}), so the {what} was not recorded") from None


def append_findings(path, existing, lines):
    # Always one blank line before the heading, even when the file already ends in one: a review whose last line is
    # empty also ends the file in "\n\n", and the reader needs a blank line after the review's own lines.
    separator = "" if not existing else ("\n" if existing.endswith("\n") else "\n\n")
    atomic_write(path, existing + separator + "\n".join(lines))


def record(path, result, when, candidate=None):
    """Append one round and return its number, with the `candidate` commit it reviewed when one was named. A findings file the reader cannot read back gets nothing appended:
    numbering it would have to guess (counting headings counts quoted ones), so `Unrecorded` is raised instead."""
    existing = existing_findings(path, "round")
    known = read_rounds(existing)
    if known is None:
        raise Unrecorded(f"{path} cannot be read back (a round heading or line is not in the format --record writes"
                         f"{unreadable_heading(existing)}; {RESERVED_NAMES}), so the round was not recorded")
    number = len(known) + 1
    who = f"{result['reviewer']} (`{result['model']}` via `{result['cli']}`)" if result["reviewer"] else "none"
    review = raw_lines(result["review"] or "No reviewer returned a review. This round is not an approval.")
    if not COUNT.fullmatch(str(len(review))) or not ROUND_HEADING.match(f"## Round {number}: none"):
        raise Unrecorded(f"the review or the round number is past what {path} can be read back with, "
                         f"so the round was not recorded")
    lines = [f"## Round {number}: {who}", "", f"- Writer: {result['writer']}", f"- Run: {when}"]
    lines += [f"- Candidate: {candidate}"] if candidate else []
    lines += metadata(result)
    lines += [f"- Lines: {len(review)}", f"- Digest: {digest(review)}", "", *review, ""]
    append_findings(path, existing, lines)
    return number


def metadata(result):
    lines = [f"- Effort: {result['effort']}"] if result.get("effort") else []
    if result.get("lenses"):
        lines.append(f"- Lenses: {', '.join(result['lenses'])}")
    return lines + [f"- Skipped: {f['reviewer']} ({f['reason']})" for f in result["failover"]]


def record_completion(path, result, when, candidate, kind="completion"):
    """Append the completion review (or, by kind, the plan or a reconciliation review). The plan and completion
    reviews are one pass each: a findings file that already holds a returned one gets nothing appended (`Unrecorded`),
    and neither does one the reader cannot read. Reconciliations repeat; `check_three_pass_order` bounds them."""
    prefix, _, key = PASSES[kind]
    what = f"{kind} review"
    existing = existing_findings(path, what)
    found = read_findings(existing)
    if found is None:
        raise Unrecorded(f"{path} cannot be read back (a heading or line is not in the format --record writes"
                         f"{unreadable_heading(existing)}; {RESERVED_NAMES}), so the {what} was not recorded")
    if kind != "reconciliation" and any(returned_one_pass(entry, kind) for entry in found[key]):
        raise Unrecorded(f"{path} already holds a {what}; it is one pass, so this one was not recorded")
    who = f"{result['reviewer']} (`{result['model']}` via `{result['cli']}`)" if result["reviewer"] else "none"
    review = raw_lines(result["review"] or f"No reviewer returned a {what}. This pass is not an approval.")
    if not COUNT.fullmatch(str(len(review))):
        raise Unrecorded(f"the review is past what {path} can be read back with, so it was not recorded")
    lines = [f"{prefix}: {who}", "", f"- Writer: {result['writer']}", f"- Run: {when}", f"- Candidate: {candidate}"]
    lines += metadata(result)
    lines += [f"- Lines: {len(review)}", f"- Digest: {digest(review)}", "", *review, ""]
    append_findings(path, existing, lines)


def git(*args):
    return subprocess.run(["git", "-C", str(ROOT), *args], capture_output=True, timeout=60)


def descends(older, newer):
    """True when commit `older` is `newer` or an ancestor of it."""
    return git("merge-base", "--is-ancestor", older, newer).returncode == 0


def receipts_folder():
    """The project's findings folder, repository-relative (todo/plan.json's layout.receipts, docs/reviews by
    default); a layout that cannot be read is refused."""
    try:
        return review_layout.receipts_folder(ROOT)
    except review_layout.LayoutError as exc:
        raise Refused(str(exc)) from None


def section_findings_pattern():
    """A section's findings file, `<receipts>/<NN-domain>/D<NN>-T<NN>-s<N>.md`. Windows opens `DOCS/Reviews/...` as
    the same file, so there the name is matched without regard to case: a section's rules cannot be stepped around by
    respelling its findings file."""
    return re.compile(re.escape(receipts_folder())
                      + r"/[0-9]{2}-[A-Za-z0-9_-]+/D[0-9]{2}-T[0-9]{2}-s[1-9][0-9]*\.md$",
                      re.IGNORECASE if os.name == "nt" else 0)


def section_record(record_path):
    """True for a findings file named for a TODO section, `<receipts>/<NN-domain>/D<NN>-T<NN>-s<N>.md`: the file
    whose passes have an order. Any other findings file is a standalone review. The folder is the repository's, so
    the path is matched whole from the repository root: `docs/reviews/...` is not a file in a folder named `reviews`."""
    if not record_path:
        return False
    try:
        relative = Path(os.path.realpath(record_path)).relative_to(ROOT).as_posix()
    except ValueError:  # outside the repository: never a section's findings file
        return False
    return bool(section_findings_pattern().fullmatch(relative))


def returned_plan(record_path):
    """The one returned plan review recorded in a findings file, or None."""
    if not record_path or not Path(record_path).exists():
        return None
    found = read_findings(existing_findings(Path(record_path), "review"))
    plans = [e for e in (found or {"plans": []})["plans"] if returned_one_pass(e, "plan")]
    return plans[0] if len(plans) == 1 else None


def is_three_pass(pin):
    return pin is not None and pin.get("schedule", {}).get("policy") == THREE_PASS


def committed_pin(sha):
    """The pin committed at `sha`, or None when that commit holds none (or holds an earlier pin that no longer loads
    and was never three-pass). A three-pass pin there that does not load is a refusal naming the commit."""
    shown = git("show", f"{sha}:.conclave/reviewers.toml")
    if shown.returncode != 0:
        return None
    try:
        return load_pin(data=shown.stdout)
    except Refused as exc:
        if THREE_PASS.encode() not in shown.stdout:
            return None
        raise Refused(f"the reviewer pin at {sha} does not load: {exc}") from None


def current_schedule(candidate):
    """The [schedule] of the pin committed at `candidate`, or None: what a section under review reads its cap against
    (`review_pin.section_cap`). A pin there that does not load never stops a section, which has its own."""
    try:
        held = committed_pin(candidate)
    except Refused:
        return None
    return held.get("schedule") if held else None


def section_pin(pin_path, record_path, kind, candidate):
    """The pin a pass runs under.

    A three-pass section keeps the pin its plan review read: the plan review runs under the three-pass pin committed
    at its own candidate, and every later pass under the one at its returned plan review's candidate, schedule and
    route both, so a pin change (or a broken working-tree pin) never moves a section already under review. That pin
    is never replaced by the working tree's: when the working tree's pin is three-pass but the plan review's
    candidate does not hold it, the plan review is refused until the pin is committed, so no section starts unfrozen.

    Everything else takes the pin at `pin_path`, as it always did: a findings file not named for a section, a section
    with no returned plan review, and a rotation section. One exception keeps a section on the rules it began under:
    when the working tree's pin has since become three-pass, a section whose plan candidate holds a rotation pin
    finishes under that committed pin. The todo kit's stamp replays the same rule."""
    if not section_record(record_path):
        return load_pin(pin_path)
    plan = returned_plan(record_path)
    sha = candidate if kind == "plan" else plan["candidate"] if plan else None
    held = committed_pin(sha) if sha else None
    if is_three_pass(held):
        return held
    working = load_pin(pin_path)
    if not is_three_pass(working) or sha is None:
        return working
    if kind == "plan":
        raise Refused(f"the pin is three-pass, but the plan review's candidate {sha} does not hold it: commit "
                      f".conclave/reviewers.toml first, so the section keeps the pin its plan review read")
    if held is None:
        raise Refused(f"this section's plan review ran at {sha}, which holds no reviewer pin, and the pin has since "
                      f"become three-pass: there is no pin for the section to finish under")
    return held


def changed_paths(base, candidate):
    """The files that differ between two commits, as repository-relative paths with forward slashes. Renames are
    listed as the file removed and the file added, so a file moved out of a watched folder still counts for it."""
    shown = git("-c", "core.quotepath=false", "diff", "--name-only", "--no-renames", "-z", base, candidate)
    if shown.returncode != 0:
        raise Refused(f"cannot list the files changed between {base} and {candidate}")
    return [name for name in shown.stdout.decode("utf-8", errors="replace").split("\0") if name]


SEVERITY_RANK = {"P1": 3, "P2": 2, "P3": 1, "none": 0, None: 3}
# How long past its owner's own timeout a section lock is believed: a run that was killed cannot hold it for ever.
LOCK_GRACE_SECONDS = 300
# How long a takeover of an expired lock may itself take before it is treated as abandoned.
TAKEOVER_SECONDS = 60


def section_lock_path(record_path):
    """Where the lock of one section's findings file lives: the system's temporary folder, never the repository, so
    a run that is killed leaves nothing in the working tree."""
    # realpath, not abspath: a folder reached by its 8.3 short name (GitHub's Windows runner hands out its user folder as RUNNER~1)
    # and by its own name is one section, so the dispatcher and the todo kit's replan take one lock (tests.yml run
    # 44492505122).
    name = os.path.normcase(os.path.realpath(record_path)).encode("utf-8")
    return Path(tempfile.gettempdir()) / f"review-dispatch-{hashlib.sha256(name).hexdigest()[:16]}.lock"


def _create_exclusive(path, text):
    """Create `path` holding `text` only if it does not exist: True when this process created it."""
    try:
        fd = os.open(path, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
    except FileExistsError:
        return False
    try:
        os.write(fd, text.encode("ascii"))
    finally:
        os.close(fd)
    return True


def _lock_expiry(path):
    """When the lock at `path` stops being believed, from what its owner wrote in it (`<pid> <expiry> <token>`), or
    None when there is no lock. A lock that cannot be read (half written, or not ours) is believed for the grace
    period from its last change."""
    try:
        return float(path.read_text(encoding="ascii", errors="replace").split()[1])
    except FileNotFoundError:
        return None
    except (IndexError, ValueError, OSError):
        try:
            return path.stat().st_mtime + LOCK_GRACE_SECONDS
        except OSError:
            return None


def _start_time(pid):
    """The kernel's start time of process `pid` (field 22 of /proc/<pid>/stat), or "0" where /proc cannot say: with
    the process id it names one process, so a reused id is never mistaken for the reviewer."""
    try:
        with open(f"/proc/{pid}/stat", encoding="ascii", errors="replace") as handle:
            return handle.read().rpartition(")")[2].split()[19]
    except (OSError, IndexError):
        return "0"


def _group_members(group):
    """(session id, start time) of every process in process group `group`, read from /proc; empty where /proc cannot
    say."""
    members = []
    try:
        names = os.listdir("/proc")
    except OSError:
        return members
    for name in names:
        if not name.isdigit():
            continue
        try:
            with open(f"/proc/{name}/stat", encoding="ascii", errors="replace") as handle:
                fields = handle.read().rpartition(")")[2].split()
            if int(fields[2]) == group:
                members.append((int(fields[3]), int(fields[19])))
        except (OSError, IndexError, ValueError):
            continue  # it ended while being read, or is not ours to read
    return members


def _is_recorded_group(group, started):
    """Whether process group `group` is still the reviewer's group the lock recorded. With its leader alive, the
    leader's start time must be the one recorded. Once the leader has exited, Linux gives no new process its id while
    the group lives, so the group is the reviewer's when every member is in the reviewer's own session (its id is the
    group's) and started no earlier than the leader did."""
    if started == "0":
        return False
    leader = _start_time(group)
    if leader != "0":
        return leader == started
    members = _group_members(group)
    return bool(members) and all(session == group and start >= int(started) for session, start in members)


def _group_alive(group):
    try:
        os.killpg(group, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True  # alive, another user's
    return True


def _end_overdue_group(group, started):
    """End a reviewer group whose dispatcher was killed and whose deadline has passed, as that dispatcher would have:
    only when the group is still the one the lock recorded (`_is_recorded_group`). True when the group is gone."""
    if not _is_recorded_group(group, started):
        return False
    try:
        os.killpg(group, signal.SIGKILL)
    except ProcessLookupError:
        return True
    deadline = time.monotonic() + CLEANUP_SECONDS
    while _group_alive(group) and time.monotonic() < deadline:
        time.sleep(POLL_SECONDS)
    return not _group_alive(group)


def _lock_free(path):
    """Whether the lock at `path` may be taken over now. A lock past the expiry its owner wrote is free, and so is one
    whose owner process is certainly gone (killed outright, so it never released). On Linux and WSL the lock also
    names the reviewer's process group, which outlives a killed dispatcher in its own session: while that group
    lives the lock is never free, and once its dispatcher is dead and its deadline past, the group is ended first. A
    lock that cannot be read is believed for the grace period from its last change."""
    expiry = _lock_expiry(path)
    if expiry is None:
        return True
    try:
        fields = path.read_text(encoding="ascii", errors="replace").split()
    except FileNotFoundError:
        return True
    except OSError:
        fields = []
    try:
        pid = int(fields[0])
        owner_gone = pid > 0 and pid != os.getpid() and _process_gone(pid)
    except (IndexError, ValueError):
        owner_gone = False
    group = started = None
    if len(fields) >= 4 and os.name != "nt":
        try:
            group_text, _, started = fields[3].partition(":")
            group = int(group_text)
        except ValueError:
            return False  # a group this check cannot read is believed
    past = time.time() > expiry
    if group is not None and _group_alive(group):
        return owner_gone and past and _end_overdue_group(group, started or "0")
    return past or owner_gone


def _process_gone(pid):
    """True only when no process has the id `pid`."""
    if os.name == "nt":
        import ctypes
        from ctypes import wintypes

        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel32.OpenProcess.restype = wintypes.HANDLE
        kernel32.OpenProcess.argtypes = (wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
        handle = kernel32.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
        if not handle:
            return ctypes.get_last_error() == 87  # ERROR_INVALID_PARAMETER: no process has this id
        try:
            code = wintypes.DWORD()
            kernel32.GetExitCodeProcess.argtypes = (wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD))
            if not kernel32.GetExitCodeProcess(handle, ctypes.byref(code)):
                return False
            return code.value != 259  # STILL_ACTIVE
        finally:
            kernel32.CloseHandle(handle)
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return True
    except PermissionError:
        return False  # alive, owned by another user
    return False


# The section lock this run holds, so the reviewer's process group can be added to it once the reviewer starts.
_HELD = {}


def note_reviewer_group(group):
    """Add the reviewer's process group to the lock this run holds (Linux and WSL), written through a temporary file
    and os.replace, and only while the lock still holds what this run wrote."""
    path, owned = _HELD.get("path"), _HELD.get("owned")
    if path is None or os.name == "nt":
        return
    # A failure here raises: the reviewer is not let through its gate unless the lock accounts for it.
    noted = f"{owned} {group}:{_start_time(group)}"
    if path.read_text(encoding="ascii", errors="replace") != owned:
        raise OSError(f"the section lock {path} is no longer this run's")
    temporary = path.with_name(f"{path.name}.{os.getpid()}.tmp")
    try:
        temporary.write_text(noted, encoding="ascii")
        os.replace(temporary, path)
    except OSError:
        temporary.unlink(missing_ok=True)
        raise
    _HELD["owned"] = noted


def _release_held():
    if _HELD.get("path") is not None:
        release_section(_HELD["path"], _HELD["owned"])


def release_section(path, owned):
    """Remove the lock at `path` only while it still holds what this process wrote: never another run's lock."""
    try:
        if path.read_text(encoding="ascii", errors="replace") == owned:
            path.unlink()
    except FileNotFoundError:
        return  # already gone: there is nothing of ours left to release
    except OSError as exc:
        # The lock then stands until the expiry written in it; say so rather than leave it unexplained.
        print(f"review-dispatch: left the lock {path} behind ({exc.strerror or type(exc).__name__})", file=sys.stderr)


def lock_section(record_path, timeout):
    """Take a three-pass section's lock for the rest of this run: the order check, the review and the record happen
    under it, so a second launch of the same pass sees the first one's record instead of spending another review.

    The owner writes its own expiry into the lock (its start, its own `--timeout` and a grace period), so a later
    launch with a shorter timeout cannot decide a live review is dead; a lock whose owner process is certainly gone
    (killed outright, so it never released) is taken over at once. Only such a lock, or one past the expiry its
    owner wrote, is taken over, by one process at a time (a second, exclusive takeover file), and a process
    releases only the lock it wrote, when it exits."""
    path = section_lock_path(record_path)
    owned = f"{os.getpid()} {time.time() + timeout + LOCK_GRACE_SECONDS:.0f} {os.urandom(8).hex()}"
    busy = Refused(f"another review of {record_path} is already running (lock {path}); a section takes one pass at "
                   f"a time")
    if not _create_exclusive(path, owned):
        if not _lock_free(path):
            raise busy
        takeover = path.with_name(path.name + ".takeover")
        if not _create_exclusive(takeover, owned):
            # Someone else is taking the expired lock over; an abandoned takeover is cleared for the next launch.
            try:
                if time.time() - takeover.stat().st_mtime > TAKEOVER_SECONDS:
                    takeover.unlink()
            except FileNotFoundError:
                raise busy from None  # the other launch finished its takeover just now
            raise busy
        try:
            # Looked at again under the takeover: the lock may have been replaced since it was first read.
            if not _lock_free(path):
                raise busy
            path.unlink(missing_ok=True)
            if not _create_exclusive(path, owned):
                raise busy
        finally:
            release_section(takeover, owned)
    _HELD.update(path=path, owned=owned)
    atexit.register(_release_held)


def section_ref(record_path):
    """`D<NN> T<NN> §<N>` for a section's findings file (`.../D07-T02-s1.md`), or None."""
    m = re.fullmatch(r"D(\d{2})-T(\d{2})-s([1-9]\d*)\.md", Path(record_path).name)
    return f"D{m[1]} T{m[2]} §{m[3]}" if m else None


def contract_changed(record_path, before, after):
    """Whether a reconciliation that ended complete on `after` leaves the chain unsettled, by the todo kit's own rule
    (`reconciliation_settles`, which `stamp` applies too): its candidate's section contract differs from the failed
    completion's on `before`. False when the todo kit's library is not beside this script or cannot answer: then
    nothing here knows the section."""
    path = ROOT / "scripts" / "lib" / "todo_graph.py"
    ref = section_ref(record_path)
    if ref is None or not path.is_file():
        return False
    try:
        spec = importlib.util.spec_from_file_location("_dispatch_todo_graph", path)
        module = importlib.util.module_from_spec(spec)
        # Registered before it runs: its dataclasses look their module up there while being defined.
        sys.modules[spec.name] = module
        spec.loader.exec_module(module)
        settles = getattr(module, "reconciliation_settles", None)
        if settles is not None:
            return not settles(ROOT, ref, before, after)
        # A todo kit from before D01 T02 §11 (the kits update apart): its section_contract answers the same question.
        contract = getattr(module, "section_contract", None)
        return contract is not None and contract(ROOT, before, ref) != contract(ROOT, after, ref)
    except (ValueError, OSError, ImportError, SyntaxError):
        # The library's refusal (a section it cannot find, a commit without the file) or one that does not load: no
        # contract to compare, so the order stands as the findings file alone says.
        return False


def check_three_pass_order(kind, record_path, candidate, schedule):
    """The order of a three-pass section's passes: the plan review, one round on a candidate descending from it, one
    completion review, then reconciliations only after a completion that returned gaps, each on a candidate
    descending from the pass before, until one returns complete. A reconciliation is also refused once the section
    has spent its `pass_cap` returned passes (`review_pin.section_cap`), after `no_progress_cap` reconciliations in a
    row whose worst finding is no less severe than the pass before, and after a pass whose worst finding is below the
    pin's `blocking` levels (those findings are filed, not reviewed again). Refused here so that a pass out of order
    never spends a review; the stamp holds the receipt to the same order. A findings file not named for a section has no order."""
    if not section_record(record_path):
        return
    raw_text = existing_findings(Path(record_path), f"{kind} review")
    found = read_findings(raw_text)
    if found is None:
        raise Unrecorded(f"{record_path} cannot be read back ({RESERVED_NAMES}), so no {kind} review was run")
    plans = [e for e in found["plans"] if returned_one_pass(e, "plan")]
    rounds = [e for e in found["rounds"] if returned_review(e)]
    completions = [e for e in found["completions"] if returned_one_pass(e, "completion")]
    reconciled = [e for e in found["reconciliations"] if returned_one_pass(e, "reconciliation")]
    if candidate and git("cat-file", "-e", f"{candidate}^{{commit}}").returncode != 0:
        raise Refused(f"--candidate {candidate} is not a commit in this repository")
    if kind == "plan":
        if plans or rounds or completions:
            raise Refused("the plan review runs once, before the build and every other pass")
        return
    if len(plans) != 1:
        raise Refused(f"the {kind} pass needs the section's returned plan review first")
    if kind == "round":
        if rounds or completions:
            raise Refused("the three-pass policy allows one round, before the completion review")
        if not candidate:
            raise Refused("a section's round needs --candidate, the commit under review")
        if not descends(plans[0]["candidate"], candidate):
            raise Refused(f"the round's candidate does not descend from the plan review's {plans[0]['candidate']}")
    elif kind == "completion":
        if len(rounds) != 1 or completions:
            raise Refused("the completion review runs once, after the section's returned round")
        if rounds[0]["candidate"] is None or not descends(rounds[0]["candidate"], candidate):
            raise Refused(f"the completion's candidate does not descend from the round's {rounds[0]['candidate']}")
    elif kind == "reconciliation":
        if len(rounds) != 1 or len(completions) != 1 or not ended_gaps(completions[0]):
            raise Refused("a reconciliation follows one round and a completion review that returned gaps")
        chain = [completions[0], *reconciled]
        # A reconciliation that ended complete on a candidate whose contract differs from the failed completion's
        # cannot be stamped (`stamp` compares the two), so it settles nothing: the section takes another
        # reconciliation, on a candidate that restores the contract. Without this a section would be stuck for good
        # (D07 T02 §1, 2026-11-17).
        unsettled = not ended_gaps(chain[-1]) and contract_changed(record_path, completions[0]["candidate"],
                                                                    chain[-1]["candidate"])
        if not ended_gaps(chain[-1]) and not unsettled:
            raise Refused("the section's reconciliation already returned complete; it needs no further pass")
        worst = [declared_highest(e["review"]) for e in chain]
        # Only a pin that names its blocking levels ends the loop this way; without the key every gap is repaired.
        if "blocking" in schedule and not unsettled and not review_pin.blocks(schedule, worst[-1]):
            raise Refused(f"the last pass's worst finding is {worst[-1]}, below the pin's blocking levels "
                          f"({', '.join(schedule['blocking'])}): file its findings and stamp; it takes no further pass")
        cap, ships = review_pin.section_cap(schedule, current_schedule(candidate))
        if len(plans) + len(rounds) + len(chain) >= cap:
            if ships and unsettled:
                raise Refused(f"the section has spent its {cap} review passes (pass_cap) and ships (at_cap): restore "
                              f"the section text the failed completion reviewed, rerun the gates, and stamp it capped")
            if ships:
                raise Refused(f"the section has spent its {cap} review passes (pass_cap) and ships (at_cap): fix the "
                              f"last pass's findings, rerun the gates, file what is left, and stamp it capped")
            raise Refused(f"the section has spent its {cap} review passes (pass_cap); it stays blocked")
        stall = review_pin.no_progress_cap(schedule)
        if stall is not None:
            run = 0
            for before, after in reversed(list(zip(worst, worst[1:]))):
                if SEVERITY_RANK[after] < SEVERITY_RANK[before]:
                    break
                run += 1
            if run >= stall and ships and unsettled:
                raise Refused(f"the section's last {run} reconciliations made no progress (no_progress_cap {stall}), "
                              f"so it ships (at_cap): restore the section text the failed completion reviewed, rerun "
                              f"the gates, and stamp it capped")
            if run >= stall and ships:
                raise Refused(f"the section's last {run} reconciliations made no progress (no_progress_cap {stall}), "
                              f"so it ships (at_cap): fix the last pass's findings, rerun the gates, file what is "
                              f"left, and stamp it capped")
            if run >= stall:
                raise Refused(f"the section's last {run} reconciliations made no progress (no_progress_cap {stall}); "
                              f"it stays blocked")
        recurred = None if unsettled else review_pin.recurrence_stalled(schedule, chain)
        if recurred and ships:
            raise Refused(f"a finding that holds the stamp has been reported open in {recurred} passes "
                          f"(recurrence_cap {schedule['recurrence_cap']}), so the section ships (at_cap): fix the last "
                          f"pass's findings, redesigning the recurring one, rerun the gates, file what is left, and "
                          f"stamp it capped")
        if recurred:
            raise Refused(f"a finding that holds the stamp has been reported open in {recurred} passes "
                          f"(recurrence_cap {schedule['recurrence_cap']}), so repairing it again is not converging; "
                          f"the section stays blocked for a redesign")
        if not descends(chain[-1]["candidate"], candidate):
            raise Refused(f"the reconciliation's candidate does not descend from the last pass's "
                          f"{chain[-1]['candidate']}")
        if ended_gaps(chain[-1]) and not _tails_have_resolution(
            _writer_tails_after(found, chain[-1]), doc_ref_defs=_collect_ref_defs(raw_text.splitlines())
        ):
            raise Refused("the findings file has no resolution entry after the last returned gaps pass; append "
                          "`- Implemented: <plain-text description outside links> in <commit SHA>`, "
                          "`- Filed: <section ref> (<gap>)`, or `## Writer note: <plain-text reason>` "
                          "(for a contested grade or rule-exempt finding) before dispatching a reconciliation")


def _writer_tails_after(found, last_returned):
    """Per-pass writer-authored line lists `(writer_lines, writer_raw)` belonging to `last_returned` and each trailing
    unreturned reconciliation attempt recorded after it, kept separate so an empty `## Writer note:` in one tail
    cannot borrow a body across a reserved heading from another."""
    def pair(entry):
        nonblank = list(entry.get("writer_lines", []))
        raw = list(entry.get("writer_raw", nonblank))
        return (nonblank, raw)

    tails = [pair(last_returned)]
    recons = found["reconciliations"]
    start = next((idx + 1 for idx, entry in enumerate(recons) if entry is last_returned), 0)
    for entry in recons[start:]:
        tails.append(pair(entry))
    return tails


def _tails_have_resolution(tails, doc_ref_defs=None):
    seen = {}
    return any(_has_resolution_entry(nonblank, seen, raw, ref_defs=doc_ref_defs) for nonblank, raw in tails)


def _norm_ref_label(label):
    return " ".join(label.split()).casefold()


def _parse_link_dest(text, pos):
    """Parse a CommonMark §6.3 link destination at `pos` (`<...>` or balanced-paren non-space/non-control) with
    CommonMark §2.4 ASCII-punctuation-only backslash escapes. Returns end index or None when malformed."""
    n = len(text)
    if pos >= n:
        return None
    if text[pos] == "<":
        j = pos + 1
        while j < n and text[j] not in "<>\n":
            j += 2 if text[j] == "\\" and j + 1 < n and text[j + 1] in ASCII_PUNCT else 1
        return j + 1 if j < n and text[j] == ">" else None
    j, depth = pos, 0
    while j < n and ord(text[j]) > 0x20 and ord(text[j]) != 0x7F:
        if text[j] == "\\" and j + 1 < n and text[j + 1] in ASCII_PUNCT:
            j += 2
            continue
        if text[j] == "(":
            depth += 1
        elif text[j] == ")":
            if depth == 0:
                break
            depth -= 1
        j += 1
    return j if j > pos and depth == 0 else None


def _parse_link_title(text, pos):
    """Parse an optional CommonMark link title delimited by `"..."`, `'...'`, or `(...)` at `pos` with CommonMark
    §2.4 ASCII-punctuation-only backslash escapes. Returns end index or None when malformed."""
    n = len(text)
    if pos >= n or text[pos] not in "\"'(":
        return None
    opener = text[pos]
    closer = ")" if opener == "(" else opener
    j = pos + 1
    while j < n and text[j] != closer:
        if opener == "(" and text[j] == "(":
            return None
        j += 2 if text[j] == "\\" and j + 1 < n and text[j + 1] in ASCII_PUNCT else 1
    return j + 1 if j < n and text[j] == closer else None


def _expand_leading_tabs(line, start_col=0):
    """Expand leading spaces and tabs of `line` using CommonMark §2.2 4-column tab stops starting at `start_col`."""
    col, idx = start_col, 0
    while idx < len(line) and line[idx] in " \t":
        col += 4 - (col % 4) if line[idx] == "\t" else 1
        idx += 1
    return (" " * (col - start_col)) + line[idx:]


def _strip_one_bq(rest, col):
    """Strip one leading CommonMark blockquote marker `>` from `rest` at global column `col`, expanding leading tabs
    before and after `>` per CommonMark §2.2 before consuming the optional single space. Returns `(matched, rest, col)`."""
    expanded = _expand_leading_tabs(rest, col)
    m = BLOCKQUOTE_ONE.match(expanded)
    if not m:
        return False, expanded, col
    gt_col = col + m.end()
    after = _expand_leading_tabs(expanded[m.end():], gt_col)
    if after.startswith(" "):
        return True, after[1:], gt_col + 1
    return True, after, gt_col


def _strip_containers(line, start_col=0):
    """Strip leading CommonMark blockquote (`>`) and list-item (`- `, `* `, `+ `, `1. `) container markers from
    `line` starting at global column `start_col`, expanding leading tabs before and after each marker per
    CommonMark §2.2 and following §4.1 (thematic breaks take precedence over list markers) and §5.2 (5+ spaces
    after a list marker consume only 1 space so the remaining 4+ spaces stay an indented code block). Returns
    `(rest, col, opened_containers)` where each container is `["bq", 0]` or `["list", rel_indent, empty_first]`."""
    rest, col = line, start_col
    opened = []
    while True:
        matched, next_rest, next_col = _strip_one_bq(rest, col)
        if matched:
            opened.append(["bq", 0])
            rest, col = next_rest, next_col
            continue
        expanded = next_rest
        if THEMATIC_BREAK.match(expanded):
            rest = expanded
            break
        lm = LIST_ITEM_ONE.match(expanded)
        if lm:
            marker_end = lm.end()
            marker_col = col + marker_end
            after = _expand_leading_tabs(expanded[marker_end:], marker_col)
            sp_len = len(after) - len(after.lstrip(" "))
            if sp_len > 0 or after == "":
                if after.lstrip(" ") == "":
                    opened.append(["list", marker_end + 1, True])
                    col += marker_end + 1
                    rest = ""
                    break
                if sp_len <= 4:
                    opened.append(["list", marker_end + sp_len, False])
                    col += marker_end + sp_len
                    rest = after[sp_len:]
                    continue
                # CommonMark §5.2: 5+ spaces after list marker means 1 space + indented code (4+ spaces).
                opened.append(["list", marker_end + 1, False])
                col += marker_end + 1
                rest = after[1:]
                break
        rest = expanded
        break
    return rest, col, opened


def _match_containers(line, containers, allow_thematic_exit=True):
    """Match a prefix of `containers` (each `["bq", 0]` or `["list", rel_indent, empty_first]`) against `line`
    in exact nesting order, expanding tabs per CommonMark §2.2 and using CommonMark §2.1 ASCII blank-line rules
    (`" \\t"` only, never U+00A0). A blank line continues through nested list items (stopping only if a blockquote
    marker is missing); thematic-break precedence (`allow_thematic_exit`) applies only outside leaf blocks.
    Returns `(matched_count, rest, col, blank_at)`."""
    rest, col, blank_at = line, 0, None
    for idx, frame in enumerate(containers):
        if frame[0] == "bq":
            matched, next_rest, next_col = _strip_one_bq(rest, col)
            if not matched:
                return idx, rest, col, blank_at
            rest, col = next_rest, next_col
            if not rest.strip(" \t"):
                if blank_at is None:
                    blank_at = idx + 1
                rest = ""
        else:
            if not rest.strip(" \t"):
                if blank_at is None:
                    blank_at = idx
                col += frame[1]
                rest = ""
                continue
            if allow_thematic_exit and THEMATIC_BREAK.match(_expand_leading_tabs(rest, col)):
                return idx, rest, col, None
            still_in, next_rest, next_col = _strip_indent(rest, frame[1], col)
            if not still_in:
                return idx, rest, col, None
            rest, col = next_rest, next_col
    if blank_at is None and not rest.strip(" \t"):
        blank_at = len(containers)
    return len(containers), rest, col, blank_at


def _fence_step(line, fence, start_col=0):
    """Track CommonMark §4.5 fenced code blocks (` ``` ` or `~~~`) using §2.2 4-column tab stops. Returns
    `(new_fence, inside_fence)`."""
    expanded = _expand_leading_tabs(line, start_col)
    lead = len(expanded) - len(expanded.lstrip(" "))
    if lead > 3:
        return (None, False) if fence is None else (fence, True)
    stripped = expanded[lead:]
    if fence is None:
        m = FENCE_OPEN.match(stripped)
        if m and not (m.group(1)[0] == "`" and "`" in m.group(2)):
            return (m.group(1)[0], len(m.group(1))), True
        return None, False
    char, length = fence
    if stripped.startswith(char * length) and stripped.lstrip(char).strip(" \t") == "":
        return None, True
    return fence, True


def _strip_indent(line, indent, start_col=0):
    """Return `(still_inside, content, new_col)` for a line inside a container indented by `indent` columns,
    expanding leading tabs at global column `start_col` per CommonMark §2.2."""
    if indent <= 0:
        return True, line, start_col
    if not line.strip(" \t"):
        return True, "", start_col + indent
    expanded = _expand_leading_tabs(line, start_col)
    lead = len(expanded) - len(expanded.lstrip(" "))
    if lead < indent:
        return False, line, start_col
    return True, expanded[indent:], start_col + indent


def _read_ref_continuation(line, containers):
    """Read a continuation line for a CommonMark §4.7 link reference definition inside `containers`.
    Returns the expanded continuation text, or None if the line exits an enclosing blockquote, is blank, or starts
    a block-level interrupt (blockquote, fence, HTML block, ATX heading, Setext underline, thematic break, or list)."""
    matched, rest, col, blank_at = _match_containers(line, containers, allow_thematic_exit=True)
    if blank_at is not None:
        return None
    exp = _expand_leading_tabs(rest, col)
    if not exp.strip(" \t") or BLOCKQUOTE_ONE.match(exp) or _fence_step(exp, None, col)[1] \
            or _html_block_start(exp, True)[0] or ATX_HEADING.match(exp) or THEMATIC_BREAK.match(exp) \
            or SETEXT_UNDERLINE.match(exp) or LIST_PARA_INTERRUPT.match(exp):
        return None
    return exp


def _scan_multiline_title(raw_lines, start_line, start_text, start_pos, containers):
    """Scan a CommonMark link title starting at `(start_line, start_pos)` across consecutive continuation lines
    inside `containers`. Returns closing line index when valid, else None."""
    opener = start_text[start_pos]
    closer = ")" if opener == "(" else opener
    line_idx = start_line
    text = start_text
    pos = start_pos + 1
    n = len(raw_lines)
    while True:
        while pos < len(text) and text[pos] != closer:
            if opener == "(" and text[pos] == "(":
                return None
            pos += 2 if text[pos] == "\\" and pos + 1 < len(text) and text[pos + 1] in ASCII_PUNCT else 1
        if pos < len(text) and text[pos] == closer:
            return line_idx if text[pos + 1 :].strip(" \t") == "" else None
        line_idx += 1
        if line_idx >= n:
            return None
        next_exp = _read_ref_continuation(raw_lines[line_idx], containers)
        if next_exp is None:
            return None
        text, pos = next_exp, 0


def _html_block_start(content, in_para):
    """Detect CommonMark §4.6 HTML block types 1 through 7. Returns `(started, active_end)` where `active_end` is
    None if the block closed on the same line, `"blank"` for types 6-7 (ended by a blank line), or a callable
    `closed(line) -> bool` for types 1-5."""
    if HTML_TYPE1_OPEN.match(content):
        return True, (None if HTML_TYPE1_CLOSE.search(content) else lambda ln: bool(HTML_TYPE1_CLOSE.search(ln)))
    if HTML_TYPE2_OPEN.match(content):
        return True, (None if "-->" in content else lambda ln: "-->" in ln)
    if HTML_TYPE3_OPEN.match(content):
        return True, (None if "?>" in content[content.find("<?") + 2 :] else lambda ln: "?>" in ln)
    if HTML_TYPE4_OPEN.match(content):
        return True, (None if ">" in content[content.find("<!") + 2 :] else lambda ln: ">" in ln)
    if HTML_TYPE5_OPEN.match(content):
        return True, (None if "]]>" in content[content.find("<![CDATA[") + 9 :] else lambda ln: "]]>" in ln)
    if HTML_TYPE6_OPEN.match(content):
        return True, "blank"
    if not in_para and HTML_TYPE7_OPEN.match(content):
        return True, "blank"
    return False, None


def _scan_ref_label(raw_lines, start_line, content, containers):
    """Scan a CommonMark §4.7 link reference label `[label]:` starting at `(start_line, content)` across one or more
    continuation lines inside `containers` (up to 999 label characters, allowing `\\`-escaped brackets and rejecting
    unescaped `[` or blank/block interrupts). Returns `(label, end_line, rest)` or None."""
    m = REF_LABEL_START.match(content)
    if not m:
        return None
    line_idx = start_line
    text = content
    pos = m.end()
    parts = []
    total = 0
    n = len(raw_lines)
    while True:
        seg_start = pos
        while pos < len(text):
            ch = text[pos]
            if ch == "\\" and pos + 1 < len(text):
                pos += 2
                continue
            if ch == "[":
                return None
            if ch == "]":
                parts.append(text[seg_start:pos])
                label = "".join(parts)
                if len(label) > 999 or not label.strip(" \t\n") or text[pos + 1 : pos + 2] != ":":
                    return None
                return label, line_idx, text[pos + 2 :].lstrip(" \t")
            pos += 1
        chunk = text[seg_start:]
        total += len(chunk) + 1
        if total > 999:
            return None
        parts.append(chunk)
        parts.append("\n")
        line_idx += 1
        if line_idx >= n:
            return None
        next_exp = _read_ref_continuation(raw_lines[line_idx], containers)
        if next_exp is None:
            return None
        text, pos = next_exp, 0


def _collect_ref_defs(raw_lines):
    """Collect valid CommonMark link reference definitions from `raw_lines` in linear time, skipping fenced code
    blocks, all seven CommonMark §4.6 HTML block types, and open paragraphs per CommonMark §4.7 (tracking the exact
    container nesting stack of blockquotes and list items with §2.1 ASCII blank lines and §2.2 tab stops), and
    supporting next-line destinations and multiline link titles."""
    defs = set()
    containers = []
    in_para = False
    fence, fence_containers = None, []
    html_end, html_containers = None, []
    i, n = 0, len(raw_lines)
    while i < n:
        line = raw_lines[i]
        if fence is not None:
            matched, inner, inner_col, _ = _match_containers(line, fence_containers, allow_thematic_exit=False)
            if matched < len(fence_containers):
                fence = None
            else:
                fence, inside = _fence_step(inner, fence, inner_col)
                if inside:
                    for frame in containers[:matched]:
                        if frame[0] == "list":
                            frame[2] = False
                    in_para = False
                    i += 1
                    continue
        if html_end is not None:
            matched, inner, _, blank_at = _match_containers(line, html_containers, allow_thematic_exit=False)
            if matched < len(html_containers):
                html_end = None
            elif html_end == "blank":
                if blank_at is not None or not inner.strip(" \t"):
                    html_end = None
                    in_para = False
                    i += 1
                    continue
                for frame in containers[:matched]:
                    if frame[0] == "list":
                        frame[2] = False
                in_para = False
                i += 1
                continue
            else:
                for frame in containers[:matched]:
                    if frame[0] == "list":
                        frame[2] = False
                in_para = False
                if html_end(inner):
                    html_end = None
                i += 1
                continue
        matched, base_text, base_col, blank_at = _match_containers(line, containers)
        if blank_at is not None:
            # Close any container after the matched prefix, and any empty list item at/after the blank line.
            containers = containers[:matched]
            while containers and containers[-1][0] == "list" and containers[-1][2]:
                containers.pop()
            in_para = False
            i += 1
            continue
        containers = containers[:matched]
        for frame in containers:
            if frame[0] == "list":
                frame[2] = False
        content, content_col, opened = _strip_containers(base_text, base_col)
        if opened:
            containers.extend(opened)
            in_para = False
        if not content.strip(" \t"):
            in_para = False
            i += 1
            continue
        new_fence, inside = _fence_step(content, None, content_col)
        if inside:
            fence, fence_containers = new_fence, [list(f) for f in containers]
            in_para = False
            i += 1
            continue
        html_started, next_html_end = _html_block_start(content, in_para)
        if html_started:
            html_end, html_containers = next_html_end, [list(f) for f in containers]
            in_para = False
            i += 1
            continue
        if ATX_HEADING.match(content) or THEMATIC_BREAK.match(content) \
                or (in_para and SETEXT_UNDERLINE.match(content)):
            in_para = False
            i += 1
            continue
        if len(content) - len(content.lstrip(" ")) >= 4:
            i += 1
            continue
        if in_para:
            i += 1
            continue
        scanned = _scan_ref_label(raw_lines, i, content, containers)
        if scanned is None:
            in_para = True
            i += 1
            continue
        label, label_end, rest = scanned
        start = label_end
        if not rest:
            if label_end + 1 >= n:
                in_para = True
                i += 1
                continue
            next_exp = _read_ref_continuation(raw_lines[label_end + 1], containers)
            if next_exp is None:
                in_para = True
                i += 1
                continue
            start = label_end + 1
            rest = next_exp.lstrip(" \t")
        dest_end = _parse_link_dest(rest, 0)
        if dest_end is None:
            in_para = True
            i += 1
            continue
        if rest[dest_end:].strip(" \t") == "":
            defs.add(_norm_ref_label(label))
            in_para = False
            i = start + 1
            continue
        j = dest_end
        while j < len(rest) and rest[j] in " \t":
            j += 1
        if j == dest_end or rest[j] not in "\"'(":
            in_para = True
            i += 1
            continue
        end_line = _scan_multiline_title(raw_lines, start, rest, j, containers)
        if end_line is not None:
            defs.add(_norm_ref_label(label))
            in_para = False
            i = end_line + 1
            continue
        in_para = True
        i += 1
    return defs


def _find_close_bracket(text, start, allow_code_spans=True):
    """Find the matching unescaped `]` starting at `start` (after `[`), skipping `\\`-escaped characters and
    (when `allow_code_spans`) matched backtick code spans; return -1 if an unescaped `[` or end-of-string is hit."""
    i, n = start, len(text)
    while i < n:
        ch = text[i]
        if ch == "\\" and i + 1 < n:
            i += 2
            continue
        if allow_code_spans and ch == "`":
            j = i
            while j < n and text[j] == "`":
                j += 1
            close = _find_closing_ticks(text, j, j - i)
            if close != -1:
                i = close + (j - i)
                continue
            i = j
            continue
        if ch == "[":
            return -1
        if ch == "]":
            return i
        i += 1
    return -1


def _unwrap_markdown_segments(text, ref_defs=()):
    """Single-pass CommonMark §6.3/§6.4 bracket-stack unwrapper returning `[(chunk, is_link_label), ...]` so
    matched link labels resolve their own inline HTML and emphasis scope without letting unmatched `*`/`_`/`~`
    cross link boundaries, while outer emphasis around links (`**[sha](diff)**`) still pairs cleanly."""
    out = []  # each (chunk_str, is_link_label)
    openers = []  # each [is_image, out_index, active, text_index]
    active_links = []  # subset of openers where not is_image and active (O(N) deactivation)
    i, n = 0, len(text)
    while i < n:
        ch = text[i]
        if ch == "\\" and i + 1 < n and text[i + 1] in ASCII_PUNCT:
            out.append((text[i : i + 2], False))
            i += 2
            continue
        if ch == "`":
            j = i
            while j < n and text[j] == "`":
                j += 1
            close = _find_closing_ticks(text, j, j - i)
            if close != -1:
                end = close + (j - i)
                out.append((text[i:end], False))
                i = end
                continue
            out.append((text[i:j], False))
            i = j
            continue
        if ch == "<":
            hm = HTML_INLINE_TAG.match(text, i)
            if hm:
                out.append((text[i : hm.end()], False))
                i = hm.end()
                continue
        if ch == "!" and i + 1 < n and text[i + 1] == "[":
            openers.append([True, len(out), True, i + 1])
            out.append(("![", False))
            i += 2
            continue
        if ch == "[":
            op = [False, len(out), True, i]
            openers.append(op)
            active_links.append(op)
            out.append(("[", False))
            i += 1
            continue
        if ch == "]" and openers:
            op = openers.pop()
            is_image, start_idx, active, br_pos = op
            if not is_image and active:
                active_links.pop()
            if not active:
                out.append(("]", False))
                i += 1
                continue
            nxt = i + 1
            end_pos = None
            cached_label = None

            def get_label():
                nonlocal cached_label
                if cached_label is None:
                    cached_label = "".join(piece for piece, _ in out[start_idx + 1 :])
                return cached_label

            if nxt < n and text[nxt] == "(":
                j = nxt + 1
                while j < n and text[j].isspace():
                    j += 1
                if j < n and text[j] == ")":
                    end_pos = j + 1
                else:
                    dest_end = _parse_link_dest(text, j)
                    if dest_end is not None:
                        j = dest_end
                        ws = j
                        while j < n and text[j].isspace():
                            j += 1
                        title_ok = True
                        if j < n and text[j] != ")":
                            title_end = None if j == ws else _parse_link_title(text, j)
                            if title_end is None:
                                title_ok = False
                            else:
                                j = title_end
                                while j < n and text[j].isspace():
                                    j += 1
                        if title_ok and j < n and text[j] == ")":
                            end_pos = j + 1
            elif ref_defs and nxt < n and text[nxt] == "[":
                close_ref = _find_close_bracket(text, nxt + 1, allow_code_spans=False)
                if close_ref != -1:
                    ref_id = text[nxt + 1 : close_ref]
                    if ref_id:
                        lookup = _norm_ref_label(ref_id)
                        if lookup in ref_defs:
                            end_pos = close_ref + 1
                    elif i - br_pos <= 1000:
                        lookup = _norm_ref_label(get_label())
                        if lookup and lookup in ref_defs:
                            end_pos = close_ref + 1
                elif i - br_pos <= 1000:
                    lbl = get_label()
                    if lbl.strip(" \t") and _norm_ref_label(lbl) in ref_defs:
                        end_pos = nxt
            elif ref_defs and i - br_pos <= 1000:
                lbl = get_label()
                if lbl.strip(" \t") and _norm_ref_label(lbl) in ref_defs:
                    end_pos = nxt
            if end_pos is not None:
                if is_image:
                    out[start_idx:] = [(" ", False)]
                else:
                    out[start_idx:] = [(get_label(), True)]
                    for prev_op in active_links:
                        prev_op[2] = False
                    active_links.clear()
                i = end_pos
                continue
        out.append((ch, False))
        i += 1
    merged, buf = [], []
    for chunk, is_lbl in out:
        if is_lbl:
            if buf:
                merged.append(("".join(buf), False))
                buf = []
            merged.append((chunk, True))
        else:
            buf.append(chunk)
    if buf:
        merged.append(("".join(buf), False))
    return merged


def _unwrap_markdown_links(text, ref_defs=()):
    """Replace CommonMark inline/reference links with visible label text and images with a space."""
    return "".join(chunk for chunk, _ in _unwrap_markdown_segments(text, ref_defs))


def _is_outer_unicode_punct(ch):
    return ord(ch) > 127 and unicodedata.category(ch).startswith("P")


def _strip_outer_url_punct(raw):
    i, j = 0, len(raw)
    while i < j and (raw[i] in ".,:;()<>\"'!?" or _is_outer_unicode_punct(raw[i])):
        i += 1
    while j > i and (raw[j - 1] in ".,:;()<>\"'!?" or _is_outer_unicode_punct(raw[j - 1])):
        j -= 1
    return raw[i:j]


def _has_domain_host(rest):
    """True when `rest` starts with a valid dotted domain host (ASCII or IDN) before any `/`, `?`, `#`, or `:`."""
    end = len(rest)
    for sep in ("/", "?", "#", ":"):
        pos = rest.find(sep)
        if pos != -1 and pos < end:
            end = pos
    host = rest[:end]
    labels = host.split(".")
    if len(labels) < 2 or any(not lbl for lbl in labels):
        return False
    for idx, lbl in enumerate(labels):
        allow_underscore = idx < len(labels) - 2
        has_alnum = False
        for ch in lbl:
            if ch == "-":
                continue
            if ch == "_":
                if not allow_underscore:
                    return False
                continue
            if ch.isalnum():
                has_alnum = True
            elif unicodedata.category(ch) in ("Mn", "Mc"):
                continue
            else:
                return False
        if not has_alnum:
            return False
    return True


def _is_www_url(trimmed):
    """True when `trimmed` is a GFM §6.9 / IDN extended `www.` autolink (`www.<domain>[:/?#...]`)."""
    return len(trimmed) >= 5 and trimmed[:4].lower() == "www." and _has_domain_host(trimmed[4:])


def _is_proto_rel_url(trimmed):
    """True when `trimmed` is a protocol-relative URL (`//<host>[:/?#...]`), including single-label hosts such as
    `//localhost/commit/...` when followed by `/`, `?`, `#`, or `:`."""
    if len(trimmed) < 5 or not trimmed.startswith("//"):
        return False
    rest = trimmed[2:]
    if _has_domain_host(rest):
        return True
    end = len(rest)
    for sep in ("/", "?", "#", ":"):
        pos = rest.find(sep)
        if pos != -1 and pos < end:
            end = pos
    if end == 0 or end == len(rest):
        return False
    host = rest[:end]
    return all(ch == "-" or ch.isalnum() or unicodedata.category(ch) in ("Mn", "Mc") for ch in host) and any(
        ch.isalnum() for ch in host
    )


def _strip_outer_angle_punct(raw):
    i, j = 0, len(raw)
    while i < j and (raw[i] in ".,:;()\"'!?" or _is_outer_unicode_punct(raw[i])):
        i += 1
    while j > i and (raw[j - 1] in ".,:;()\"'!?" or _is_outer_unicode_punct(raw[j - 1])):
        j -= 1
    return raw[i:j]


def _is_email_token(trimmed):
    """True when `trimmed` is a CommonMark §6.5 / GFM §6.9 email autolink (`user@domain.tld`)."""
    if "@" not in trimmed or "/" in trimmed:
        return False
    local, _, domain = trimmed.partition("@")
    if not local or not domain or "@" in domain:
        return False
    if not all(ch.isalnum() or ch in ".!#$%&'*+/=?^_`{|}~-" for ch in local):
        return False
    return _has_domain_host(domain)


def _is_url_token(raw):
    """True when `raw` is a scheme URL (`://`), protocol-relative URL (`//<host>...`), GFM `www.<host>...` link,
    CommonMark §6.5 angle-bracket URI autolink (`<scheme:...>`), or email/mailto autolink."""
    if "://" in raw:
        return True
    angle = _strip_outer_angle_punct(raw)
    if angle and CM_URI_AUTOLINK.fullmatch(angle):
        return True
    trimmed = _strip_outer_url_punct(raw)
    if not trimmed:
        return False
    if trimmed.lower().startswith("mailto:") and "@" in trimmed:
        return True
    return _is_www_url(trimmed) or _is_proto_rel_url(trimmed) or _is_email_token(trimmed)


def _strip_paired_md(raw):
    """Strip outer ASCII and Unicode sentence punctuation (never leading `.` or `:`, ASCII brackets, or git revspec
    operators `~`/`^`/`^!`) and matched inline-code backticks (` `` `, `` ` ``). Span-level emphasis (`*`, `_`, `~~`)
    is already resolved by `_strip_inline_emphasis_segments` respecting link boundaries."""
    i, j = 0, len(raw)
    while i < j and (raw[i] in ",;()<>\"'" or _is_outer_unicode_punct(raw[i])):
        i += 1
    while j > i and (raw[j - 1] in ".,:;()<>\"'?" or _is_outer_unicode_punct(raw[j - 1])
                     or (raw[j - 1] == "!" and not raw[:j].endswith("^!"))):
        j -= 1
    tok = raw[i:j]
    changed = True
    while changed and tok:
        changed = False
        for mark in ("``", "`"):
            if len(tok) > 2 * len(mark) and tok.startswith(mark) and tok.endswith(mark):
                tok = tok[len(mark) : -len(mark)]
                changed = True
                break
    return tok


def _is_commit_in_git(tok, seen):
    key = ("commit", tok.lower())
    if key not in seen:
        seen[key] = git("cat-file", "-e", f"{tok}^{{commit}}").returncode == 0
    return seen[key]


def _is_revspec_in_git(expr, seen):
    if not expr or expr.startswith("-"):
        return False
    key = ("rev", expr)
    if key not in seen:
        if expr.startswith(":/"):
            done = git("rev-parse", "--verify", "--quiet", expr)
            sha = done.stdout.decode("ascii", "replace").strip() if done.returncode == 0 else ""
            seen[key] = bool(sha) and _is_commit_in_git(sha, seen)
        else:
            seen[key] = git("rev-parse", "--verify", "--quiet", f"{expr}^{{commit}}").returncode == 0
    return seen[key]


def _repo_refs(seen):
    if "__refs__" not in seen:
        done = git("for-each-ref", "--format=%(refname:short)%0a%(refname)")
        text = done.stdout.decode("utf-8", "replace") if done.returncode == 0 else ""
        names = {line.strip() for line in text.splitlines() if line.strip()}
        names.add("HEAD")
        seen["__refs__"] = names
    return seen["__refs__"]


def _is_commit_ref_token(raw, seen):
    """True when `raw` is purely a URL (`://` or `www.`) or git commit reference / branch / tag / reflog selector /
    range / repo-qualified ref / list (including pairs joined by arrows, punctuation, slashes, colons, or hyphens
    with zero prose words). Iterative (no recursion on nested qualifiers)."""
    if _is_url_token(raw):
        return True
    stripped = _strip_paired_md(raw)
    if not stripped:
        return False
    rm = REPO_QUALIFIED_REF.fullmatch(stripped)
    if rm:
        stripped = rm.group(1)
        if not stripped:
            return False
    refs = _repo_refs(seen)
    if stripped in refs:
        return True
    if stripped.startswith(":/"):
        return _is_revspec_in_git(stripped, seen)

    def piece_is_ref(piece):
        pm = REPO_QUALIFIED_REF.fullmatch(piece)
        if pm:
            piece = pm.group(1)
        base = REVSPEC_SUFFIX.sub("", piece.lstrip("^"))
        if base and base in refs:
            return True
        if base and COMMIT_TOKEN.fullmatch(base) and (len(base) >= 7 or _is_commit_in_git(base, seen)):
            return True
        if base and base.count("{") == base.count("}") and _is_revspec_in_git(base, seen):
            return True
        if piece.count("{") == piece.count("}") and _is_revspec_in_git(piece.lstrip("^"), seen):
            return True
        return False

    if piece_is_ref(stripped):
        return True
    pieces = [p for p in re.split(r"(?:->|=>|\.{2,3}|[,;]|[^\w\s~^@{}:/.-]+)", stripped) if p]
    if not pieces:
        return False
    for piece in pieces:
        if piece_is_ref(piece):
            continue
        if any(ch in piece for ch in "/:-") and "{" not in piece:
            sub = [s for s in re.split(r"[/:-]", piece) if s]
            if sub and all(piece_is_ref(s) for s in sub):
                continue
        return False
    return True


def _split_plain_revspecs(text):
    """Split a backtick-free segment on whitespace and outer Unicode punctuation while keeping closed Git brace
    selectors (`@{...}`, `^{...}`), `:/...` commit-message search tokens, and URLs (`://` or `www.`) intact."""
    tokens, buf = [], []
    in_url = False
    lead_delims = 0
    i, n = 0, len(text)
    while i < n:
        ch = text[i]
        if ch in "@^" and i + 1 < n and text[i + 1] == "{":
            close = text.find("}", i + 2)
            if close != -1:
                buf.append(text[i : close + 1])
                i = close + 1
                continue
        is_search = len(buf) >= 2 and buf[0] == ":" and buf[1] == "/"
        if ch.isspace() or (_is_outer_unicode_punct(ch) and not is_search and not in_url):
            if buf:
                tokens.append("".join(buf))
                buf = []
            in_url = False
            lead_delims = 0
        else:
            if len(buf) == lead_delims and ch in "(\"'<":
                lead_delims += 1
            buf.append(ch)
            if not in_url:
                if (ch == "/" and len(buf) >= 4 and buf[-3:] == [":", "/", "/"]
                        and buf[-4].isascii() and (buf[-4].isalnum() or buf[-4] in "+-.")):
                    in_url = True
                elif ch == "/" and len(buf) == lead_delims + 2 and buf[-2:] == ["/", "/"]:
                    in_url = True
                elif ch == "." and len(buf) == lead_delims + 4 and "".join(buf[-4:-1]).lower() == "www":
                    in_url = True
        i += 1
    if buf:
        tokens.append("".join(buf))
    return tokens


def _find_closing_ticks(text, start, count):
    """Find the next exact run of `count` backticks at or after `start` (CommonMark §6.1), or -1."""
    i, n = start, len(text)
    while i < n:
        if text[i] == "`":
            j = i
            while j < n and text[j] == "`":
                j += 1
            if j - i == count:
                return i
            i = j
        else:
            i += 1
    return -1


def _decode_cm_entity(match):
    """Decode one CommonMark §2.5 entity or numeric character reference without discarding control codes."""
    tok = match.group(0)
    if tok.startswith("&#"):
        code = int(tok[3:-1], 16) if tok[2] in "xX" else int(tok[2:-1], 10)
        if code == 0 or code > 0x10FFFF or 0xD800 <= code <= 0xDFFF:
            return "\ufffd"
        return chr(code)
    return html.unescape(tok)


def _split_revspec_tokens(text):
    """Split `text` on whitespace while isolating matched CommonMark inline code spans (`` `...` ``) so Git brace
    selectors never cross code-span boundaries, inner commands like `` `git show <sha>` `` expose bare SHA tokens,
    inline-code selectors like `` `HEAD@{1 minute ago}` `` stay intact, and non-whitespace characters directly
    adjacent to a code span (such as ``[`<sha>`][ref]`` or `` `<sha>`~ ``) stay attached to the adjacent token."""
    tokens = []
    glued = False

    def push_segment(seg, is_code):
        nonlocal glued
        if not seg:
            if not is_code:
                glued = False
            return
        if is_code and len(seg) >= 2 and seg[0] == " " and seg[-1] == " " and seg.strip(" "):
            seg = seg[1:-1]
        elif not is_code and "&" in seg:
            seg = CM_ENTITY.sub(_decode_cm_entity, seg)
        parts = _split_plain_revspecs(seg)
        if not parts:
            glued = False
            return
        lead_ws = seg[0].isspace() or _is_outer_unicode_punct(seg[0])
        trail_ws = seg[-1].isspace() or _is_outer_unicode_punct(seg[-1])
        if glued and not lead_ws and tokens:
            tokens[-1] += parts[0]
            tokens.extend(parts[1:])
        else:
            tokens.extend(parts)
        glued = not trail_ws

    plain_start, i, n = 0, 0, len(text)
    while i < n:
        if text[i] == "`":
            j = i
            while j < n and text[j] == "`":
                j += 1
            close = _find_closing_ticks(text, j, j - i)
            if close != -1:
                if i > plain_start:
                    push_segment(text[plain_start:i], False)
                push_segment(text[j:close], True)
                i = close + (j - i)
                plain_start = i
                continue
            i = j
            continue
        i += 1
    if plain_start < n:
        push_segment(text[plain_start:], False)
    return tokens


def _all_commit_refs(tokens, seen):
    """Iterative bottom-up check: True when every token in `tokens` is covered by git commit references, URLs,
    ranges, reflog selectors, and/or multiword `:/...` commit-message search selectors (zero prose, no recursion)."""
    n = len(tokens)
    dp = [False] * (n + 1)
    dp[n] = True
    for k in range(n - 1, -1, -1):
        tok = tokens[k]
        if not any(ch.isalnum() for ch in tok):
            dp[k] = dp[k + 1]
            continue
        stripped = _strip_paired_md(tok)
        if stripped.startswith(":/"):
            limit = next((m for m in range(k + 1, n) if _strip_paired_md(tokens[m]).startswith(":/")), n)
            for end in range(limit, k, -1):
                if dp[end] and _is_revspec_in_git(_strip_paired_md(" ".join(tokens[k:end])), seen):
                    dp[k] = True
                    break
        elif dp[k + 1] and _is_commit_ref_token(tok, seen):
            dp[k] = True
    return dp[0]


def _expand_commit_sep_tokens(tokens, seen):
    """Expand comma/semicolon-separated commit lists (such as ``[`sha1`](/a),[`sha2`](/b)``) and slash-separated
    pure-commit lists (such as `` `sha1`/`sha2` ``) while leaving URLs, file paths (`src/<sha>`), brace selectors,
    and `:/` search selectors untouched."""
    out = []
    for raw in tokens:
        if _is_url_token(raw) or "{" in raw or "}" in raw or raw.startswith(":/"):
            out.append(raw)
            continue
        if "," in raw or ";" in raw:
            sub = [p for p in re.split(r"[,;]", raw) if p]
            if len(sub) > 1 and any(
                "/" not in p and COMMIT_TOKEN.fullmatch(_strip_paired_md(p))
                and _is_commit_in_git(_strip_paired_md(p), seen)
                for p in sub
            ):
                out.extend(sub)
                continue
        if "/" in raw:
            sub = raw.split("/")
            if len(sub) > 1 and all(sub) and all(
                COMMIT_TOKEN.fullmatch(_strip_paired_md(p)) and _is_commit_in_git(_strip_paired_md(p), seen)
                for p in sub
            ):
                out.extend(sub)
                continue
        out.append(raw)
    return out


def _strip_inline_html(text):
    """Strip CommonMark §6.6 inline HTML tags and comments in leftmost-first order alongside CommonMark §6.1 backtick
    code spans (so backticks inside HTML comments stay hidden while tags inside code spans stay literal). Transparent
    inline phrasing tags (`<a>`, `<code>`, `<em>`, etc.) and HTML comments (`<!-- ... -->`) become empty string;
    replaced and block HTML elements (`<img>`, `<br>`, etc.) become a space."""
    out = []
    i, n = 0, len(text)
    while i < n:
        ch = text[i]
        if ch == "`":
            j = i
            while j < n and text[j] == "`":
                j += 1
            close = _find_closing_ticks(text, j, j - i)
            if close != -1:
                end = close + (j - i)
                out.append(text[i:end])
                i = end
                continue
            out.append(text[i:j])
            i = j
            continue
        if ch == "<":
            m = HTML_INLINE_TAG.match(text, i)
            if m:
                out.append("" if m.group("transparent") else " ")
                i = m.end()
                continue
        out.append(ch)
        i += 1
    return "".join(out)


def _strip_inline_emphasis(text):
    """Strip matched span-level CommonMark/GFM emphasis wrappers (`***`, `**`, `*`, `___`, `__`, `_`, `~~`) outside
    matched backtick code spans and backslash escapes, respecting flanking rules (and word-boundary rules for `_`)
    so multi-word emphasized links like `[**fixed in <sha>**](diff)` expose bare SHA tokens while unpaired `**`/`~`
    and intraword underscores (`fetch_head`) stay untouched."""
    out = []
    stack = []  # each [char, count, out_index]
    i, n = 0, len(text)
    while i < n:
        ch = text[i]
        if ch == "\\" and i + 1 < n and text[i + 1] in ASCII_PUNCT:
            out.append(text[i : i + 2])
            i += 2
            continue
        if ch == "`":
            j = i
            while j < n and text[j] == "`":
                j += 1
            close = _find_closing_ticks(text, j, j - i)
            if close != -1:
                end = close + (j - i)
                out.append(text[i:end])
                i = end
                continue
            out.append(text[i:j])
            i = j
            continue
        if ch in "*_~":
            j = i
            while j < n and text[j] == ch:
                j += 1
            run = j - i
            if ch == "~" and run != 2:
                out.append(text[i:j])
                i = j
                continue
            before = text[i - 1] if i > 0 else " "
            after = text[j] if j < n else " "
            can_open = not after.isspace() and (ch != "_" or not before.isalnum())
            can_close = not before.isspace() and (ch != "_" or not after.isalnum())
            rem = run
            if can_close and stack:
                # Match innermost compatible opener runs of the same delimiter character.
                while rem > 0:
                    match_idx = None
                    for k in range(len(stack) - 1, -1, -1):
                        if stack[k][0] == ch and (ch != "~" or stack[k][1] == rem):
                            match_idx = k
                            break
                    if match_idx is None:
                        break
                    op_ch, op_cnt, op_out = stack[match_idx]
                    take = min(rem, op_cnt)
                    out[op_out] = op_ch * (op_cnt - take)
                    if op_cnt == take:
                        del stack[match_idx:]
                    else:
                        stack[match_idx][1] -= take
                        del stack[match_idx + 1 :]
                    rem -= take
            if rem > 0:
                if can_open and (ch != "~" or rem == 2):
                    stack.append([ch, rem, len(out)])
                out.append(ch * rem)
            i = j
            continue
        out.append(ch)
        i += 1
    return "".join(out)


def _should_separate_link_boundary(pieces, left_ch, right_ch, seen):
    """True when a space should separate an adjacent outer segment and link-label segment so standalone linked SHAs
    next to prose or sentence punctuation stay tokenizable while paths, revspecs, ranges, `<rev>:<path>` expressions,
    and malformed emphasis stay glued."""
    if left_ch.isspace() or right_ch.isspace():
        return False
    if left_ch in "/@~^*-_" or right_ch in "/@.~^*-_:":
        return False
    if left_ch == ".":
        # Separate after a single sentence-ending period (e.g. `Corrected parser.[sha](diff)`), never after `..`
        # ranges or leading dotfiles (`.sha`).
        tail = (pieces[-2] if len(pieces) >= 2 else "") + pieces[-1]
        if len(tail) < 2:
            return False
        before_dot = tail[-2]
        return before_dot != "." and (
            before_dot.isalnum() or unicodedata.category(before_dot).startswith("M") or before_dot in ")]\"'"
        )
    if left_ch == ":":
        # Separate after a prose colon (`fixed:[sha](diff)`), never after a Git `<rev>:` or index `:0:` prefix.
        # Walk backwards only to the preceding whitespace so repeated `:` links stay O(N).
        chars_rev = []
        for piece in reversed(pieces):
            stop = False
            for ch in reversed(piece):
                if ch.isspace():
                    stop = True
                    break
                chars_rev.append(ch)
            if stop:
                break
        prefix = "".join(reversed(chars_rev))[:-1]
        if not prefix or prefix.startswith(":") or prefix.isdigit() or prefix.endswith(":"):
            return False
        if seen is not None:
            if prefix in _repo_refs(seen):
                return False
            if COMMIT_TOKEN.fullmatch(prefix) and _is_commit_in_git(prefix, seen):
                return False
            if _is_revspec_in_git(prefix, seen):
                return False
        return True
    return True


def _strip_inline_emphasis_segments(segments, seen=None):
    """Strip matched span-level CommonMark/GFM emphasis wrappers (`***`, `**`, `*`, `___`, `__`, `_`, `~~`) across
    outer `(chunk, False)` segments while treating link-label `(chunk, True)` segments as their own already-resolved
    scope so unmatched `*`/`_`/`~` never pair across link boundaries. Returns `(visible_all, visible_outer)` where
    `visible_outer` omits link-label text so repair descriptions must include plain text outside Markdown links."""
    resolved = [(chunk if not is_lbl else _strip_inline_emphasis(_strip_inline_html(chunk)), is_lbl)
                for chunk, is_lbl in segments]
    # Flatten with per-character segment ownership (sid > 0 for link labels, 0 for outer text) so outer flanking
    # checks see exact neighbor characters while link-label boundaries remain observable when reassembling.
    chars = []
    seg_ids = []
    next_lbl = 1
    for chunk, is_lbl in resolved:
        text = chunk if is_lbl else _strip_inline_html(chunk)
        sid = next_lbl if is_lbl else 0
        if is_lbl:
            next_lbl += 1
        for ch in text:
            chars.append(ch)
            seg_ids.append(sid)
    flat = "".join(chars)
    out = []  # each [text, sid]
    stack = []  # each [char, count, out_index]
    i, n = 0, len(flat)
    while i < n:
        if seg_ids[i]:
            out.append([flat[i], seg_ids[i]])
            i += 1
            continue
        ch = flat[i]
        if ch == "\\" and i + 1 < n and not seg_ids[i + 1] and flat[i + 1] in ASCII_PUNCT:
            out.append([flat[i : i + 2], 0])
            i += 2
            continue
        if ch == "`":
            j = i
            while j < n and not seg_ids[j] and flat[j] == "`":
                j += 1
            close = _find_closing_ticks(flat, j, j - i)
            if close != -1 and not any(seg_ids[j : close + (j - i)]):
                end = close + (j - i)
                out.append([flat[i:end], 0])
                i = end
                continue
            out.append([flat[i:j], 0])
            i = j
            continue
        if ch in "*_~":
            j = i
            while j < n and not seg_ids[j] and flat[j] == ch:
                j += 1
            run = j - i
            if ch == "~" and run != 2:
                out.append([flat[i:j], 0])
                i = j
                continue
            before = flat[i - 1] if i > 0 else " "
            after = flat[j] if j < n else " "
            can_open = not after.isspace() and (ch != "_" or not before.isalnum())
            can_close = not before.isspace() and (ch != "_" or not after.isalnum())
            rem = run
            if can_close and stack:
                while rem > 0:
                    match_idx = None
                    for k in range(len(stack) - 1, -1, -1):
                        if stack[k][0] == ch and (ch != "~" or stack[k][1] == rem):
                            match_idx = k
                            break
                    if match_idx is None:
                        break
                    op_ch, op_cnt, op_out = stack[match_idx]
                    take = min(rem, op_cnt)
                    out[op_out][0] = op_ch * (op_cnt - take)
                    if op_cnt == take:
                        del stack[match_idx:]
                    else:
                        stack[match_idx][1] -= take
                        del stack[match_idx + 1 :]
                    rem -= take
            if rem > 0:
                if can_open and (ch != "~" or rem == 2):
                    stack.append([ch, rem, len(out)])
                out.append([ch * rem, 0])
            i = j
            continue
        out.append([ch, 0])
        i += 1
    pieces, outer_pieces, prev_text, prev_sid, saw_link = [], [], "", None, False
    for text, sid in out:
        if not text:
            continue
        if prev_sid is not None and sid != prev_sid and (prev_sid or sid):
            if _should_separate_link_boundary(pieces, prev_text[-1], text[0], seen):
                pieces.append(" ")
        pieces.append(text)
        if sid == 0:
            if saw_link and outer_pieces and not outer_pieces[-1][-1].isspace() and not text[0].isspace():
                outer_pieces.append(" ")
            outer_pieces.append(text)
            saw_link = False
        else:
            saw_link = True
        prev_text, prev_sid = text, sid
    return "".join(pieces), "".join(outer_pieces)


def _strip_comments_line(line, in_comment=False, allow_multiline=True):
    """Strip HTML comments (`<!-- ... -->`) from `line`, preserving backtick code spans and visible HTML elements
    (such as `<img>`). When `allow_multiline` is True, an unclosed `<!--` carries `in_comment=True` across lines;
    when False (single-line ATX heading titles), an unclosed `<!--` stays literal per CommonMark §6.6."""
    out = []
    i, n = 0, len(line)
    while i < n:
        if in_comment:
            close = line.find("-->", i)
            if close == -1:
                return "".join(out), True
            in_comment = False
            i = close + 3
            continue
        if line[i] == "`":
            j = i
            while j < n and line[j] == "`":
                j += 1
            close = _find_closing_ticks(line, j, j - i)
            if close != -1:
                end = close + (j - i)
                out.append(line[i:end])
                i = end
                continue
            out.append(line[i:j])
            i = j
            continue
        if line.startswith("<!--", i):
            if line.startswith("<!-->", i):
                i += 5
                continue
            if line.startswith("<!--->", i):
                i += 6
                continue
            close = line.find("-->", i + 4)
            if close == -1:
                if allow_multiline:
                    return "".join(out), True
                out.append(line[i : i + 4])
                i += 4
                continue
            i = close + 3
            continue
        out.append(line[i])
        i += 1
    return "".join(out), False


def _has_visible_note_content(text, allow_multiline_comment=True):
    """True when `text` (a `## Writer note:` title or collected body) carries plain-text Markdown prose or code
    (at least one non-whitespace decoded text character or matched backtick code span) after stripping HTML comments,
    `<script>`/`<style>` blocks, and complete CommonMark §6.6 HTML tags (raw HTML elements and widgets without prose
    do not satisfy the guard, and no HTML5 layout or CSS evaluation is performed)."""
    if not text.strip():
        return False
    i, n = 0, len(text)
    while i < n:
        ch = text[i]
        if ch.isspace():
            i += 1
            continue
        if ch == "`":
            j = i
            while j < n and text[j] == "`":
                j += 1
            close = _find_closing_ticks(text, j, j - i)
            if close != -1:
                return True
            return True
        if text.startswith("<!--", i):
            if text.startswith("<!-->", i):
                i += 5
                continue
            if text.startswith("<!--->", i):
                i += 6
                continue
            close = text.find("-->", i + 4)
            if close == -1:
                if allow_multiline_comment:
                    return False
                return True
            i = close + 3
            continue
        if ch == "<":
            m_ss = HTML_SCRIPT_STYLE_BLOCK.match(text, i)
            if m_ss:
                i = m_ss.end()
                continue
            m_tag = CM_HTML_TAG.match(text, i)
            if m_tag:
                i = m_tag.end()
                continue
            return True
        if ch == "&":
            m_ent = CM_ENTITY.match(text, i)
            if m_ent:
                if _decode_cm_entity(m_ent).strip():
                    return True
                i = m_ent.end()
                continue
            return True
        return True
    return False


def _advance_html_tag_state(line, state):
    """Advance multiline HTML comment, open-tag/attribute, `<script>`/`<style>`, processing-instruction (`<?...?>`),
    CDATA (`<![CDATA[...]]>`), and declaration (`<!NAME ...>`) state across `line` (outside code spans) so `## `
    inside any open CommonMark §6.6 raw-HTML construct does not split a `## Writer note:` body early."""
    in_comment, in_open_tag, tag_quote, tag_buf, in_script_style, in_pi, in_cdata, in_decl = state
    i, n = 0, len(line)
    while i < n:
        if in_script_style is not None:
            m_close = re.search(rf"</{in_script_style}[ \t\n\r\f]*>", line[i:], re.IGNORECASE)
            if not m_close:
                break
            in_script_style = None
            i += m_close.end()
            continue
        if in_comment:
            close = line.find("-->", i)
            if close == -1:
                break
            in_comment = False
            i = close + 3
            continue
        if in_pi:
            close = line.find("?>", i)
            if close == -1:
                break
            in_pi = False
            i = close + 2
            continue
        if in_cdata:
            close = line.find("]]>", i)
            if close == -1:
                break
            in_cdata = False
            i = close + 3
            continue
        if in_decl:
            close = line.find(">", i)
            if close == -1:
                break
            in_decl = False
            i = close + 1
            continue
        if in_open_tag:
            ch = line[i]
            tag_buf += ch
            if tag_quote:
                if ch == tag_quote:
                    tag_quote = None
            elif ch in ('"', "'"):
                tag_quote = ch
            elif ch == ">":
                in_open_tag = False
                m_ss = HTML_SCRIPT_STYLE_TAG.fullmatch(tag_buf)
                if m_ss:
                    in_script_style = m_ss.group(1).lower()
                tag_buf = ""
            i += 1
            continue
        if line[i] == "`":
            j = i
            while j < n and line[j] == "`":
                j += 1
            close = _find_closing_ticks(line, j, j - i)
            if close != -1:
                i = close + (j - i)
                continue
            i = j
            continue
        if line.startswith("<!--", i):
            if line.startswith("<!-->", i):
                i += 5
                continue
            if line.startswith("<!--->", i):
                i += 6
                continue
            close = line.find("-->", i + 4)
            if close == -1:
                in_comment = True
                break
            i = close + 3
            continue
        if line.startswith("<![CDATA[", i):
            close = line.find("]]>", i + 9)
            if close == -1:
                in_cdata = True
                break
            i = close + 3
            continue
        if line.startswith("<?", i):
            close = line.find("?>", i + 2)
            if close == -1:
                in_pi = True
                break
            i = close + 2
            continue
        if line.startswith("<!", i) and i + 2 < n and line[i + 2].isascii() and line[i + 2].isalpha():
            close = line.find(">", i + 3)
            if close == -1:
                in_decl = True
                break
            i = close + 1
            continue
        if line[i] == "<" and i + 1 < n and ((line[i + 1].isascii() and line[i + 1].isalpha()) or line[i + 1] == "/"):
            in_open_tag = True
            tag_quote = None
            tag_buf = line[i : i + 2]
            i += 2
            continue
        i += 1
    if in_open_tag:
        tag_buf += "\n"
    return (in_comment, in_open_tag, tag_quote, tag_buf, in_script_style, in_pi, in_cdata, in_decl)


def _has_resolution_entry(writer_lines, seen=None, raw_lines=None, ref_defs=None):
    """True when one pass's writer-authored lines hold at least one valid resolution outside fenced code blocks and
    HTML comment blocks: a populated `- Implemented:` list item (including continuation lines) with at least one
    plain-text description word outside Markdown links (distinct from commit references, ranges, refs, reflog/search
    selectors, and URLs/autolinks) and a standalone or Markdown-linked commit SHA resolving in git, a `- Filed: DNN
    TNN §N` line matching `todo_graph.FILED`, or a non-empty `## Writer note:` section (title text or visible body
    before the next `## `)."""
    if seen is None:
        seen = {}
    lines = writer_lines if raw_lines is None else raw_lines
    if ref_defs is None:
        ref_defs = _collect_ref_defs(lines)
    fence = None  # (char, length)
    in_comment = False
    idx, total = 0, len(lines)
    while idx < total:
        line = lines[idx]
        if in_comment:
            _, in_comment = _strip_comments_line(line, in_comment=True, allow_multiline=True)
            idx += 1
            continue
        if fence is not None:
            stripped = _expand_leading_tabs(line, 0)
            m_close = re.match(r"^[ ]{0,3}(`{3,}|~{3,})[ \t]*$", stripped)
            if m_close and m_close.group(1)[0] == fence[0] and len(m_close.group(1)) >= fence[1]:
                fence = None
            idx += 1
            continue
        expanded = _expand_leading_tabs(line, 0)
        if HTML_TYPE2_OPEN.match(expanded):
            _, in_comment = _strip_comments_line(expanded, in_comment=False, allow_multiline=True)
            idx += 1
            continue
        fm = FENCE_OPEN.match(expanded)
        if fm and not (fm.group(1)[0] == "`" and "`" in fm.group(2)):
            fence = (fm.group(1)[0], len(fm.group(1)))
            idx += 1
            continue
        m = IMPLEMENTED_LINE.match(line)
        if m:
            item_lines = [m.group(1)]
            j = idx + 1
            while j < total:
                nxt = lines[j]
                if not nxt.strip(" \t"):
                    break
                nxt_exp = _expand_leading_tabs(nxt, 0)
                if (FENCE_OPEN.match(nxt_exp) or ATX_HEADING.match(nxt_exp)
                        or BLOCKQUOTE_ONE.match(nxt_exp) or THEMATIC_BREAK.match(nxt_exp)
                        or LIST_ITEM_ONE.match(nxt_exp) or HTML_TYPE1_OPEN.match(nxt_exp)
                        or HTML_TYPE2_OPEN.match(nxt_exp) or HTML_TYPE3_OPEN.match(nxt_exp)
                        or HTML_TYPE4_OPEN.match(nxt_exp) or HTML_TYPE5_OPEN.match(nxt_exp)
                        or HTML_TYPE6_OPEN.match(nxt_exp) or HTML_TYPE7_OPEN.match(nxt_exp)):
                    break
                item_lines.append(nxt)
                j += 1
            segments = _unwrap_markdown_segments("\n".join(item_lines).strip(), ref_defs)
            visible_all, visible_outer = _strip_inline_emphasis_segments(segments, seen)
            parts = _expand_commit_sep_tokens(_split_revspec_tokens(visible_all), seen)
            commit_pos = set()
            for pos, raw in enumerate(parts):
                if "/" in raw or _is_url_token(raw):
                    continue
                tok = _strip_paired_md(raw)
                if COMMIT_TOKEN.fullmatch(tok) and _is_commit_in_git(tok, seen):
                    commit_pos.add(pos)
            if commit_pos:
                outer_parts = _expand_commit_sep_tokens(_split_revspec_tokens(visible_outer), seen)
                outer_non_url = [tok for tok in outer_parts if not _is_url_token(tok)]
                if outer_non_url and not _all_commit_refs(outer_non_url, seen):
                    return True
            idx = j
            continue
        if FILED_LINE.match(line):
            return True
        wn = WRITER_NOTE.match(line)
        if wn:
            # Strip optional CommonMark §4.2 ATX closing `#` sequence (must follow whitespace in the full heading).
            title = re.sub(r"[ \t]+#+[ \t]*$", "", wn.group(1))
            if _has_visible_note_content(title, allow_multiline_comment=False):
                return True
            k = idx + 1
            body_fence = None
            html_state = (False, False, None, "", None, False, False, False)
            at_block_start = True
            body_chunks = []
            while k < total:
                body_line = lines[k]
                if body_fence is not None:
                    stripped = _expand_leading_tabs(body_line, 0)
                    m_close = re.match(r"^[ ]{0,3}(`{3,}|~{3,})[ \t]*$", stripped)
                    if m_close and m_close.group(1)[0] == body_fence[0] and len(m_close.group(1)) >= body_fence[1]:
                        body_fence = None
                        at_block_start = True
                    elif body_line.strip():
                        body_chunks.append(" code ")
                    k += 1
                    continue
                in_raw_html = (html_state[0] or html_state[1] or html_state[4] is not None
                               or html_state[5] or html_state[6] or html_state[7])
                if not in_raw_html:
                    if not body_line.strip():
                        body_chunks.append("")
                        at_block_start = True
                        k += 1
                        continue
                    if body_line.startswith("## "):
                        break
                    body_exp = _expand_leading_tabs(body_line, 0)
                    if at_block_start and body_exp.startswith("    "):
                        body_chunks.append(" code ")
                        k += 1
                        continue
                    fm_body = FENCE_OPEN.match(body_exp)
                    if fm_body and not (fm_body.group(1)[0] == "`" and "`" in fm_body.group(2)):
                        body_fence = (fm_body.group(1)[0], len(fm_body.group(1)))
                        k += 1
                        continue
                body_chunks.append(body_line)
                html_state = _advance_html_tag_state(body_line, html_state)
                at_block_start = False
                k += 1
            if _has_visible_note_content("\n".join(body_chunks), allow_multiline_comment=True):
                return True
            idx = k
            in_comment = html_state[0]
            fence = body_fence
            continue
        idx += 1
    return False


def list_rounds(records_dir):
    """Every findings file under records_dir that holds rounds, with each round as `record` wrote it. A file
    whose round headings or lines are not `record`'s is named in `unreadable`, never dropped."""
    records_dir = Path(records_dir)
    records, unreadable = [], []

    def shown(path):
        # Never resolve: a path being reported as unreadable may be a symlink loop, which resolve() raises on.
        path = Path(os.path.abspath(path))
        return path.relative_to(ROOT).as_posix() if path.is_relative_to(ROOT) else path.as_posix()

    try:
        paths = sorted(records_dir.rglob("*.md")) if records_dir.is_dir() else []
    except OSError:  # a folder that cannot be walked is reported, like a file that cannot be read
        return {"records": [], "unreadable": [shown(records_dir)]}
    for path in paths:
        # One file that cannot be read, or is not UTF-8, is named in `unreadable`; the others are still listed.
        try:
            found = read_findings(path.read_bytes().decode("utf-8"))
        # RuntimeError: a symlink loop on older Pythons. ValueError: anything read_findings meets that int() or the
        # digest cannot take, so one bad file never aborts the listing.
        except (OSError, UnicodeDecodeError, RuntimeError, ValueError):
            found = None
        if found is None:
            unreadable.append(shown(path))
        elif found["rounds"] or found["completions"] or found["plans"] or found["reconciliations"]:
            records.append({"path": shown(path), "rounds": found["rounds"],
                            **({"completions": found["completions"]} if found["completions"] else {}),
                            **({"plans": found["plans"]} if found["plans"] else {}),
                            **({"reconciliations": found["reconciliations"]} if found["reconciliations"] else {})})
    return {"records": records, **({"unreadable": unreadable} if unreadable else {})}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--writer", choices=WRITERS, help="The agent whose work is under review")
    parser.add_argument("--prompt", help="File holding the review prompt")
    parser.add_argument("--record", help="Findings file to append this round (or completion review) to")
    parser.add_argument("--kind", choices=("round", "completion", "plan", "reconciliation"), default="round",
                        help="round (default): one review round; completion: the one-pass completion and excellence "
                             "review, recorded as a ## Completion review section and not counted as a round; plan: "
                             "the one-pass plan review before the build, recorded as a ## Plan review section; "
                             "reconciliation (three-pass pins): the pass after a completion that returned gaps")
    parser.add_argument("--candidate", help="The full 40-character commit under review: required with --kind "
                                            "completion, plan or reconciliation (for a plan review, the commit "
                                            "holding the section as reviewed) and for a three-pass section's round; "
                                            "optional for any other round, recorded as its Candidate line")
    parser.add_argument("--paths", nargs="*", default=[], metavar="FILE",
                        help="Three-pass pins with [[schedule.conditional]]: the files the section changes, for a "
                             "plan review or a review outside a section. A section's later passes take their changed "
                             "files from the candidate instead")
    parser.add_argument("--surface", action="store_true",
                        help="The section has Surface: yes, so the design lens applies: a rotation schedule includes "
                             "it in its rotation, and a three-pass round keeps it")
    parser.add_argument("--timeout", type=int, default=1800,
                        help="Seconds before the reviewer is stopped and the round records no review")
    parser.add_argument("--pin", default=str(PIN), help=argparse.SUPPRESS)
    parser.add_argument("--check-pin", action="store_true",
                        help="Report whether the pin loads, and each CLI's presence, instead of running a review; "
                             "no reviewer runs (exit 0 loads, 2 refused)")
    parser.add_argument("--list-rounds", action="store_true",
                        help="List the recorded review rounds instead of running one; no reviewer runs")
    parser.add_argument("--json", action="store_true", help="With --list-rounds, print the listing as JSON")
    parser.add_argument("--records",
                        help="With --list-rounds, the folder of findings files (default todo/plan.json's "
                             "layout.receipts, docs/reviews without it)")
    args = parser.parse_args(argv)
    if args.check_pin:
        # load_pin stays the only place the rules live: this reports its verdict and runs nothing.
        try:
            pin = load_pin(args.pin)
        except Refused as exc:
            print(json.dumps({"ok": False, "error": str(exc)}) if args.json else f"refused: {exc}")
            return 2
        clis = {cli: find_cli(cli) is not None for cli in CLIS}
        report = {"ok": True, "schema_version": pin["schema_version"], "reviewers": pin["reviewers"],
                  "routes": pin["routes"], **({"schedule": pin["schedule"]} if "schedule" in pin else {}), "clis": clis}
        print(json.dumps(report) if args.json else "ok")
        return 0
    if args.list_rounds:
        try:
            records = args.records or str(ROOT / receipts_folder())
        except Refused as exc:
            print(json.dumps({"ok": False, "error": str(exc)}) if args.json else f"refused: {exc}")
            return 2
        listing = list_rounds(records)
        if args.json:
            print(json.dumps(listing, indent=2))
        else:
            for entry in listing["records"]:
                for p in entry.get("plans", []):
                    who = f"{p['reviewer']} ({p['model']} via {p['cli']})" if p["reviewer"] else "no review"
                    print(f"{entry['path']}  Plan review: {who}, candidate {p['candidate']}, {p['run']}")
                for c in entry.get("completions", []):
                    who = f"{c['reviewer']} ({c['model']} via {c['cli']})" if c["reviewer"] else "no review"
                    print(f"{entry['path']}  Completion review: {who}, candidate {c['candidate']}, {c['run']}")
                for c in entry.get("reconciliations", []):
                    who = f"{c['reviewer']} ({c['model']} via {c['cli']})" if c["reviewer"] else "no review"
                    print(f"{entry['path']}  Reconciliation review: {who}, candidate {c['candidate']}, {c['run']}")
                for r in entry["rounds"]:
                    who = f"{r['reviewer']} ({r['model']} via {r['cli']})" if r["reviewer"] else "no review"
                    print(f"{entry['path']}  Round {r['number']}: {who}, writer {r['writer']}, {r['run']}")
            for path in listing.get("unreadable", []):
                print(f"{path}  unreadable: a round heading or line is not in the record format")
        return 0
    if not args.writer or not args.prompt:
        parser.error("--writer and --prompt are required unless --check-pin or --list-rounds is given")
    one_pass = args.kind in PASSES
    if one_pass and not (args.candidate and CANDIDATE.fullmatch(args.candidate)):
        parser.error(f"--kind {args.kind} needs --candidate, the full 40-character commit under review")
    if args.kind == "round" and args.candidate and not CANDIDATE.fullmatch(args.candidate):
        parser.error("--candidate must be the full 40-character commit under review")
    try:
        # A section's earlier chain, kept by the todo kit's `replan` (D00 T01 §9), is closed: nothing is appended to
        # it, and no reviewer runs for a pass that could not be recorded.
        if args.record and KEPT_CHAIN.search(Path(args.record).name):
            raise Refused(f"{args.record} is a kept earlier review chain (todo-graph.py replan): record the pass in "
                          "the section's own findings file")
        # A section keeps the pin its plan review read, schedule and route both.
        pin = section_pin(args.pin, args.record, args.kind, args.candidate)
        schedule = pin.get("schedule", {})
        three_pass = schedule.get("policy") == THREE_PASS
        paths = [p.replace("\\", "/") for p in args.paths]
        # A section's findings file takes one pass at a time under any pin: the todo kit's replan takes this same
        # lock before it moves the file, so a pass in flight never lands in the chain that replaces it (D00 T01 §9).
        if section_record(args.record):
            lock_section(args.record, args.timeout)
        if three_pass:
            check_three_pass_order(args.kind, args.record, args.candidate, schedule)
            if args.kind != "plan" and section_record(args.record):
                if paths:
                    raise Refused("--paths goes with a plan review or a review outside a section; this pass takes "
                                  "its changed files from its candidate")
                paths = changed_paths(returned_plan(args.record)["candidate"], args.candidate)
        elif args.kind == "reconciliation":
            raise Refused("this section's pin has no reconciliation pass; only policy = \"three-pass\" takes one")
        elif paths:
            raise Refused("--paths goes with a three-pass pin; this pin's schedule has no conditional lenses")
        plan = None
        if "schedule" in pin:
            rounds = []
            if args.record and args.kind == "round":
                # The position in the schedule comes from the rounds already recorded; a file that cannot be read
                # back is refused here, before a review is spent that could not be recorded.
                existing = existing_findings(Path(args.record), "round")
                rounds = read_rounds(existing)
                if rounds is None:
                    raise Unrecorded(f"{args.record} cannot be read back (a round heading or line is not in the "
                                     f"format --record writes{unreadable_heading(existing)}; {RESERVED_NAMES}), so "
                                     f"no round was run")
            plan = scheduled(schedule, args.kind, rounds, args.surface, paths)
        result = review(pin, args.writer, args.prompt, args.timeout, plan, args.kind)
    except (Refused, Unrecorded, OSError) as exc:
        print("review-dispatch refused: " + str(exc), file=sys.stderr)
        return 2
    when = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    if one_pass or args.candidate:
        result.update(kind=args.kind, candidate=args.candidate)
    if three_pass and result["reviewer"]:
        # What the review's worst finding means under the pin: only a blocking level holds the stamp or earns
        # another pass.
        worst = declared_highest(result["review"])
        result.update(highest=worst, blocking=review_pin.blocks(schedule, worst))
    if plan and plan["position"] and three_pass:
        result.update(position=plan["position"], escalated=False, severity_stop=False)
    elif plan and plan["position"]:
        result.update(position=plan["position"], escalated=plan["escalated"])
        stop_after = pin["schedule"]["severity_stop_after"]
        # The severity stop: a returned round past `severity_stop_after` whose worst finding is P3 ends the loop (0:
        # from round 1; todo_graph.SEVERITY_CUTOFF_ROUND is the same number, and `stamp`
        # re-checks it).
        result["severity_stop"] = bool(result["reviewer"]) and plan["position"] > stop_after \
            and highest(result["review"]) == "P3"
    if args.record:
        try:
            if one_pass:
                record_completion(Path(args.record), result, when, args.candidate, args.kind)
                result["recorded"] = True
            else:
                result["round"] = record(Path(args.record), result, when, args.candidate)
        except (Unrecorded, OSError) as exc:
            # The review still reaches stdout; only the findings file is left as it was. A write that fails (a full
            # disk, a folder that may not be written) is the same: the writes are atomic, so the file is whole, and a
            # review that was paid for is never lost with it.
            unrecorded = {"recorded": False} if one_pass else {"round": None}
            print(json.dumps({**result, **unrecorded, "run": when}, indent=2))
            why = str(exc) if isinstance(exc, Unrecorded) else \
                f"{args.record} could not be written ({exc.strerror or exc}), so the review was not recorded"
            print("review-dispatch refused: " + why, file=sys.stderr)
            return 2
    print(json.dumps({**result, "run": when}, indent=2))
    return 0 if result["reviewer"] else 1


if __name__ == "__main__":
    sys.exit(main())
