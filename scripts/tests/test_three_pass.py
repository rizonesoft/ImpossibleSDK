"""scripts/review-dispatch.py under policy = "three-pass": the pass order, the section's own pin, the caps, blocking
levels, lens verdict lines and conditional lenses, driven through the command line with a fake reviewer in a real git
repository."""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import textwrap
import time
import unittest
import unittest.mock

# The scratch repository and the fake reviewer CLIs; unittest discovery puts this folder on sys.path.
from test_review_dispatch import Harness

SCRIPTS_DIR = Path(__file__).resolve().parents[1]

PIN = textwrap.dedent('''\
    schema_version = 1

    [reviewers.sol]
    cli = "codex"
    model = "gpt-6.1-sol"
    effort = "xhigh"

    [routes]
    claude = ["sol"]
    gemini = ["sol"]

    [schedule]
    policy = "three-pass"
    pass_cap = 5

    [schedule.lenses]
    concurrency = "Which lock or teardown order is wrong on two CPUs?"

    [schedule.plan]
    effort = "medium"
    lenses = ["plan"]

    [[schedule.rounds]]
    effort = "medium"
    lenses = ["adversarial", "integration"]

    [schedule.completion]
    effort = "medium"
    lenses = ["plan-delivery", "record"]

    [schedule.reconciliation]
    effort = "medium"
    lenses = ["adversarial", "plan-delivery", "record"]

    [[schedule.conditional]]
    lenses = ["concurrency"]
    passes = ["plan", "round"]
    paths = ["src/kernel/"]
''')
RECORD = "docs/reviews/00-core/D00-T01-s1.md"
TODO_FILE = """---
schema_version: 1
id: core
domain: 00-core
status: active
title: "TODO-01 -- Core"
---

# TODO-01 -- Core

> **Goal:** g.

> **Current state:** c.

## Inputs

- i

## Outcome

- o

**Adjacency:** all=not-applicable (fixture)

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Thing | -- | [ ] |

## 1. Thing

> **Started:** 2026-11-17T00:00Z

**Phase:** 0
**Needs:** none
**Surface:** no

**Build order:**

1. Do it. Done when: it shows.

- [x] Work.
- [ ] Commit: `x: thing`

**Test checkpoint:** `true` exits 0; {how}.

## Verification

- v
"""


def reply(kind, lenses, top="none", gaps=False):
    """A complete reply of one kind: a verdict line per lens, HIGHEST, and the kind's final lines."""
    lines = [f"LENS {name}: {'findings' if gaps else 'clean'}" for name in lenses] + [f"HIGHEST: {top}"]
    if kind == "plan":
        lines += ["PLAN: gaps" if gaps else "PLAN: complete"]
    if kind in ("plan", "round"):
        lines += ["OVERALL: needs-attention" if gaps else "OVERALL: approve"]
    else:
        lines += [f"{kind.upper()}: {'gaps' if gaps else 'complete'}"]
    return "\n".join(["1. P2 a.py:3 is wrong." if gaps else "Nothing found.", *lines])


LENSES = {"plan": ["plan"], "round": ["adversarial", "integration"], "completion": ["plan-delivery", "record"],
          "reconciliation": ["adversarial", "plan-delivery", "record"]}


class ThreePass(Harness):
    def setUp(self):
        super().setUp()
        self.git_dir = str(Path(shutil.which("git")).parent)
        self.git("init", "-q", "-b", "main")
        self.git("config", "user.email", "t@example.com")
        self.git("config", "user.name", "T")
        self.git("config", "core.autocrlf", "false")
        self.record = self.repo / RECORD
        self.base = self.commit("pin", {".conclave/reviewers.toml": PIN})

    def git(self, *args):
        done = subprocess.run(["git", "-C", str(self.repo), *args], capture_output=True, text=True, timeout=60)
        self.assertEqual(done.returncode, 0, done.stderr)
        return done.stdout.strip()

    def commit(self, message, files=None):
        for name, text in (files or {}).items():
            path = self.repo / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="utf-8", newline="\n")
        self.git("add", "-A")
        self.git("commit", "-q", "--allow-empty", "-m", message)
        return self.git("rev-parse", "HEAD")

    def dispatch_env(self, **modes):
        # The dispatcher asks git for the section's pin and changed files, so git is on the fake PATH here.
        system_root = {"SYSTEMROOT": os.environ["SYSTEMROOT"]} if os.name == "nt" else {}
        return {"PATH": os.pathsep.join([str(self.bin), str(Path(sys.executable).parent), self.git_dir]),
                "FAKE_LOG": str(self.log), "FAKE_CHILD_PID": str(Path(self._tmp.name) / "child.pid"), **system_root,
                **{var: str(self.scratch) for var in ("TMP", "TEMP", "TMPDIR")},
                **{f"FAKE_{k.upper()}": v for k, v in modes.items()}}

    def dispatch(self, *args, **modes):
        result = subprocess.run([sys.executable, "-B", str(self.repo / "scripts/review-dispatch.py"), *args],
                                capture_output=True, text=True, env=self.dispatch_env(**modes), timeout=60,
                                cwd=str(self.repo))
        summary = json.loads(result.stdout) if result.stdout.strip().startswith("{") else None
        return result, summary

    def run_pass(self, kind, candidate, say=None, top="none", gaps=False, extra=(), record=True, writer="claude"):
        say = reply(kind, LENSES[kind], top, gaps) if say is None else say
        args = ["--kind", kind, "--writer", writer, "--prompt", str(self.prompt), *extra]
        args += ["--candidate", candidate] if candidate else []
        args += ["--record", str(self.record)] if record else []
        return self.dispatch(*args, codex="say", say=say)

    def refused(self, kind, candidate, needle, **kwargs):
        before = len(self.calls())
        result, _ = self.run_pass(kind, candidate, **kwargs)
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn(needle, result.stderr)
        self.assertEqual(len(self.calls()), before, "a refused pass must not reach the reviewer")
        return result

    def ok(self, kind, candidate, **kwargs):
        result, summary = self.run_pass(kind, candidate, **kwargs)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return summary

    def findings(self):
        return self.module().read_findings(self.record.read_bytes().decode("utf-8"))

    def resolve(self, sha, note=None):
        """Append a writer resolution entry naming commit `sha` to `self.record`."""
        line = note if note is not None else f"- Implemented: repaired gap in {sha[:8]}"
        with open(self.record, "a", encoding="utf-8", newline="") as handle:
            handle.write(f"\n{line}\n")

    def to_completion(self, top="P2", gaps=True):
        """Plan, build, round, then a completion review; returns the last candidate."""
        self.ok("plan", self.base)
        built = self.commit("build", {"src/app/a.py": "x = 1\n"})
        self.ok("round", built)
        self.ok("completion", built, top=top, gaps=gaps)
        return built


class Chain(ThreePass):
    def test_a_whole_chain_runs_and_reads_back(self):
        plan = self.ok("plan", self.base, gaps=True, top="P2")
        self.assertEqual((plan["kind"], plan["candidate"], plan["lenses"], plan["effort"]),
                         ("plan", self.base, ["plan"], "medium"))
        self.assertEqual((plan["highest"], plan["blocking"]), ("P2", True))
        built = self.commit("build", {"src/app/a.py": "x = 1\n"})
        rnd = self.ok("round", built)
        self.assertEqual((rnd["position"], rnd["severity_stop"], rnd["escalated"], rnd["round"], rnd["candidate"]),
                         (1, False, False, 1, built))
        self.assertEqual((rnd["highest"], rnd["blocking"]), ("none", False))
        completion = self.ok("completion", built, gaps=True, top="P2")
        self.assertEqual(completion["lenses"], ["plan-delivery", "record"])
        repaired = self.commit("repair", {"src/app/a.py": "x = 2\n"})
        self.resolve(repaired)
        done = self.ok("reconciliation", repaired)
        self.assertEqual((done["kind"], done["candidate"], done["recorded"]), ("reconciliation", repaired, True))
        found = self.findings()
        self.assertEqual([len(found[k]) for k in ("plans", "rounds", "completions", "reconciliations")], [1, 1, 1, 1])
        self.assertEqual(found["rounds"][0]["candidate"], built)
        self.assertEqual(found["reconciliations"][0]["lenses"], ["adversarial", "plan-delivery", "record"])
        listing, _ = self.dispatch("--list-rounds", "--records", str(self.repo / "docs/reviews"))
        self.assertIn("Reconciliation review: sol", listing.stdout)
        # The reviewer was given each lens's question and the verdict line it owes.
        prompt = self.calls()[1]["stdin"]
        self.assertIn("LENS adversarial: <clean|findings>", prompt)
        self.assertIn("- `integration`:", prompt)

    def test_a_kept_chain_is_refused_before_a_reviewer_runs_and_left_as_it_was(self):
        # D00 T01 §9: the todo kit's replan keeps a section's earlier chain as -chain<k>.md, closed to new passes.
        self.ok("plan", self.base)
        kept = self.record.with_name(self.record.stem + "-chain1.md")
        self.record.replace(kept)
        before = kept.read_bytes()
        self.record = kept
        for kind in ("plan", "round", "completion", "reconciliation"):
            with self.subTest(kind):
                self.refused(kind, self.base, "is a kept earlier review chain")
        # Round 1 of D00 T01 §9: Windows folds the case, so the same file by another spelling is the same chain.
        self.record = kept.with_name(kept.name.upper().replace(".MD", ".md"))
        self.refused("round", self.base, "is a kept earlier review chain")
        self.assertEqual(kept.read_bytes(), before, "a refused pass changed the kept chain")

    def test_a_pass_out_of_order_is_refused_before_a_reviewer_runs(self):
        self.refused("round", self.base, "needs the section's returned plan review first")
        self.refused("completion", self.base, "needs the section's returned plan review first")
        self.ok("plan", self.base)
        self.refused("plan", self.base, "the plan review runs once")
        built = self.commit("build", {"src/app/a.py": "x = 1\n"})
        self.refused("round", None, "a section's round needs --candidate")
        self.refused("round", "0" * 40, "is not a commit in this repository")
        self.git("checkout", "-q", "--orphan", "other")
        unrelated = self.commit("unrelated", {"z.txt": "z\n"})
        self.git("checkout", "-q", "main")
        self.refused("round", unrelated, "does not descend from the plan review's")
        self.refused("completion", built, "after the section's returned round")
        self.refused("reconciliation", built, "a reconciliation follows one round and a completion")
        self.ok("round", built)
        self.refused("round", built, "allows one round")
        self.refused("completion", self.base, "does not descend from the round's")
        self.ok("completion", built, gaps=True, top="P2")
        self.refused("completion", built, "runs once")
        self.refused("reconciliation", self.base, "does not descend from the last pass's")
        repaired = self.commit("repair", {"src/app/a.py": "x = 2\n"})
        self.resolve(repaired)
        self.ok("reconciliation", repaired)
        self.refused("reconciliation", repaired, "already returned complete")

    def test_a_complete_reconciliation_on_a_changed_contract_takes_another(self):
        # D07 T02 §1, 2026-11-17: a reconciliation ended complete on a candidate whose section text had changed since
        # the failed completion; `stamp` refused it and the dispatcher refused every further pass, so the section
        # could never stamp. The todo kit's library, beside the dispatcher, now tells the dispatcher so.
        # Beside the dispatcher in an installed project; in the SDK, the todo kit's own copy (reached through
        # .parent, which stops at the drive, so a shallow installed copy is never asked for a parent it lacks).
        library = SCRIPTS_DIR / "lib/todo_graph.py"
        if not library.is_file():
            library = SCRIPTS_DIR.parent.parent.parent / "todo/templates/scripts/lib/todo_graph.py"
        if not library.is_file():
            library = None
        if library is None:
            self.skipTest("the todo kit's scripts/lib/todo_graph.py is not beside this reviewer kit")
        (self.repo / "scripts/lib").mkdir(exist_ok=True)
        shutil.copy(library, self.repo / "scripts/lib/todo_graph.py")
        todo = "todo/00-core/TODO-01-core.md"
        # A whole TODO file the stamp accepts: the section's checklist done but its Commit, its Started line at the
        # commit the plan review reads, and the indexes that list it.
        section = TODO_FILE.replace("{how}", "{how}")
        indexes = {"todo/00-core/INDEX.md": "# Index\n- TODO-01-core.md\n",
                   "todo/TODO-00-INDEX.md": "# Index\n- 00-core/TODO-01-core.md\n"}
        # Under a pin with blocking levels (plan review P1 of D01 T02 §11): the complete reconciliation declares
        # HIGHEST: none, which the blocking rule alone would also have refused to follow.
        blocking = PIN.replace("pass_cap = 5", 'pass_cap = 5\nblocking = ["P1", "P2"]')
        self.base = self.commit("section", {todo: section.format(how="a mutation fails it"),
                                            ".conclave/reviewers.toml": blocking, **indexes})
        built = self.to_completion()
        # Changed in its promise (the checkpoint), not by a dated note: the stamp would refuse this candidate.
        changed = self.commit("reworded", {todo: section.format(how="another mutation fails it")})
        self.resolve(changed)
        self.ok("reconciliation", changed)
        # Complete-but-unsettled contract recovery: no new resolution entry is required after a complete pass.
        restored = self.commit("restored", {todo: section.format(how="a mutation fails it").replace(
            "fails it.", "fails it. **Corrected 2026-11-17:** the mutation is named in the findings file.")})
        self.ok("reconciliation", restored)
        self.refused("reconciliation", restored, "already returned complete")
        self.assertNotEqual(built, restored)
        # And it goes on to stamp (completion review of D01 T02 §11): the library's own stamp, then the graph reads the
        # receipt back.
        import importlib.util
        spec = importlib.util.spec_from_file_location("todo_graph_under_test", self.repo / "scripts/lib/todo_graph.py")
        graph = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = graph
        spec.loader.exec_module(graph)
        record = RECORD
        receipt = graph.stamp(self.repo, "D00 T01 §1", restored, [{"round": 1, "report": record, "verdict": "clean"}],
                              ["true -> ok"], "2026-11-17", completion={"report": record, "verdict": "reconciled"},
                              plan=record)
        data = json.loads((self.repo / receipt).read_text(encoding="utf-8"))
        self.assertEqual((data["passes_spent"], data["completion"]["candidate"]), (5, restored))
        self.assertTrue(graph.graph(self.repo)["D00 T01 §1"].done)

    def test_an_older_todo_library_beside_the_dispatcher_never_crashes_it(self):
        # Round 1 of D01 T02 §11: the reviewer kit updates apart from the todo kit, so the library beside the
        # dispatcher may predate reconciliation_settles, or hold neither function.
        for library, takes_another in (
                ("def section_contract(root, sha, ref):\n    return sha\n", True),   # the promise differs per commit
                ("X = 1\n", False)):
            with self.subTest(library=library.splitlines()[0]):
                self.tearDown()
                self.setUp()
                (self.repo / "scripts/lib").mkdir(exist_ok=True)
                (self.repo / "scripts/lib/todo_graph.py").write_text(library, encoding="utf-8")
                self.to_completion()
                changed = self.commit("reworded", {"todo/x.md": "x\n"})
                self.resolve(changed)
                self.ok("reconciliation", changed)
                later = self.commit("later", {"todo/x.md": "y\n"})
                if takes_another:
                    self.ok("reconciliation", later)
                else:
                    self.refused("reconciliation", later, "already returned complete")

    def lock_path(self, record):
        module = self.module()
        with unittest.mock.patch.object(module.tempfile, "gettempdir", return_value=str(self.scratch)):
            return module.section_lock_path(str(record))

    def test_a_second_launch_of_a_sections_pass_is_refused_while_the_first_runs(self):
        # Rounds 2 and 3 of D00 T01 §2: two launches both passed the order check and both spent a review, and a
        # launch with a shorter timeout took a live review's lock.
        self.ok("plan", self.base)
        built = self.commit("build", {"src/app/a.py": "x = 1\n"})
        lock = self.lock_path(self.record)
        live = f"{os.getpid()} {time.time() + 1800:.0f} feedfeedfeedfeed"  # a live owner: this test process
        lock.write_text(live, encoding="ascii")
        # The running review's own expiry decides, not this launch's timeout.
        self.refused("round", built, "is already running", extra=("--timeout", "1"))
        self.assertEqual(lock.read_text(encoding="ascii"), live, "a refused launch must leave the other run's lock")
        # A lock past the expiry its owner wrote belongs to a dead run: it is taken over, and released afterwards.
        lock.write_text(f"12345 {time.time() - 5:.0f} feedfeedfeedfeed", encoding="ascii")
        self.ok("round", built)
        self.assertFalse(lock.exists())
        self.assertFalse(lock.with_name(lock.name + ".takeover").exists())

    def test_an_expired_lock_is_taken_over_by_one_launch_at_a_time(self):
        self.ok("plan", self.base)
        built = self.commit("build", {"src/app/a.py": "x = 1\n"})
        lock = self.lock_path(self.record)
        expired = f"12345 {time.time() - 5:.0f} feedfeedfeedfeed"
        lock.write_text(expired, encoding="ascii")
        takeover = lock.with_name(lock.name + ".takeover")
        takeover.write_text("another launch", encoding="ascii")
        self.refused("round", built, "is already running")
        self.assertEqual(lock.read_text(encoding="ascii"), expired)
        # An abandoned takeover is cleared, so the launch after it goes through.
        old = time.time() - 3600
        os.utime(takeover, (old, old))
        self.refused("round", built, "is already running")
        self.assertFalse(takeover.exists())
        self.ok("round", built)
        # A lock that cannot be read is believed for the grace period, then taken over.
        lock.write_text("garbage", encoding="ascii")
        self.refused("completion", built, "is already running")
        os.utime(lock, (old, old))
        self.ok("completion", built)

    def test_a_lock_whose_owner_was_killed_is_taken_over_before_its_expiry(self):
        # 2026-11-12: a dispatch ended with taskkill /F never released its lock, and the section's next pass was
        # refused for the rest of the dead run's half-hour expiry.
        self.ok("plan", self.base)
        built = self.commit("build", {"src/app/a.py": "x = 1\n"})
        gone = subprocess.Popen([sys.executable, "-c", "pass"])
        gone.wait()
        lock = self.lock_path(self.record)
        lock.write_text(f"{gone.pid} {time.time() + 1800:.0f} feedfeedfeedfeed", encoding="ascii")
        self.ok("round", built)
        self.assertFalse(lock.exists())
        # A lock that cannot be read is still believed: no owner can be checked.
        lock.write_text("garbage", encoding="ascii")
        self.refused("completion", built, "is already running")

    @unittest.skipUnless(os.name == "posix", "on Windows the reviewer's kill-on-close job ends with the dispatcher")
    def test_a_killed_dispatchers_live_reviewer_keeps_the_lock_until_its_group_ends(self):
        # Review of the dead-owner takeover: on Linux and WSL the reviewer runs in its own session, so a dispatcher
        # killed with SIGKILL leaves it running, and a takeover then would pay for a second review alongside it.
        import signal
        self.ok("plan", self.base)
        built = self.commit("build", {"src/app/a.py": "x = 1\n"})
        child_pid = Path(self._tmp.name) / "child.pid"
        args = ["--kind", "round", "--writer", "claude", "--prompt", str(self.prompt), "--candidate", built,
                "--record", str(self.record), "--timeout", "120"]
        dispatcher = subprocess.Popen([sys.executable, "-B", str(self.repo / "scripts/review-dispatch.py"), *args],
                                      env=self.dispatch_env(codex="sleepchild"), cwd=str(self.repo),
                                      stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        group = None
        try:
            deadline = time.monotonic() + 30
            while not child_pid.exists() and time.monotonic() < deadline:
                time.sleep(0.05)
            self.assertTrue(child_pid.exists(), "the fake reviewer never started its child")
            os.kill(dispatcher.pid, signal.SIGKILL)
            dispatcher.wait(timeout=30)
            lock = self.lock_path(self.record)
            fields = lock.read_text(encoding="ascii").split()
            self.assertEqual(len(fields), 4, "the lock does not name the reviewer's process group")
            group = int(fields[3].partition(":")[0])
            self.refused("round", built, "is already running")  # its reviewer is still running
            os.killpg(group, signal.SIGKILL)
            child = int(child_pid.read_text())
            deadline = time.monotonic() + 30
            while time.monotonic() < deadline:
                try:
                    os.killpg(group, 0)
                except ProcessLookupError:
                    break
                time.sleep(0.05)
            with self.assertRaises(ProcessLookupError):
                os.kill(child, 0)
            group = None
            self.ok("round", built)  # the dispatcher and its reviewer are both gone: the lock is free
            self.assertFalse(lock.exists())
        finally:
            if dispatcher.poll() is None:
                dispatcher.kill()
                dispatcher.wait()
            if group is not None:
                try:
                    os.killpg(group, signal.SIGKILL)
                except ProcessLookupError:
                    pass  # the group ended already

    def start_killed_review(self):
        """A round whose dispatcher was killed with SIGKILL while its reviewer (and the reviewer's child) runs on:
        returns (built, lock path, reviewer group, child pid)."""
        import signal
        self.ok("plan", self.base)
        built = self.commit("build", {"src/app/a.py": "x = 1\n"})
        child_pid = Path(self._tmp.name) / "child.pid"
        args = ["--kind", "round", "--writer", "claude", "--prompt", str(self.prompt), "--candidate", built,
                "--record", str(self.record), "--timeout", "120"]
        dispatcher = subprocess.Popen([sys.executable, "-B", str(self.repo / "scripts/review-dispatch.py"), *args],
                                      env=self.dispatch_env(codex="sleepchild"), cwd=str(self.repo),
                                      stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        deadline = time.monotonic() + 30
        while not child_pid.exists() and time.monotonic() < deadline:
            time.sleep(0.05)
        os.kill(dispatcher.pid, signal.SIGKILL)
        dispatcher.wait(timeout=30)
        self.assertTrue(child_pid.exists(), "the fake reviewer never started its child")
        lock = self.lock_path(self.record)
        group = int(lock.read_text(encoding="ascii").split()[3].partition(":")[0])
        self.addCleanup(self.end_group, group)
        return built, lock, group, int(child_pid.read_text())

    def end_group(self, group):
        import signal
        try:
            os.killpg(group, signal.SIGKILL)
        except ProcessLookupError:
            pass  # already ended by the test

    def expire(self, lock, started=None):
        fields = lock.read_text(encoding="ascii").split()
        fields[1] = f"{time.time() - 5:.0f}"
        if started is not None:
            fields[3] = f"{fields[3].partition(':')[0]}:{started}"
        lock.write_text(" ".join(fields), encoding="ascii")

    @unittest.skipUnless(os.name == "posix", "the reviewer group is recorded on Linux and WSL")
    def test_an_overdue_reviewer_of_a_killed_dispatcher_is_ended_before_the_takeover(self):
        # Review of the dead-owner takeover: past its expiry the killed dispatcher's reviewer still ran, and the
        # expiry alone admitted a second review beside it.
        built, lock, group, child = self.start_killed_review()
        self.expire(lock)
        self.ok("round", built)  # the overdue group is ended, then the pass runs
        with self.assertRaises(ProcessLookupError):
            os.killpg(group, 0)
        with self.assertRaises(ProcessLookupError):
            os.kill(child, 0)

    @unittest.skipUnless(os.name == "posix", "the reviewer group is recorded on Linux and WSL")
    def test_an_overdue_group_whose_leader_exited_is_still_ended_by_its_members(self):
        # Third review of the takeover: with the leader gone, /proc had no start time for it, the cleanup refused, and
        # the leader's surviving child blocked the section for ever.
        import signal
        built, lock, group, child = self.start_killed_review()
        os.kill(group, signal.SIGKILL)  # the reviewer itself; its child, in the same group, runs on
        deadline = time.monotonic() + 30
        while self.module()._start_time(group) != "0" and time.monotonic() < deadline:
            time.sleep(0.05)
        os.killpg(group, 0)  # the group lives on in the child
        self.expire(lock)
        self.ok("round", built)
        with self.assertRaises(ProcessLookupError):
            os.kill(child, 0)
    @unittest.skipUnless(os.name == "posix", "the reviewer group is recorded on Linux and WSL")
    def test_a_group_whose_start_time_differs_is_never_ended_and_keeps_the_lock(self):
        built, lock, group, child = self.start_killed_review()
        self.expire(lock, started="1")  # as if the id now named another process
        self.refused("round", built, "is already running")
        os.killpg(group, 0)  # untouched: still alive

    @unittest.skipUnless(os.name == "posix", "the gate is the POSIX launcher")
    def test_the_gate_runs_the_reviewer_only_after_go(self):
        module = self.module()
        for say, ran in ((b"go", True), (b"", False)):
            with self.subTest(say=say):
                marker = Path(self._tmp.name) / f"ran-{ran}"
                read, write = os.pipe()
                status, status_write = os.pipe()
                launcher = subprocess.Popen([sys.executable, "-c", module.REVIEWER_GATE, str(read), str(status_write),
                                             sys.executable, "-c", f"open({str(marker)!r}, 'w').close()"],
                                            pass_fds=(read, status_write))
                os.close(read)
                os.close(status_write)
                os.write(write, say) if say else None
                os.close(write)
                self.assertEqual(launcher.wait(timeout=30), 0 if ran else 125)
                self.assertEqual(marker.exists(), ran)
                if ran:
                    self.assertEqual(module.read_exec_status(status), "", "a successful exec closes the status pipe")
                os.close(status)

    @unittest.skipUnless(os.name == "posix", "the reviewer group is recorded on Linux and WSL")
    def test_a_group_that_cannot_be_recorded_runs_no_reviewer(self):
        module = self.module()
        lock = Path(self._tmp.name) / "held.lock"
        lock.write_text("someone else's lock", encoding="ascii")
        marker = Path(self._tmp.name) / "reviewer-ran"
        out = Path(self._tmp.name) / "out.txt"
        real = module.note_reviewer_group

        def slow_refusal(group):
            time.sleep(1.5)  # long enough for a reviewer let through without the gate to have run
            real(group)
        with unittest.mock.patch.dict(module._HELD, {"path": lock, "owned": "this run's text"}), \
                unittest.mock.patch.object(module, "note_reviewer_group", slow_refusal):
            code, why = module.run([sys.executable, "-c", f"open({str(marker)!r}, 'w').close()"], None, 30, [out])
        self.assertIsNone(code)
        self.assertIn("could not start: record the reviewer's group", why)
        time.sleep(0.5)
        self.assertFalse(marker.exists(), "the reviewer ran although its group was not recorded")

    @unittest.skipUnless(os.name == "nt", "the kill-on-close job is Windows only")
    def test_a_dispatcher_killed_on_windows_takes_its_reviewer_down_and_frees_the_lock(self):
        # taskkill /F is TerminateProcess: no finally runs, so the job's kill-on-close limit must end the reviewer.
        self.ok("plan", self.base)
        built = self.commit("build", {"src/app/a.py": "x = 1\n"})
        child_pid = Path(self._tmp.name) / "child.pid"
        args = ["--kind", "round", "--writer", "claude", "--prompt", str(self.prompt), "--candidate", built,
                "--record", str(self.record), "--timeout", "120"]
        dispatcher = subprocess.Popen([sys.executable, "-B", str(self.repo / "scripts/review-dispatch.py"), *args],
                                      env=self.dispatch_env(codex="sleepchild"), cwd=str(self.repo),
                                      stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            deadline = time.monotonic() + 30
            while not child_pid.exists() and time.monotonic() < deadline:
                time.sleep(0.05)
            self.assertTrue(child_pid.exists(), "the fake reviewer never started its child")
            dispatcher.kill()
            dispatcher.wait(timeout=30)
            child = int(child_pid.read_text())
            module = self.module()
            deadline = time.monotonic() + 30
            while not module._process_gone(child) and time.monotonic() < deadline:
                time.sleep(0.05)
            self.assertTrue(module._process_gone(child), "the reviewer's child outlived the killed dispatcher")
            self.assertTrue(self.lock_path(self.record).exists(), "a killed dispatcher cannot release its lock")
            self.ok("round", built)
        finally:
            if dispatcher.poll() is None:
                dispatcher.kill()
                dispatcher.wait()
    def test_a_review_outside_a_section_takes_no_lock(self):
        self.record = self.repo / "docs/reviews/topic.md"
        other = self.lock_path(self.record)
        other.write_text(f"{os.getpid()} {time.time() + 1800:.0f} feedfeedfeedfeed", encoding="ascii")
        self.ok("round", None)

    def test_a_completion_that_is_complete_takes_no_reconciliation(self):
        built = self.to_completion(top="none", gaps=False)
        self.refused("reconciliation", built, "a completion review that returned gaps")

    def test_a_rotation_pin_has_no_reconciliation_and_takes_no_paths(self):
        rotation = PIN.partition("[schedule]")[0]
        base = self.commit("no schedule", {".conclave/reviewers.toml": rotation})
        self.record = self.repo / "docs/reviews/00-core/D00-T01-s2.md"
        self.refused("reconciliation", base, "only policy = \"three-pass\" takes one")
        self.refused("round", None, "--paths goes with a three-pass pin", extra=("--paths", "src/kernel/a.c"))


class Incomplete(ThreePass):
    def test_a_reply_missing_a_lens_line_is_recorded_as_no_review_and_can_be_rerun(self):
        self.ok("plan", self.base)
        built = self.commit("build", {"src/app/a.py": "x = 1\n"})
        partial = reply("round", ["adversarial"])
        result, summary = self.run_pass("round", built, say=partial)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIsNone(summary["reviewer"])
        self.assertEqual(summary["incomplete"], ["integration"])
        self.assertIn("incomplete: no LENS verdict line for integration", summary["failover"][0]["reason"])
        self.assertEqual(summary["review"].splitlines(), partial.splitlines())
        text = self.record.read_text(encoding="utf-8")
        self.assertIn("## Round 1: none", text)
        self.assertIn("- Skipped: sol (incomplete: no LENS verdict line for integration)", text)
        self.assertIn("LENS adversarial: clean", text)
        # It did not count as the section's round: the round runs again, and only then may the completion follow.
        self.refused("completion", built, "after the section's returned round")
        again = self.ok("round", built)
        self.assertEqual((again["round"], again["position"]), (2, 1))
        self.ok("completion", built)

    def test_a_complete_verdict_that_declares_a_blocking_finding_is_not_a_review(self):
        # Round 1 of D00 T01 §3: HIGHEST: P1 then COMPLETION: complete was recorded as a complete completion.
        built = self.commit("build", {"src/app/a.py": "x = 1\n"})
        self.ok("plan", self.base)
        self.ok("round", built)
        result, summary = self.run_pass("completion", built, top="P1", gaps=False)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("ends COMPLETION: complete but declares HIGHEST: P1", summary["failover"][0]["reason"])
        self.assertIn("COMPLETION: complete", summary["review"])
        # It did not count: the completion review runs again.
        self.ok("completion", built)

    def test_a_reply_without_its_final_verdict_lines_is_incomplete_for_every_kind(self):
        no_footer = "LENS plan: clean\nHIGHEST: none\nLooks fine."
        result, summary = self.run_pass("plan", self.base, say=no_footer)
        self.assertEqual(result.returncode, 1)
        self.assertIn("no final plan verdict line", summary["failover"][0]["reason"])
        self.ok("plan", self.base)
        built = self.commit("build", {"src/app/a.py": "x = 1\n"})
        self.ok("round", built)
        result, summary = self.run_pass("completion", built, say="LENS plan-delivery: clean\nLENS record: clean\nfine")
        self.assertEqual(result.returncode, 1)
        self.assertIn("no final completion verdict line", summary["failover"][0]["reason"])
        self.ok("completion", built, gaps=True, top="P2")
        repaired = self.commit("repair", {"src/app/a.py": "x = 2\n"})
        self.resolve(repaired)
        result, _ = self.run_pass("reconciliation", repaired, say=reply("reconciliation", ["adversarial"]))
        self.assertEqual(result.returncode, 1)
        # Four returned passes so far under pass_cap 5: the failed attempts did not count, so this one still runs.
        self.ok("reconciliation", repaired)
        found = self.findings()
        self.assertEqual([len(found[k]) for k in ("plans", "completions", "reconciliations")], [2, 2, 2])


class Caps(ThreePass):
    def gaps_until_refused(self, tops):
        """Completion with gaps, then one reconciliation with gaps per severity in `tops`; returns the last commit."""
        built = self.to_completion(top=tops[0])
        for n, top in enumerate(tops[1:], 1):
            built = self.commit(f"repair {n}", {"src/app/a.py": f"x = {n + 1}\n"})
            self.resolve(built)
            self.ok("reconciliation", built, gaps=True, top=top)
        return built

    def test_a_section_that_has_spent_its_passes_is_refused(self):
        last = self.gaps_until_refused(["P2", "P2", "P2"])   # plan, round, completion, two reconciliations: five
        again = self.commit("repair again", {"src/app/a.py": "x = 9\n"})
        # A pin that does not say otherwise ships at the cap.
        self.refused("reconciliation", again, "has spent its 5 review passes (pass_cap) and ships (at_cap)")
        del last

    def test_a_pin_that_holds_at_the_cap_keeps_the_section_blocked(self):
        self.base = self.commit("hold", {".conclave/reviewers.toml": PIN.replace("pass_cap = 5",
                                                                                 'pass_cap = 4\nat_cap = "hold"')})
        self.gaps_until_refused(["P2", "P2"])
        self.refused("reconciliation", self.commit("again", {"src/app/a.py": "x = 9\n"}), "(pass_cap); it stays blocked")

    def test_the_cap_is_the_one_in_the_sections_own_pin(self):
        self.base = self.commit("cap 4", {".conclave/reviewers.toml": PIN.replace("pass_cap = 5", "pass_cap = 4")})
        self.gaps_until_refused(["P2", "P2"])
        # Raising the cap in a later commit does not reach a section whose plan review read the earlier pin.
        again = self.commit("cap 9", {".conclave/reviewers.toml": PIN.replace("pass_cap = 5", "pass_cap = 9")})
        self.refused("reconciliation", again, "has spent its 4 review passes (pass_cap)")

    def test_a_section_at_a_cap_that_ships_is_told_to_stamp_capped(self):
        self.base = self.commit("ship", {".conclave/reviewers.toml": PIN.replace("pass_cap = 5",
                                                                                 'pass_cap = 4')})
        self.gaps_until_refused(["P2", "P2"])
        again = self.commit("repair again", {"src/app/a.py": "x = 9\n"})
        self.refused("reconciliation", again, "has spent its 4 review passes (pass_cap) and ships (at_cap)")

    def test_a_later_pin_that_ships_lowers_the_cap_of_a_section_under_review(self):
        self.gaps_until_refused(["P2"])  # three of five passes spent
        lower = self.commit("ship at 3", {".conclave/reviewers.toml": PIN.replace("pass_cap = 5",
                                                                                  'pass_cap = 3')})
        self.refused("reconciliation", lower, "has spent its 3 review passes (pass_cap) and ships (at_cap)")

    def test_no_progress_cap_counts_reconciliations_that_got_no_less_severe(self):
        stalled = PIN.replace("pass_cap = 5", "pass_cap = 30\nno_progress_cap = 2")
        self.base = self.commit("stall 2", {".conclave/reviewers.toml": stalled})
        # P1, then P2 (progress), then P2 and P2: two in a row without progress.
        self.gaps_until_refused(["P1", "P2", "P2"])
        mid = self.commit("repair mid", {"src/app/a.py": "x = 50\n"})
        self.resolve(mid)
        # One stalled pass so far, and a failed attempt does not add to it.
        result, _ = self.run_pass("reconciliation", mid, say="no verdict lines at all")
        self.assertEqual(result.returncode, 1)
        self.ok("reconciliation", mid, gaps=True, top="P2")
        last = self.commit("repair last", {"src/app/a.py": "x = 51\n"})
        self.refused("reconciliation", last, "made no progress (no_progress_cap 2), so it ships (at_cap)")

    def test_without_no_progress_cap_only_pass_cap_stops_the_loop(self):
        self.base = self.commit("cap 8", {".conclave/reviewers.toml": PIN.replace("pass_cap = 5", "pass_cap = 8")})
        last = self.gaps_until_refused(["P2", "P2", "P2", "P2", "P2", "P2"])
        self.refused("reconciliation", self.commit("more", {"src/app/a.py": "x = 99\n"}), "pass_cap")
        del last


def recurring_reply(times, cite=True):
    """A reconciliation that ends gaps with a P2 finding reported open in `times` passes, its mark citing the
    completion and reconciliations 1 to times - 2 unless `cite` is False; `times` None leaves the line out."""
    text = reply("reconciliation", LENSES["reconciliation"], "P2", True)
    if times is None:
        return text
    body, last = text.rsplit("\n", 1)
    cited = ", ".join(["completion", *(f"reconciliation {n}" for n in range(1, times - 1))])
    mark = f"2. P2 b.py:4 again (recurring: {cited})\n" if cite and times > 1 else ""
    return f"{mark}{body}\nRECURRING: {'none' if times == 1 else times}\n{last}"


class Recurrence(ThreePass):
    """A finding the repairs keep bringing back stops the chain; new findings at one severity never do."""

    def setUp(self):
        super().setUp()
        self.base = self.commit("recurrence 3", {".conclave/reviewers.toml":
                                                 PIN.replace("pass_cap = 5", "pass_cap = 30\nrecurrence_cap = 3")})

    def reconcile(self, times, cite=True):
        self.n = getattr(self, "n", 0) + 1
        built = self.commit(f"repair {self.n}", {"src/app/a.py": f"x = {self.n + 1}\n"})
        self.resolve(built)
        self.ok("reconciliation", built, say=recurring_reply(times, cite))
        return built

    def next_candidate(self):
        return self.commit("repair next", {"src/app/a.py": "x = 99\n"})

    def test_a_finding_open_in_three_passes_stops_the_chain(self):
        self.to_completion()
        self.reconcile(2)
        self.reconcile(3)
        self.refused("reconciliation", self.next_candidate(), "(recurrence_cap 3), so the section ships (at_cap)")

    def test_new_findings_at_one_severity_keep_the_chain_running(self):
        self.to_completion()
        for _ in range(4):
            self.reconcile(1)
        nxt = self.next_candidate()
        self.resolve(nxt)
        self.ok("reconciliation", nxt, say=recurring_reply(1))

    def test_an_uncited_count_does_not_stop_the_chain(self):
        self.to_completion()
        self.reconcile(1)
        self.reconcile(3, cite=False)
        nxt = self.next_candidate()
        self.resolve(nxt)
        self.ok("reconciliation", nxt, say=recurring_reply(1))

    def test_a_reconciliation_without_its_line_is_incomplete_and_is_asked_for_it(self):
        self.to_completion()
        built = self.commit("repair", {"src/app/a.py": "x = 2\n"})
        self.resolve(built)
        result, summary = self.run_pass("reconciliation", built, say=recurring_reply(None))
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("no single RECURRING line", summary["failover"][0]["reason"])
        self.assertIn("RECURRING: <n>", self.calls()[-1]["stdin"])
        self.assertIn("takes no number", self.calls()[-1]["stdin"])

        self.ok("reconciliation", built, say=recurring_reply(1))

    def test_a_pin_without_the_cap_never_asks_for_the_line(self):
        self.base = self.commit("no cap", {".conclave/reviewers.toml": PIN})
        self.to_completion()
        built = self.commit("repair", {"src/app/a.py": "x = 2\n"})
        self.resolve(built)
        self.ok("reconciliation", built, say=recurring_reply(None))

class Blocking(ThreePass):
    def setUp(self):
        super().setUp()
        pin = PIN.replace("pass_cap = 5", 'pass_cap = 9\nblocking = ["P1", "P2"]')
        self.base = self.commit("blocking", {".conclave/reviewers.toml": pin})

    def test_a_pass_below_the_blocking_levels_takes_no_further_pass(self):
        built = self.to_completion(top="P3")
        repaired = self.commit("repair", {"src/app/a.py": "x = 2\n"})
        self.refused("reconciliation", repaired, "worst finding is P3, below the pin's blocking levels (P1, P2)")
        del built

    def test_a_pin_without_blocking_never_ends_the_loop_on_severity(self):
        # Round 1 of D00 T01 §2: a reply ending HIGHEST: none then COMPLETION: gaps crashed on the missing key.
        self.base = self.commit("no blocking", {".conclave/reviewers.toml": PIN})
        self.to_completion(top="none")
        repaired = self.commit("repair", {"src/app/a.py": "x = 2\n"})
        self.resolve(repaired)
        self.ok("reconciliation", repaired)

    def test_a_blocking_pass_is_followed_by_a_reconciliation(self):
        self.to_completion(top="P2")
        repaired = self.commit("repair", {"src/app/a.py": "x = 2\n"})
        self.resolve(repaired)
        self.ok("reconciliation", repaired, gaps=True, top="P3")
        # That reconciliation ended below the blocking levels, so the loop ends there too.
        self.refused("reconciliation", self.commit("more", {"src/app/a.py": "x = 3\n"}), "below the pin's blocking")

    def test_the_summary_says_whether_the_worst_finding_blocks(self):
        for top, want in (("P2", ("P2", True)), ("P3", ("P3", False)), ("none", ("none", False))):
            with self.subTest(top=top):
                summary = self.ok("plan", self.base, top=top, gaps=top != "none", record=False)
                self.assertEqual((summary["highest"], summary["blocking"]), want)
        # Round 2 of D00 T01 §3: a reply with no HIGHEST line gave the blocking rule nothing to read. It is incomplete.
        silent = "LENS plan: findings\nPLAN: gaps\nOVERALL: needs-attention"
        result, summary = self.run_pass("plan", self.base, say=silent, record=False)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("no HIGHEST line", summary["failover"][0]["reason"])
        self.assertNotIn("blocking", summary)


class SectionPin(ThreePass):
    def test_a_section_finishes_under_the_pin_its_plan_review_read(self):
        self.ok("plan", self.base)
        changed = PIN.replace('model = "gpt-6.1-sol"', 'model = "gpt-9-other"').replace(
            'lenses = ["adversarial", "integration"]', 'lenses = ["quality"]')
        built = self.commit("pin moved", {".conclave/reviewers.toml": changed, "src/app/a.py": "x = 1\n"})
        summary = self.ok("round", built)
        self.assertEqual((summary["model"], summary["lenses"]), ("gpt-6.1-sol", ["adversarial", "integration"]))
        self.assertIn("gpt-6.1-sol", self.calls()[-1]["argv"])
        # A working-tree pin that no longer loads, or has switched policy, does not stop the section either.
        (self.repo / ".conclave/reviewers.toml").write_text("not toml [", encoding="utf-8")
        self.ok("completion", built)

    def test_a_plan_review_runs_under_the_pin_at_its_own_candidate(self):
        moved = self.commit("new route", {".conclave/reviewers.toml": PIN.replace("gpt-6.1-sol", "gpt-9-other")})
        summary = self.ok("plan", self.base)
        self.assertEqual(summary["model"], "gpt-6.1-sol")
        del moved

    def test_a_plan_candidate_without_a_loadable_pin_is_refused_naming_the_commit(self):
        self.git("rm", "-q", ".conclave/reviewers.toml")
        bare = self.commit("no pin")
        self.refused("plan", bare, "no reviewer pin at")
        broken = self.commit("broken pin", {".conclave/reviewers.toml": PIN.replace("pass_cap = 5", "pass_cap = 2")})
        self.refused("plan", broken, f"the reviewer pin at {broken} does not load: [schedule] pass_cap")

    def test_a_rotation_section_follows_the_working_tree_pin_as_it_always_did(self):
        # Round 1 of D00 T01 §2: keeping the plan review's pin is the three-pass rule; a rotation section is not bound.
        rotation = PIN.partition("[schedule]")[0]
        base = self.commit("rotation", {".conclave/reviewers.toml": rotation})
        self.ok("plan", base, say="x\nPLAN: complete\nOVERALL: approve")
        self.commit("new model", {".conclave/reviewers.toml": rotation.replace("gpt-6.1-sol", "gpt-9-other")})
        summary = self.ok("round", None, say="fine\nOVERALL: approve")
        self.assertEqual(summary["model"], "gpt-9-other")

    def test_a_three_pass_plan_review_needs_its_pin_committed_at_the_candidate(self):
        # Round 4 of D00 T01 §2: a pin that exists only in the working tree froze nothing for the section.
        rotation = PIN.partition("[schedule]")[0]
        for label, text in (("rotation pin committed", rotation), ("no pin committed", None)):
            with self.subTest(case=label):
                if text is None:
                    self.git("rm", "-q", "-f", ".conclave/reviewers.toml")
                    candidate = self.commit("no pin")
                else:
                    candidate = self.commit("rotation", {".conclave/reviewers.toml": text})
                (self.repo / ".conclave").mkdir(exist_ok=True)
                (self.repo / ".conclave/reviewers.toml").write_text(PIN, encoding="utf-8")
                self.refused("plan", candidate, f"the plan review's candidate {candidate} does not hold it")
                self.git("add", "-A")

    def test_a_section_that_began_under_rotation_finishes_under_it_after_the_switch(self):
        rotation = PIN.partition("[schedule]")[0]
        base = self.commit("rotation", {".conclave/reviewers.toml": rotation})
        self.ok("plan", base, say="x\nPLAN: complete\nOVERALL: approve")
        self.commit("switch to three-pass", {".conclave/reviewers.toml": PIN.replace("gpt-6.1-sol", "gpt-9-other")})
        # Under the three-pass pin this second round would be refused and the model would be the new one.
        for number in (1, 2):
            summary = self.ok("round", None, say="fine\nOVERALL: approve")
            self.assertEqual((summary["round"], summary["model"]), (number, "gpt-6.1-sol"))
            self.assertNotIn("blocking", summary)

    def test_a_respelled_findings_path_is_still_the_sections_on_windows(self):
        self.ok("plan", self.base)
        built = self.commit("build", {"src/app/a.py": "x = 1\n"})
        self.ok("round", built)
        if os.name != "nt":
            self.skipTest("only Windows opens a differently cased path as the same file")
        self.record = self.repo / RECORD.replace("docs/reviews", "DOCS/Reviews")
        self.refused("round", built, "allows one round")

    def test_a_review_outside_a_section_uses_the_working_tree_pin(self):
        self.record = self.repo / "docs/reviews/topic.md"
        (self.repo / ".conclave/reviewers.toml").write_text(PIN.replace("gpt-6.1-sol", "gpt-9-other"),
                                                            encoding="utf-8")
        self.assertEqual(self.ok("round", None)["model"], "gpt-9-other")
        # No order outside a section: a second round is just another round.
        self.assertEqual(self.ok("round", None)["round"], 2)


class Conditional(ThreePass):
    def test_a_conditional_lens_joins_a_pass_whose_candidate_changed_a_matching_file(self):
        self.ok("plan", self.base)
        built = self.commit("kernel", {"src/kernel/schéd.c": "int x;\n", "README.md": "x\n"})
        both = LENSES["round"] + ["concurrency"]
        summary = self.ok("round", built, say=reply("round", both))
        self.assertEqual(summary["lenses"], both)
        self.assertIn("- `concurrency`: Which lock or teardown order is wrong on two CPUs?",
                      self.calls()[-1]["stdin"])
        self.assertEqual(self.findings()["rounds"][0]["lenses"], both)
        # Completion is not one of the entry's passes.
        self.assertEqual(self.ok("completion", built)["lenses"], LENSES["completion"])

    def test_a_failed_changed_file_lookup_is_a_refusal(self):
        # Round 5 of D00 T01 §2: the test below stops at the pin lookup, so the diff failure itself had no test.
        module = self.module()
        self.assertEqual(module.changed_paths(self.base, self.base), [])
        with self.assertRaises(module.Refused) as refused:
            module.changed_paths("0" * 40, self.base)
        self.assertIn("cannot list the files changed between", str(refused.exception))

    def test_a_missing_git_refuses_before_a_reviewer_runs(self):
        self.ok("plan", self.base)
        built = self.commit("build", {"src/kernel/a.c": "int x;\n"})
        before = len(self.calls())
        # The base harness's PATH holds the fake reviewers and Python only: git cannot be found.
        result, _ = Harness.dispatch(self, "--kind", "round", "--writer", "claude", "--prompt", str(self.prompt),
                                     "--candidate", built, "--record", str(self.record), codex="say",
                                     say=reply("round", LENSES["round"]))
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("review-dispatch refused", result.stderr)
        self.assertEqual(len(self.calls()), before)
        self.assertEqual(len(self.findings()["rounds"]), 0)

    def test_a_file_moved_out_of_a_watched_folder_still_counts(self):
        self.base = self.commit("kernel file", {"src/kernel/a.c": "int x;\nint y;\nint z;\n"})
        self.ok("plan", self.base)
        (self.repo / "src/app").mkdir(parents=True, exist_ok=True)
        self.git("mv", "src/kernel/a.c", "src/app/a.c")
        moved = self.commit("moved out")
        both = LENSES["round"] + ["concurrency"]
        self.assertEqual(self.ok("round", moved, say=reply("round", both))["lenses"], both)

    def test_no_matching_file_means_no_conditional_lens(self):
        self.ok("plan", self.base)
        built = self.commit("app only", {"src/app/a.py": "x = 1\n", "src/kernelish.c": "int y;\n"})
        self.assertEqual(self.ok("round", built)["lenses"], LENSES["round"])

    def test_paths_serve_a_plan_review_and_a_standalone_review_only(self):
        summary = self.ok("plan", self.base, say=reply("plan", ["plan", "concurrency"]),
                          extra=("--paths", "src\\kernel\\a.c"))
        self.assertEqual(summary["lenses"], ["plan", "concurrency"])
        built = self.commit("build", {"src/app/a.py": "x = 1\n"})
        self.refused("round", built, "--paths goes with a plan review or a review outside a section",
                     extra=("--paths", "src/kernel/a.c"))
        self.record = self.repo / "docs/reviews/topic.md"
        standalone = self.ok("round", None, say=reply("round", LENSES["round"] + ["concurrency"]),
                             extra=("--paths", "src/kernel/a.c", "docs/x.md"))
        self.assertEqual(standalone["lenses"], LENSES["round"] + ["concurrency"])

    def test_surface_still_gates_design_on_a_pin_without_conditional_entries(self):
        plain = PIN.partition("[[schedule.conditional]]")[0].replace(
            'lenses = ["adversarial", "integration"]', 'lenses = ["adversarial", "design"]')
        self.base = self.commit("design in the round", {".conclave/reviewers.toml": plain})
        self.ok("plan", self.base)
        built = self.commit("build", {"src/app/a.py": "x = 1\n"})
        self.record = self.repo / "docs/reviews/topic.md"
        (self.repo / ".conclave/reviewers.toml").write_text(plain, encoding="utf-8")
        self.assertEqual(self.ok("round", None, say=reply("round", ["adversarial"]))["lenses"], ["adversarial"])
        with_surface = self.ok("round", None, say=reply("round", ["adversarial", "design"]), extra=("--surface",))
        self.assertEqual(with_surface["lenses"], ["adversarial", "design"])
        del built


class Writers(ThreePass):
    def test_agy_and_gemini_are_one_writer(self):
        # The fixture routes `gemini`; a review asked for as `agy` finds that route, and the other way round.
        self.assertEqual(self.ok("plan", self.base, writer="agy", record=False)["writer"], "agy")
        swapped = self.commit("agy route", {".conclave/reviewers.toml": PIN.replace('gemini = ["sol"]', 'agy = ["sol"]')})
        (self.repo / ".conclave/reviewers.toml").write_text(PIN.replace('gemini = ["sol"]', 'agy = ["sol"]'),
                                                            encoding="utf-8")
        self.assertEqual(self.ok("plan", swapped, writer="gemini", record=False)["reviewer"], "sol")
        module = self.module()
        both = PIN.replace('gemini = ["sol"]', 'gemini = ["sol"]\nagy = ["sol"]')
        with self.assertRaises(module.Refused) as refused:
            module.load_pin(data=both.encode("utf-8"))
        self.assertIn("names both agy and gemini, which are one writer", str(refused.exception))
        own = PIN.replace('cli = "codex"\nmodel = "gpt-6.1-sol"\neffort = "xhigh"',
                          'cli = "agy"\nmodel = "gemini-4-argon-medium"').replace(
            'claude = ["sol"]\ngemini = ["sol"]', 'agy = ["sol"]')
        with self.assertRaises(module.Refused) as refused:
            module.load_pin(data=own.encode("utf-8"))
        self.assertIn("route 'agy' names sol, of the writer's own family", str(refused.exception))
        # A recorded round by the agy writer counts as a review from another family.
        entry = {"reviewer": "sol", "writer": "agy", "cli": "codex", "review": "x\nOVERALL: approve"}
        self.assertTrue(module.returned_review(entry))


class NoLenses(ThreePass):
    def test_a_pass_with_no_lenses_is_still_asked_for_its_highest_line(self):
        # D00 T01 §4: a completion step without lenses (a pin from before completion had its own) got no appended
        # request, so its reply had no HIGHEST line and was recorded as incomplete.
        earlier = PIN.replace('[schedule.completion]\neffort = "medium"\nlenses = ["plan-delivery", "record"]',
                              '[schedule.completion]\neffort = "medium"')
        self.assertNotEqual(earlier, PIN)
        self.base = self.commit("completion without lenses", {".conclave/reviewers.toml": earlier})
        self.ok("plan", self.base)
        built = self.commit("build", {"src/app/a.py": "x = 1\n"})
        self.ok("round", built)
        summary = self.ok("completion", built, say="Nothing missing.\nHIGHEST: none\nCOMPLETION: complete")
        self.assertEqual(summary["lenses"], [])
        prompt = self.calls()[-1]["stdin"]
        self.assertIn("write one line `HIGHEST: P1`", prompt)
        self.assertNotIn("LENS ", prompt)
        (entry,) = self.findings()["completions"]
        self.assertEqual(entry["reviewer"], "sol")


class Antigravity(ThreePass):
    def test_a_pass_reviewed_through_antigravity_records_the_scheduled_effort(self):
        # Completion review of D00 T01 §3: the record held no effort, so the stamp's replay of the schedule refused it.
        argon = PIN.replace('cli = "codex"\nmodel = "gpt-6.1-sol"\neffort = "xhigh"',
                            'cli = "agy"\nmodel = "gemini-4-argon-medium"').replace('gemini = ["sol"]\n', "")
        self.base = self.commit("antigravity reviewer", {".conclave/reviewers.toml": argon})
        module = self.module()
        schedule = module.load_pin(self.repo / ".conclave/reviewers.toml")["schedule"]
        say = reply("plan", LENSES["plan"])
        result, summary = self.dispatch("--kind", "plan", "--writer", "claude", "--prompt", str(self.prompt),
                                        "--candidate", self.base, "--record", str(self.record), agy="say", say=say)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((summary["cli"], summary["effort"]), ("agy", "medium"))
        # No effort flag reaches agy; the prompt itself (passed inline as -p= on Linux) does mention the effort.
        self.assertFalse([a for a in self.calls()[-1]["argv"] if "effort" in a and not a.startswith("-p=")])
        (entry,) = self.findings()["plans"]
        expected = module.scheduled(schedule, "plan", [], False)
        self.assertEqual((entry["effort"], entry["lenses"]), (expected["effort"], expected["lenses"]))


class Decoding(ThreePass):
    def test_a_reply_is_utf8_or_windows_1252_and_never_raises(self):
        module = self.module()
        self.assertEqual(module.decode_reply("it’s é".encode("utf-8")), "it’s é")
        self.assertEqual(module.decode_reply(b"it\x92s"), "it’s")
        self.assertEqual(module.decode_reply(b"a\x81b"), "a�b")


class ResolutionGuard(ThreePass):
    """D00 T01 §12: reconciliation after a returned gaps pass refuses before invoking the reviewer unless a valid
    resolution entry appears in writer-authored text after that pass."""

    def test_reconciliation_refuses_missing_or_incomplete_resolution_before_calling_reviewer(self):
        built = self.to_completion(top="P2", gaps=True)
        repaired = self.commit("repair the guard", {"src/app/a.py": "x = 2\n"})
        (self.repo / ".git" / "FETCH_HEAD").write_text(f"{repaired}\t\tbranch 'main' of .\n", encoding="utf-8")
        baseline = self.record.read_bytes()
        bad_notes = (
            ("empty", None),
            ("unprefixed prose", f"- Fixed the gap in {repaired[:8]}"),
            ("missing commit SHA", "- Implemented: fixed the gap without a commit"),
            ("nonexistent commit SHA", "- Implemented: fixed the gap in deadbeef"),
            ("bare SHA with no description", f"- Implemented: `{repaired[:8]}`"),
            ("two pasted SHAs with no description", f"- Implemented: {built[:8]} {repaired[:8]}"),
            ("two SHAs separated by HTML br with no description", f"- Implemented: {built[:8]} <br> {repaired[:8]}"),
            ("two SHAs separated by HTML comment containing backticked prose",
             f"- Implemented: {built[:8]} <!-- `hidden` --> {repaired[:8]}"),
            ("two SHAs separated by HTML entity nbsp with no description",
             f"- Implemented: {built[:8]} &nbsp; {repaired[:8]}"),
            ("two SHAs separated by empty-alt Markdown image with no description",
             f"- Implemented: {built[:8]} ![](icon.png) {repaired[:8]}"),
            ("two SHAs separated by linked status badge with no description",
             f'- Implemented: {built[:8]} [![](badge.svg)](build "Build status") {repaired[:8]}'),
            ("two SHAs separated by linked platform badge with bracketed alt text",
             f'- Implemented: {built[:8]} [![CI [linux]](badge.svg)](build "Build status") {repaired[:8]}'),
            ("SHA plus slash-separated backticked SHAs with no description",
             f"- Implemented: {repaired[:8]} `{built[:8]}`/`{repaired[:8]}`"),
            ("SHA plus arrow-separated commit pair with no description",
             f"- Implemented: {repaired[:8]} {built[:8]}\u2192{repaired[:8]}"),
            ("SHA plus git range with no description", f"- Implemented: {repaired[:8]} {built[:8]}..{repaired[:8]}"),
            ("SHA plus branch range with no description", f"- Implemented: {repaired[:8]} {built[:8]}..main"),
            ("SHA plus reflog selector with no description", f"- Implemented: {repaired[:8]} HEAD@{{0}}"),
            ("SHA plus parent-set selector HEAD^@ with no description", f"- Implemented: {repaired[:8]} HEAD^@"),
            ("SHA plus FETCH_HEAD pseudoref with no description", f"- Implemented: {repaired[:8]} FETCH_HEAD"),
            ("SHA plus date reflog selector with spaces and no description",
             f"- Implemented: {repaired[:8]} HEAD@{{1 minute ago}}"),
            ("SHA plus backticked date reflog selector with no description",
             f"- Implemented: {repaired[:8]} `HEAD@{{1 minute ago}}`"),
            ("SHA plus two date reflog selectors with no description",
             f"- Implemented: {repaired[:8]} HEAD@{{1 minute ago}} HEAD@{{2 minutes ago}}"),
            ("SHA plus commit-message search selector with no description",
             f"- Implemented: {repaired[:8]} :/repair"),
            ("SHA plus multiword commit-message search selector with no description",
             f"- Implemented: {repaired[:8]} :/repair the guard"),
            ("SHA plus two multiword commit-message search selectors with no description",
             f"- Implemented: {repaired[:8]} :/repair the guard :/build"),
            ("SHA plus interleaved search selectors and HEAD with no description",
             f"- Implemented: {repaired[:8]} :/repair the guard HEAD :/build"),
            ("SHA plus triple-dot range and HEAD with no description",
             f"- Implemented: {repaired[:8]} {built[:8]}...HEAD"),
            ("SHA plus bare commit URL with no description",
             f"- Implemented: {repaired[:8]} https://example.com/commit/{built[:8]}"),
            ("SHA plus repo-qualified commit ref with no description",
             f"- Implemented: {repaired[:8]} owner/repo@{built[:8]}"),
            ("SHA plus www.github.com commit permalink with no description",
             f"- Implemented: {repaired[:8]} www.github.com/owner/repo/commit/{built}"),
            ("SHA plus ported www commit permalink with no description",
             f"- Implemented: {repaired[:8]} www.git.example.com:8443/project/commit/{built[:8]}"),
            ("SHA plus URL with Unicode punctuation in path and no description",
             f"- Implemented: {repaired[:8]} https://git.example.com/cgit/fix\u2013tools/commit/?id={built[:8]}"),
            ("SHA plus www URL with Unicode punctuation in path and no description",
             f"- Implemented: {repaired[:8]} www.git.example.com:8443/cgit/fix\u2013tools/commit/{built[:8]}"),
            ("SHA plus IDN www commit permalink with no description",
             f"- Implemented: {repaired[:8]} www.m\u00fcnchen.de/project/commit/{built[:8]}"),
            ("SHA plus zero-padded-port www commit permalink with no description",
             f"- Implemented: {repaired[:8]} www.git.example.com:000443/commit/{built[:8]}"),
            ("SHA plus protocol-relative commit URL with no description",
             f"- Implemented: {repaired[:8]} //git.example.com/commit/{built[:8]}"),
            ("SHA plus localhost protocol-relative commit URL with no description",
             f"- Implemented: {repaired[:8]} //localhost/commit/{built[:8]}"),
            ("SHA plus generic commit-labeled link with no description",
             f"- Implemented: {repaired[:8]} [commit](https://example.com/commit/{built[:8]})"),
            ("SHA plus View on GitHub link with no plain-text description outside links",
             f"- Implemented: {repaired[:8]} [View on GitHub](https://github.com/o/r/commit/{built[:8]})"),
            ("SHA plus angle-bracket email autolink with no description",
             f"- Implemented: <owner@example.com> {repaired[:8]}"),
            ("SHA plus mailto URI autolink with no description",
             f"- Implemented: <mailto:owner@example.com> {repaired[:8]}"),
            ("SHA plus bare email autolink with no description",
             f"- Implemented: owner@example.com {repaired[:8]}"),
            ("SHA plus angle-bracket URN autolink with no description",
             f"- Implemented: <urn:isbn:0451450523> {repaired[:8]}"),
            ("multiline wrapped link covering entire description and SHA",
             f"- Implemented: [Corrected parser {repaired[:8]}\n  here](diff)"),
            ("fenced code block quoting Implemented entry refused",
             f"```text\n- Implemented: corrected parser {repaired[:8]}\n```"),
            ("tilde fenced code block quoting Filed entry refused",
             "~~~\n- Filed: D00 T01 §2\n~~~"),
            ("fenced code block quoting Writer note refused",
             "```\n## Writer note: quoted example\n```"),
            ("HTML comment quoting Writer note refused",
             "<!--\n## Writer note: draft only\n-->"),
            ("HTML comment quoting Implemented entry refused",
             f"<!--\n- Implemented: corrected parser {repaired[:8]}\n-->"),
            ("HTML comment quoting Filed entry refused",
             "<!--\n- Filed: D00 T01 §2\n-->"),
            ("empty Writer note followed only by HTML comment refused",
             "## Writer note:\n\n<!-- draft only -->"),
            ("Writer note with comment-only title refused",
             "## Writer note: <!-- draft only -->"),
            ("Writer note with comment-only title and closing ATX hashes refused",
             "## Writer note: <!-- draft only --> ##"),
            ("Writer note with adjacent closed and multiline HTML comments refused",
             "## Writer note:\n<!-- obsolete --> <!--\ndraft only\n-->"),
            ("Writer note with superseded heading inside second adjacent HTML comment refused",
             "## Writer note:\n<!-- obsolete --> <!--\n## Writer note: superseded\n-->"),
            ("Writer note body with only empty HTML span around comment refused",
             "## Writer note:\n<span><!-- draft only --></span>"),
            ("Writer note body with multiline empty HTML span around comment refused",
             '## Writer note:\n<span\n  class="draft"><!-- draft --></span>'),
            ("Writer note body with heading inside multiline HTML attribute refused",
             '## Writer note:\n<div title="draft\n## obsolete\n"></div>'),
            ("Writer note body with raw HTML img widget without prose refused",
             '## Writer note:\n\n<img src="proof.png" alt="Screenshot of the repaired dialog">'),
            ("Writer note body with raw HTML progress indicator without prose refused",
             '## Writer note:\n\n<progress value="8" max="8"></progress>'),
            ("Writer note body with CSS-styled progress indicator without prose refused",
             '## Writer note:\n\n<progress style="display:none; display:block" value="8" max="8"></progress>'),
            ("Writer note body with CSS custom-property progress indicator without prose refused",
             '## Writer note:\n<progress style="--state:none;display:var(--state)"></progress>'),
            ("Writer note body with hidden HTML progress indicator refused",
             '## Writer note:\n<progress hidden value="8" max="8"></progress>'),
            ("Writer note body with hidden ancestor wrapping progress indicator refused",
             '## Writer note:\n<div hidden><progress value="8" max="8"></progress></div>'),
            ("Empty Writer note with closed div cannot borrow following section text",
             "## Writer note:\n<div></div>\n## Other work\nUnrelated text."),
            ("Empty Writer note with unclosed paragraph cannot borrow following section text",
             "## Writer note:\n<p>\n\n## Other work\nUnrelated text."),
            ("Empty Writer note with open div ends at following heading",
             "## Writer note:\n<div>\n## Repair verified\n</div>"),
            ("Writer note body with script block only refused",
             "## Writer note:\n<script>\n## Repair verified\n</script>"),
            ("Writer note body with multiline script opener containing Writer note refused",
             '## Writer note:\n<script\n type="text/javascript">\n## Writer note: draft\n</script>'),
            ("Writer note body with multiline CDATA containing heading refused",
             "## Writer note:\n<![CDATA[\n## Other work\n]]>"),
            ("Writer note body with multiline processing instruction containing heading refused",
             "## Writer note:\n<?draft\n## Other work\n?>"),
            ("Writer note body with image unquoted attribute containing nonbreaking space refused",
             "## Writer note:\n<img alt=Figure\u00a01>"),
            ("Writer note body with style block only refused",
             "## Writer note:\n<style>body { color: red; }</style>"),
            ("Writer note body with whitespace entity only refused",
             "## Writer note:\n\n&nbsp;"),
            ("Writer note body with hidden HTML input refused",
             '## Writer note:\n<input type="hidden" value="draft">'),
            ("paired link-only description refused when prose sits only inside link label",
             f"- Implemented: [Corrected parser](diff) {repaired[:8]}"),
            ("Git rev:path expression with linked hash filename refused",
             f"- Implemented: fixed HEAD:[{repaired[:8]}](diff)"),
            ("Git branch:path expression with linked hash filename refused",
             f"- Implemented: fixed main:[{repaired[:8]}](diff)"),
            ("Git commit:path expression with linked hash filename refused",
             f"- Implemented: fixed {built[:8]}:[{repaired[:8]}](diff)"),
            ("dotfile prefix before linked SHA refused",
             f"- Implemented: fixed .[{repaired[:8]}](diff)"),
            ("Git range operator before linked SHA refused",
             f"- Implemented: fixed {built[:8]}..[{repaired[:8]}](diff)"),
            ("localized filename containing SHA is not a standalone repair SHA",
             f"- Implemented: fixed src/\u4fee\u5fa9{repaired[:8]}"),
            ("unpaired tilde parent shorthand", f"- Implemented: fixed {repaired[:8]}~"),
            ("emphasis must not pair across link label boundary",
             f"- Implemented: fixed src/*.py in [{repaired[:8]}*](diff)"),
            ("token cleanup must not pair asterisk across link boundary",
             f"- Implemented: fixed *[{repaired[:8]}*](diff)"),
            ("CJK prose with malformed asterisk across link boundary",
             f"- Implemented: 修復於*[{repaired[:8]}*](diff)。"),
            ("backticked SHA with trailing tilde parent selector", f"- Implemented: fixed `{repaired[:8]}`~"),
            ("linked SHA with no description",
             f"- Implemented: [{repaired[:8]}](https://example.com/commit/{repaired[:8]})"),
            ("bold linked SHA with no description",
             f"- Implemented: [**{repaired[:8]}**](https://example.com/commit/{repaired[:8]})"),
            ("reference-linked SHA with no description",
             f"- Implemented: [{repaired[:8]}][repair]\n\n[repair]: https://example.com/commit/{repaired[:8]}"),
            ("undefined backticked reference-linked SHA with no description",
             f"- Implemented: [`{repaired[:8]}`][repair]"),
            ("undefined full reference-linked SHA", f"- Implemented: fixed [{repaired[:8]}][missing]"),
            ("undefined collapsed reference-linked SHA", f"- Implemented: fixed [{repaired[:8]}][]"),
            ("undefined shortcut reference-linked SHA", f"- Implemented: fixed [{repaired[:8]}]"),
            ("reference definition inside fenced code block",
             f"- Implemented: fixed [{repaired[:8]}][repair]\n\n```markdown\n"
             f"[repair]: https://example.com/commit/{repaired[:8]}\n```"),
            ("reference definition inside fenced block after blockquoted backticks",
             f"- Implemented: fixed [{repaired[:8]}][repair]\n\n```markdown\n> ```\n"
             f"[repair]: https://example.com/commit/{repaired[:8]}\n```"),
            ("reference definition tab-indented inside list-item fence",
             f"- Implemented: fixed [{repaired[:8]}][repair]\n\n- ```\n\t[repair]: https://example.com/commit/{repaired[:8]}\n  ```"),
            ("reference definition inside second list-item fence after space-plus-tab close",
             f"- Implemented: fixed [{repaired[:8]}][repair]\n\n- ```\n  example\n  \t```\n\n  ```\n"
             f"  [repair]: https://example.com/commit/{repaired[:8]}\n  ```"),
            ("reference definition inside outer fence after tab-marked list item",
             f"- Implemented: fixed [{repaired[:8]}][repair]\n\n-\t~~~\n  ~~~\n\n"
             f"[repair]: https://example.com/commit/{repaired[:8]}"),
            ("reference definition indented as code inside list item",
             f"- Implemented: fixed [{repaired[:8]}][repair]\n\n-     [repair]: https://example.com/commit/{repaired[:8]}"),
            ("reference definition on four-space paragraph continuation in list item",
             f"- Implemented: fixed [{repaired[:8]}][repair]\n\n- example\n"
             f"    [repair]: https://example.com/commit/{repaired[:8]}"),
            ("reference definition inside fenced block after blank line in list item",
             f"- Implemented: fixed [{repaired[:8]}][repair]\n\n- example\n\n    ~~~\n"
             f"  [repair]: https://example.com/commit/{repaired[:8]}\n  ~~~"),
            ("reference definition after indented thematic break inside list-item fence",
             f"- Implemented: fixed [{repaired[:8]}][repair]\n\n- ~~~\n  ---\n"
             f"  [repair]: https://example.com/commit/{repaired[:8]}\n  ~~~"),
            ("reference definition after blank line inside nested-list fence",
             f"- Implemented: fixed [{repaired[:8]}][repair]\n\n- - ~~~\n\n"
             f"    [repair]: https://example.com/commit/{repaired[:8]}\n    ~~~"),
            ("reference definition indented as code after spaced thematic break",
             f"- Implemented: fixed [{repaired[:8]}][repair]\n\n* * *\n\n"
             f"    [repair]: https://example.com/commit/{repaired[:8]}"),
            ("reference definition inside multiline HTML comment",
             f"- Implemented: fixed [{repaired[:8]}][repair]\n\n<!--\n"
             f"[repair]: https://example.com/commit/{repaired[:8]}\n-->"),
            ("reference definition after thematic break inside HTML div block",
             f"- Implemented: fixed [{repaired[:8]}][repair]\n\n<div>\n---\n"
             f"[repair]: https://example.com/commit/{repaired[:8]}\n</div>"),
            ("reference definition after nonbreaking-space line inside HTML div block",
             f"- Implemented: fixed [{repaired[:8]}][repair]\n\n<div>\n\u00a0\n"
             f"[repair]: https://example.com/commit/{repaired[:8]}\n</div>"),
            ("malformed angle-bracket reference definition",
             f"- Implemented: fixed [{repaired[:8]}][repair]\n\n[repair]: <unterminated"),
            ("reference destination with backslash-space escape",
             f"- Implemented: fixed [{repaired[:8]}][repair]\n\n[repair]: /commit\\ withspace"),
            ("multiline title interrupted by zero-padded ordered list start 01",
             f'- Implemented: fixed [{repaired[:8]}][repair]\n\n[repair]: /commit "example\n01. item"'),
            ("blank line before reference destination",
             f"- Implemented: fixed [{repaired[:8]}][repair]\n\n[repair]:\n\n  https://example.com/commit/{repaired[:8]}"),
            ("thematic break after destinationless reference label",
             f"- Implemented: fixed [{repaired[:8]}][repair]\n\n- [repair]:\n---"),
            ("titled linked SHA with no description",
             f'- Implemented: [{repaired[:8]}](https://example.com/commit/{repaired[:8]} "repair")'),
            ("parenthesized titled linked SHA with no description",
             f"- Implemented: [{repaired[:8]}](https://example.com/commit/{repaired[:8]} (repair))"),
            ("malformed identifier suffix", f"- Implemented: {repaired[:8]}oops"),
            ("SHA split across visible space inside code span",
             f"- Implemented: fixed {repaired[:3]}` {repaired[3:8]}`"),
            ("SHA split across inline HTML img element",
             f'- Implemented: fixed {repaired[:3]}<img src="icon.png">{repaired[3:8]}'),
            ("identifier joined to SHA across inline HTML comment",
             f"- Implemented: fixed build<!-- split -->{repaired[:8]}"),
            ("SHA split across numeric control character entity",
             f"- Implemented: fixed {repaired[:3]}&#11;{repaired[3:8]}"),
            ("bare commit URL", f"- Implemented: https://example.com/commit/{repaired[:8]}"),
            ("root-relative path keyed by two SHAs", f"- Implemented: fixed /{built[:8]}/{repaired[:8]}"),
            ("bare Filed without ref", "- Filed: later"),
            ("Filed with double space", "- Filed:  D00 T01 §2 (extra space mismatches stamp)"),
            ("empty Writer note", "## Writer note:"),
        )
        for label, note in bad_notes:
            with self.subTest(case=label):
                self.record.write_bytes(baseline)
                if note is not None:
                    self.resolve(repaired, note)
                before_bytes = self.record.read_bytes()
                res = self.refused("reconciliation", repaired, "has no resolution entry")
                self.assertIn("plain-text description outside links", res.stderr)
                self.assertIn("## Writer note: <plain-text reason>", res.stderr)
                self.assertEqual(self.record.read_bytes(), before_bytes, "refusal must leave findings file unchanged")
        if subprocess.run(["git", "rev-parse", "--verify", "--quiet", "fetch_head^{commit}"],
                          cwd=self.repo, capture_output=True).returncode == 0:
            self.record.write_bytes(baseline)
            self.resolve(repaired, f"- Implemented: {repaired[:8]} fetch_head")
            before_bytes = self.record.read_bytes()
            self.refused("reconciliation", repaired, "has no resolution entry")
            self.assertEqual(self.record.read_bytes(), before_bytes, "refusal must leave findings file unchanged")
        short4 = self.git("rev-parse", "--short=4", repaired)
        good_impl_cases = (
            ("backticked short SHA", f"- Implemented: fixed the gap in `{repaired[:8]}`"),
            ("SHA inside inline code command",
             f"- Implemented: verified `git show {repaired[:8]} --stat`"),
            ("SHA last inside inline code command",
             f"- Implemented: verified `git show {repaired[:8]}`"),
            ("prose with literal opening brace before SHA",
             f"- Implemented: handled literal `{{` in {repaired[:8]}"),
            ("prose with literal @{{ and }} code spans around SHA",
             f"- Implemented: documented `@{{` in {repaired[:8]} and its closing `}}`"),
            ("Markdown-linked SHA",
             f"- Implemented: repaired the guard in [{repaired[:8]}](https://example.com/commit/{repaired[:8]})"),
            ("bold Markdown-linked SHA",
             f"- Implemented: fixed [**{repaired[:8]}**](https://example.com/commit/{repaired[:8]})"),
            ("bold multi-word linked repair description ending in SHA",
             f"- Implemented: repaired [**fixed in {repaired[:8]}**](https://example.com/commit/{repaired[:8]})"),
            ("paired plain-text description accepted when prose sits outside link label",
             f"- Implemented: Corrected parser {repaired[:8]}"),
            ("linked SHA immediately after sentence-ending period",
             f"- Implemented: Corrected parser.[{repaired[:8]}](diff)"),
            ("documented SKILL.md implemented format",
             f"- Implemented: repaired gap 1 in {repaired[:8]}"),
            ("multiline wrapped implemented item with plain-text description and linked SHA",
             f"- Implemented: fixed the bug in\n  [{repaired[:8]}](diff)"),
            ("linked SHA followed by sentence-ending exclamation mark",
             f"- Implemented: Fixed the crash in [{repaired[:8]}](diff)!"),
            ("full reference-linked SHA",
             f"- Implemented: fixed [{repaired[:8]}][repair]\n\n[repair]: https://example.com/commit/{repaired[:8]}"),
            ("reference-linked SHA with escaped closing bracket in label",
             f"- Implemented: fixed [{repaired[:8]}][ticket\\]]\n\n"
             f"[ticket\\]]: https://example.com/commit/{repaired[:8]}"),
            ("reference-linked SHA with wrapped multiline label definition",
             f"- Implemented: fixed [{repaired[:8]}][release notes]\n\n"
             f"[release\n notes]: https://example.com/commit/{repaired[:8]}"),
            ("escaped exclamation mark before linked SHA",
             f"- Implemented: fixed \\![{repaired[:8]}](diff)"),
            ("linked repair description with bracketed platform label",
             f"- Implemented: fixed [fix [Windows] in {repaired[:8]}](diff)"),
            ("linked SHA with image syntax in literal destination filename",
             f"- Implemented: fixed [{repaired[:8]}](docs/![](icon).md)"),
            ("blockquoted reference-linked SHA",
             f"- Implemented: fixed [{repaired[:8]}][repair]\n\n> [repair]: https://example.com/commit/{repaired[:8]}"),
            ("list-item reference-linked SHA",
             f"- Implemented: fixed [{repaired[:8]}][repair]\n\n- [repair]: https://example.com/commit/{repaired[:8]}"),
            ("unindented reference definition after unclosed list-item fence",
             f"- Implemented: fixed [{repaired[:8]}][repair]\n\n- ```\n  example\n\n"
             f"[repair]: https://example.com/commit/{repaired[:8]}"),
            ("reference definition after list-item fence closed by tab-indented fence",
             f"- Implemented: fixed [{repaired[:8]}][repair]\n\n- ```\n  example\n\t```\n\n"
             f"[repair]: https://example.com/commit/{repaired[:8]}"),
            ("reference definition in list item after space-plus-tab closing fence",
             f"- Implemented: fixed [{repaired[:8]}][repair]\n\n- ```\n  example\n  \t```\n\n"
             f"  [repair]: https://example.com/commit/{repaired[:8]}"),
            ("reference definition outside tab-marked list-item fence",
             f"- Implemented: fixed [{repaired[:8]}][repair]\n\n-\t~~~\n"
             f"  [repair]: https://example.com/commit/{repaired[:8]}"),
            ("reference definition indented in list item after blank line",
             f"- Implemented: fixed [{repaired[:8]}][repair]\n\n- example\n\n"
             f"    [repair]: https://example.com/commit/{repaired[:8]}"),
            ("reference definition immediately below thematic break",
             f"- Implemented: fixed [{repaired[:8]}][repair]\n\n---\n"
             f"[repair]: https://example.com/commit/{repaired[:8]}"),
            ("reference definition immediately below single-line HTML pre block",
             f"- Implemented: fixed [{repaired[:8]}][repair]\n\n<pre>example</pre>\n"
             f"[repair]: https://example.com/commit/{repaired[:8]}"),
            ("reference definition in top-level blockquote after list-quoted HTML block",
             f"- Implemented: fixed [{repaired[:8]}][repair]\n\n- > <div>\n"
             f"> [repair]: https://example.com/commit/{repaired[:8]}"),
            ("reference definition indented in nested list item after blank line",
             f"- Implemented: fixed [{repaired[:8]}][repair]\n\n- - example\n\n"
             f"      [repair]: https://example.com/commit/{repaired[:8]}"),
            ("multiline reference definition inside nested list blockquote",
             f"- Implemented: fixed [{repaired[:8]}][repair]\n\n- - > [repair]:\n"
             f"    > https://example.com/commit/{repaired[:8]}"),
            ("multiline reference-linked SHA",
             f"- Implemented: fixed [{repaired[:8]}][repair]\n\n[repair]:\n  https://example.com/commit/{repaired[:8]}"),
            ("multiline quoted title reference-linked SHA",
             f'- Implemented: fixed [{repaired[:8]}][repair]\n\n[repair]: https://example.com/commit/{repaired[:8]} '
             f'"Repair of\nthe guard"'),
            ("multiline quoted title with numbered second line starting at 2",
             f'- Implemented: fixed [{repaired[:8]}][repair]\n\n[repair]: https://example.com/commit/{repaired[:8]} '
             f'"example\n2. item"'),
            ("SHA contiguous across inline HTML comment",
             f"- Implemented: fixed {repaired[:3]}<!-- comment -->{repaired[3:8]}"),
            ("collapsed reference-linked SHA",
             f"- Implemented: fixed [{repaired[:8]}][]\n\n[{repaired[:8]}]: https://example.com/commit/{repaired[:8]}"),
            ("shortcut reference-linked SHA",
             f"- Implemented: fixed [{repaired[:8]}]\n\n[{repaired[:8]}]: https://example.com/commit/{repaired[:8]}"),
            ("double-quoted titled Markdown-linked SHA",
             f'- Implemented: fixed [{repaired[:8]}](https://example.com/commit/{repaired[:8]} "repair")'),
            ("single-quoted titled Markdown-linked SHA",
             f"- Implemented: fixed [{repaired[:8]}](https://example.com/commit/{repaired[:8]} 'repair')"),
            ("parenthesized titled Markdown-linked SHA",
             f"- Implemented: fixed [{repaired[:8]}](https://example.com/commit/{repaired[:8]} (repair))"),
            ("angle-bracket destination Markdown-linked SHA",
             f"- Implemented: fixed [{repaired[:8]}](<https://example.com/commit/{repaired[:8]}> (repair))"),
            ("Markdown-linked backticked SHA",
             f"- Implemented: repaired the guard in [`{repaired[:8]}`](https://example.com/commit/{repaired[:8]})."),
            ("comma-separated Markdown-linked backticked SHAs",
             f"- Implemented: fixed [`{built[:8]}`](https://example.com/commit/{built[:8]}),"
             f"[`{repaired[:8]}`](https://example.com/commit/{repaired[:8]})"),
            ("short=4 SHA", f"- Implemented: fixed in {short4}"),
            ("uppercase SHA", f"- Implemented: fixed in {repaired[:8].upper()}"),
            ("non-ASCII Unicode description", f"- Implemented: 修復 {repaired[:8]}"),
            ("linked SHA followed by CJK full stop", f"- Implemented: 修復 [{repaired[:8]}](diff)。"),
            ("linked SHA directly adjacent to preceding CJK prose", f"- Implemented: 修復於[{repaired[:8]}](diff)。"),
            ("linked SHA directly adjacent to mixed CJK and ASCII prose", f"- Implemented: 修復API[{repaired[:8]}](diff)。"),
            ("linked backticked SHA directly adjacent to mixed prose", f"- Implemented: 修復API[`{repaired[:8]}`](diff)。"),
            ("linked SHA directly adjacent to Thai word ending in combining tone mark",
             f"- Implemented: \u0e41\u0e01\u0e49[{repaired[:8]}](diff)"),
            ("linked SHA directly adjacent to decomposed-accent prose",
             f"- Implemented: corrig\u0065\u0301[{repaired[:8]}](diff)"),
            ("linked SHA directly after prose colon",
             f"- Implemented: fixed:[{repaired[:8]}](diff)"),
            ("linked SHA directly adjacent to following CJK prose", f"- Implemented: [{repaired[:8]}](diff)已修復。"),
        )
        for label, note in good_impl_cases:
            with self.subTest(good_impl=label):
                self.record.write_bytes(baseline)
                self.resolve(repaired, note)
                before_calls = len(self.calls())
                self.ok("reconciliation", repaired)
                self.assertEqual(len(self.calls()), before_calls + 1)

    def test_empty_writer_note_cannot_borrow_body_across_retry_and_git_lookups_deduplicate(self):
        self.to_completion(top="P2", gaps=True)
        repaired = self.commit("repair", {"src/app/a.py": "x = 2\n"})
        self.resolve(repaired, "## Writer note:")
        module = self.module()
        module.record_completion(
            self.record,
            {"writer": "claude", "reviewer": None, "failover": [{"reviewer": "sol", "reason": "timed out"}],
             "review": None, "effort": "medium", "lenses": LENSES["reconciliation"]},
            "2026-11-22T00:00:00Z", repaired, "reconciliation",
        )
        self.resolve(repaired, "ordinary retry log")
        before_bytes = self.record.read_bytes()
        self.refused("reconciliation", repaired, "has no resolution entry")
        self.assertEqual(self.record.read_bytes(), before_bytes)

        calls = []
        real_git = module.git

        def counting_git(*args):
            calls.append(args)
            return real_git(*args)

        with unittest.mock.patch.object(module, "ROOT", self.repo), unittest.mock.patch.object(module, "git", counting_git):
            line = "- Implemented: fixed " + " ".join(["deadbeef"] * 12)
            self.assertFalse(module._has_resolution_entry([line]))
        self.assertEqual(len(calls), 1, f"expected 1 deduplicated git lookup, got {len(calls)}")
        # Linear-time reference definition collection (no quadratic rescan across adjacent definitions).
        many = [f"[r{k}]: /commit/{k}" for k in range(2000)]
        t0 = time.monotonic()
        defs = module._collect_ref_defs(many)
        elapsed = time.monotonic() - t0
        self.assertEqual(len(defs), 2000)
        self.assertLess(elapsed, 0.25, f"_collect_ref_defs took {elapsed:.3f}s on 2000 consecutive definitions")
        # Iterative reference check must not hit RecursionError on a long reference list.
        with unittest.mock.patch.object(module, "ROOT", self.repo):
            long_refs = f"- Implemented: {repaired[:8]} " + " ".join(["HEAD"] * 1050)
            self.assertFalse(module._has_resolution_entry([long_refs]))
            nested_qual = f"- Implemented: {repaired[:8]} " + ("a/b@" * 600) + "HEAD"
            self.assertIsInstance(module._has_resolution_entry([nested_qual]), bool)
        # Linear-time bracket scanning on deeply nested literal brackets (fails quadratic copy regression).
        small_brackets = "[" * 16000 + "x" + "]" * 16000
        t0 = time.monotonic()
        self.assertEqual(module._unwrap_markdown_links(small_brackets, set()), small_brackets)
        t_small = time.monotonic() - t0
        nested_brackets = "[" * 128000 + "x" + "]" * 128000
        t0 = time.monotonic()
        self.assertEqual(module._unwrap_markdown_links(nested_brackets, set()), nested_brackets)
        t_large = time.monotonic() - t0
        self.assertLess(t_large, 0.45, f"_unwrap_markdown_links took {t_large:.3f}s on 128k nested brackets")
        self.assertLess(
            t_large,
            max(t_small * 16, 0.25),
            f"_unwrap_markdown_links scaled super-linearly: 16k={t_small:.4f}s, 128k={t_large:.4f}s",
        )
        # Linear-time www. prefix detection on repeated embedded "www." tokens (fails quadratic slice regression).
        www_repeat = "x" + ("www." * 16000)
        t0 = time.monotonic()
        self.assertEqual(module._split_plain_revspecs(www_repeat), [www_repeat])
        t_www = time.monotonic() - t0
        self.assertLess(t_www, 0.10, f"_split_plain_revspecs took {t_www:.3f}s on 16k repeated www. tokens")
        # Linear-time opener deactivation when unmatched "[" or "![" precede repeated links.
        for prefix in ("[", "!["):
            many_links = (prefix * 8000) + ("[x](d)" * 8000)
            t0 = time.monotonic()
            self.assertEqual(module._unwrap_markdown_links(many_links, set()), (prefix * 8000) + ("x" * 8000))
            t_links = time.monotonic() - t0
            self.assertLess(t_links, 0.25, f"_unwrap_markdown_links took {t_links:.3f}s on 8k {prefix}+links")

    def test_reconciliation_accepts_filed_and_writer_note_including_stamp_exemption(self):
        cases = (
            ("filed out-of-scope", "- Filed: D00 T01 §2 (out-of-scope gap)"),
            ("writer note titled", "## Writer note: contested grade\n\n- Finding 1 has no real Path:."),
            ("writer note body for stamp checkbox rule",
             "## Writer note:\n\n- Finding only reports pending stamp checkbox, exempt per "
             "docs/reconciliation-review-prompt.md:26."),
            ("documented SKILL.md copyable stamp-only writer note",
             "## Writer note: open Commit item and Implementation Order row are ticked by stamp per "
             "docs/reconciliation-review-prompt.md"),
            ("writer note body with visible prose after inline HTML comment",
             "## Writer note:\n\n<!-- internal --> The pending stamp accounts for this gap."),
            ("writer note body with prose between short HTML comment and trailing comment",
             "## Writer note:\n<!-->Repair verified<!-- later -->"),
            ("writer note body with prose after multiline script containing heading",
             '## Writer note:\n<script\n type="text/javascript">\n## internal\n</script>\nRepair verified'),
            ("writer note body with nonbreaking space after tag name stays literal text",
             "## Writer note:\n<div\u00a0hidden>"),
            ("writer note body with plain-text prose inside CSS-styled span",
             '## Writer note:\n\n<span style="display:none">Repair verified</span>'),
            ("writer note body with indented code block showing literal img tag",
             '## Writer note:\n\n    <img src="proof.png">'),
            ("writer note body with fenced code block showing literal img tag",
             '## Writer note:\n\n```\n<img src="proof.png">\n```'),
            ("writer note title with inline code span showing literal img tag",
             '## Writer note: `<img src="proof.png">`'),
            ("writer note title with literal underscore placeholder",
             "## Writer note: <repair_commit>"),
            ("writer note title with literal hashes directly after colon",
             "## Writer note:###"),
        )
        for label, note in cases:
            with self.subTest(case=label):
                self.tearDown()
                self.setUp()
                self.to_completion(top="P2", gaps=True)
                repaired = self.commit("repair", {"src/app/a.py": "x = 2\n"})
                self.resolve(repaired, note)
                before_calls = len(self.calls())
                self.ok("reconciliation", repaired)
                self.assertEqual(len(self.calls()), before_calls + 1)

    @staticmethod
    def _skill_candidates(scripts_dir):
        candidates = [scripts_dir.parent / ".claude/skills/review-todo-section/SKILL.md"]
        if len(scripts_dir.parents) > 2:
            candidates.append(scripts_dir.parents[2] / "todo/templates/.claude/skills/review-todo-section/SKILL.md")
        return candidates

    def test_documented_skill_examples_are_read_from_skill_and_accepted(self):
        # Installing directly under a drive root (e.g. D:/project/scripts) has only 2 parents and must not crash.
        shallow = Path("D:/project/scripts") if os.name == "nt" else Path("/project/scripts")
        self.assertEqual(len(self._skill_candidates(shallow)), 1)
        skill_path = next((p for p in self._skill_candidates(SCRIPTS_DIR) if p.is_file()), None)
        if skill_path is None:
            self.skipTest("review-todo-section/SKILL.md not present in standalone reviewer-only install")
        text = skill_path.read_text(encoding="utf-8")
        m_note = re.search(r"\(`(## Writer note: [^`]+)`\)", text)
        m_impl = re.search(r"\(`(- Implemented: [^`]+)`\)", text)
        self.assertIsNotNone(m_note, f"missing copyable ## Writer note: example in {skill_path}")
        self.assertIsNotNone(m_impl, f"missing copyable - Implemented: example in {skill_path}")
        for label, template in (("skill Writer note example", m_note.group(1)),
                                ("skill Implemented example", m_impl.group(1))):
            with self.subTest(case=label):
                self.tearDown()
                self.setUp()
                self.to_completion(top="P2", gaps=True)
                repaired = self.commit("repair", {"src/app/a.py": "x = 2\n"})
                entry = template.replace("<gap>", "gap 1").replace("<commit>", repaired[:8])
                self.resolve(repaired, entry)
                before_calls = len(self.calls())
                self.ok("reconciliation", repaired)
                self.assertEqual(len(self.calls()), before_calls + 1)

    def test_markers_in_review_body_or_earlier_pass_do_not_satisfy_current_pass(self):
        self.ok("plan", self.base)
        built = self.commit("build", {"src/app/a.py": "x = 1\n"})
        self.ok("round", built)
        repaired = self.commit("repair 1", {"src/app/a.py": "x = 2\n"})
        quoted_reply = (
            f"1. P2 record: quote `- Implemented: fixed in {repaired[:8]}` and `- Filed: D00 T01 §2`.\n"
            f"- Implemented: fixed in {repaired[:8]}\n"
            f"- Filed: D00 T01 §2 (quoted inside review body)\n"
            f"## Writer note: quoted inside review body\n"
            + "\n".join(f"LENS {n}: findings" for n in LENSES["completion"])
            + "\nHIGHEST: P2\nCOMPLETION: gaps"
        )
        self.ok("completion", built, say=quoted_reply)
        before_bytes = self.record.read_bytes()
        self.refused("reconciliation", repaired, "has no resolution entry")
        self.assertEqual(self.record.read_bytes(), before_bytes)
        self.resolve(repaired, f"- Implemented: fixed gap 1 in {repaired[:8]}")
        self.ok("reconciliation", repaired, gaps=True, top="P2")
        repaired2 = self.commit("repair 2", {"src/app/a.py": "x = 3\n"})
        # Completion's resolution entry must not satisfy Reconciliation 1's gaps.
        before_bytes = self.record.read_bytes()
        self.refused("reconciliation", repaired2, "has no resolution entry")
        self.assertEqual(self.record.read_bytes(), before_bytes)
        self.resolve(repaired2, f"- Implemented: fixed gap 2 in {repaired2[:8]}")
        before_calls = len(self.calls())
        self.ok("reconciliation", repaired2)
        self.assertEqual(len(self.calls()), before_calls + 1)

    def test_document_level_ref_def_from_earlier_pass_resolves_in_later_reconciliation(self):
        self.to_completion(top="P2", gaps=True)
        repaired = self.commit("repair1", {"src/app/a.py": "x = 2\n"})
        self.resolve(repaired, f"- Implemented: fixed gap 1 in {repaired[:8]}\n\n[repair]: /diff")
        self.ok("reconciliation", repaired, top="P2", gaps=True)
        repaired2 = self.commit("repair2", {"src/app/a.py": "x = 3\n"})
        self.resolve(repaired2, f"- Implemented: [repair] {repaired2[:8]}")
        before_bytes = self.record.read_bytes()
        res = self.refused("reconciliation", repaired2, "has no resolution entry")
        self.assertIn("plain-text description outside links", res.stderr)
        self.assertEqual(self.record.read_bytes(), before_bytes)

    def test_retry_after_no_review_reconciliation_keeps_resolution_and_accepts_post_attempt_note(self):
        self.to_completion(top="P2", gaps=True)
        repaired = self.commit("repair", {"src/app/a.py": "x = 2\n"})
        self.resolve(repaired, f"- Implemented: fixed gap in {repaired[:8]}")
        res, _ = self.run_pass("reconciliation", repaired, say="no verdict lines")
        self.assertEqual(res.returncode, 1)
        # Retry still sees the resolution written before the failed no-review attempt.
        before_calls = len(self.calls())
        self.ok("reconciliation", repaired)
        self.assertEqual(len(self.calls()), before_calls + 1)

        # And if a historical no-review attempt existed without prior notes, notes added after it are also accepted.
        self.tearDown()
        self.setUp()
        self.to_completion(top="P2", gaps=True)
        repaired = self.commit("repair", {"src/app/a.py": "x = 2\n"})
        module = self.module()
        module.record_completion(
            self.record,
            {"writer": "claude", "reviewer": None, "failover": [{"reviewer": "sol", "reason": "timed out"}],
             "review": None, "effort": "medium", "lenses": LENSES["reconciliation"]},
            "2026-11-22T00:00:00Z", repaired, "reconciliation",
        )
        self.refused("reconciliation", repaired, "has no resolution entry")
        self.resolve(repaired, f"- Implemented: fixed gap in {repaired[:8]}")
        before_calls = len(self.calls())
        self.ok("reconciliation", repaired)
        self.assertEqual(len(self.calls()), before_calls + 1)


if __name__ == "__main__":
    unittest.main()
