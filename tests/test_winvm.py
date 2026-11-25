"""The live pass: when one is owed, what not now keeps, and that only a real pass is recorded (tools/winvm/live.py)."""
import json
import os
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/winvm"))
import live

SETTINGS = {"name": "machine", "path": "D:\\machine", "avoid": ["other"]}


class LivePass(unittest.TestCase):
    def setUp(self):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.root = Path(folder.name) / "repo"
        (self.root / "reference").mkdir(parents=True)
        (self.root / "tests").mkdir()
        (self.root / "reference/App.xaml.cs").write_text("one")
        (self.root / "tests/test_live.py").write_text("one")
        (self.root / "other.txt").write_text("one")
        subprocess.run(["git", "init", "-q", str(self.root)], check=True, timeout=60)
        self.work = Path(folder.name) / "work"
        self.work.mkdir()
        patch = mock.patch.object(live, "there", str)
        patch.start()
        self.addCleanup(patch.stop)
        self.commands = []

    def machine(self, code, result):
        def run(command, out):
            self.commands.append(command)
            out.mkdir()
            return code, result
        return run

    def attempt(self, code, result, now=1000.0, packed=None):
        return live.attempt(self.root, SETTINGS, machine=self.machine(code, result), now=lambda: now,
                            packer=lambda root: (self.work, packed or live.fingerprint(root)))

    def test_a_pass_settles_what_it_packed_and_a_change_owes_another(self):
        self.assertTrue(live.owed(self.root))
        self.assertEqual(self.attempt(0, {"outcome": "ran", "exit": 0, "seconds": 9}), 0)
        self.assertFalse(live.owed(self.root))
        (self.root / "other.txt").write_text("two")
        self.assertFalse(live.owed(self.root))
        (self.root / "reference/new.json").write_text("{}")
        self.assertTrue(live.owed(self.root))

    def test_a_tree_that_moved_during_the_pass_is_still_owed(self):
        before = live.fingerprint(self.root)
        (self.root / "tests/test_live.py").write_text("two")
        self.assertEqual(self.attempt(0, {"outcome": "ran", "exit": 0}, packed=before), 0)
        self.assertTrue(live.owed(self.root))

    def test_not_now_keeps_the_pass_owed(self):
        for code, result in ((75, {}), (75, {"outcome": "not-now", "reason": "other is Running"}), (4, {})):
            self.assertEqual(self.attempt(code, result), 75)
            self.assertFalse((self.root / live.PASS).exists())
        self.assertEqual(live.attempt(self.root, {}, machine=None, packer=None), 75)

    def test_nothing_but_a_run_that_exits_0_is_a_pass(self):
        for code, result in ((0, {}), (3, {}), (1, {"outcome": "ran", "exit": 1}), (0, {"outcome": "ran"}),
                             (75, {"outcome": "ran", "exit": 75}), (4, {"outcome": "ran", "exit": 4}), (0, {"outcome": "ran", "exit": 1})):
            self.assertEqual(self.attempt(code, result), 1, (code, result))
            self.assertTrue(live.owed(self.root))

    def test_the_memory_target_holds_for_30_minutes_from_the_first_try(self):
        def floor():
            command = self.commands[-1]
            return command[command.index("--min-avail-mb") + 1]
        with mock.patch.object(live.shutil, "which", return_value="bosun"):
            for now, want in ((1000, "5120"), (2799, "5120"), (2800, "0"), (9000, "0")):
                self.attempt(75, {}, now=now)
                self.assertEqual(floor(), want, now)
            (self.root / "reference/App.xaml.cs").write_text("two")  # a new debt, a new half hour
            self.attempt(75, {}, now=9001)
            self.assertEqual(floor(), "5120")

    def test_the_turn_is_taken_in_bosun_where_it_is_and_directly_where_it_is_not(self):
        with mock.patch.object(live.shutil, "which", return_value="bosun"):
            self.attempt(75, {})
        with mock.patch.object(live.shutil, "which", return_value=None):
            self.attempt(75, {})
        queued, direct = self.commands
        self.assertEqual(queued[:6], ["bosun", "run", "--ns", "Bosun.VM", "--slots", "1"])
        self.assertIn("--or-skip", queued)
        self.assertEqual(direct[0], "powershell.exe")
        for command in self.commands:
            self.assertEqual(command[command.index("-Avoid") + 1], "other")
        self.assertLess(live.HARD_SECONDS, 300)
        self.assertLess(live.TURN_SECONDS, 360)

    def test_a_new_run_never_reads_an_old_result(self):
        def silent(command, out):
            return live.run_machine([sys.executable, "-c", "pass"], out)
        code = live.attempt(self.root, SETTINGS, machine=silent, packer=lambda root: (self.work, "x"))
        self.assertEqual(code, 1)

    def test_everything_that_decides_the_verdict_owes_a_pass(self):
        for name in (
            "tests/reference/cases.json",
            "tests/frames.py",
            "tests/layout_cases.py",
            "tests/fixtures/layout_reference.json",
            "tools/winui/check_package.py",
            "tools/winvm/needs.json",
            "ui/clip.c",
            "ui/draw.c",
            "ui/layer.c",
            "ui/template_data.c",
        ):
            self.attempt(0, {"outcome": "ran", "exit": 0})
            self.assertFalse(live.owed(self.root))
            (self.root / name).parent.mkdir(parents=True, exist_ok=True)
            (self.root / name).write_text("new")
            self.assertTrue(live.owed(self.root), name)

    @unittest.skipUnless(live.fcntl, "a pass runs from WSL")
    def test_one_attempt_of_a_checkout_at_a_time(self):
        with live.alone(self.root) as first, live.alone(self.root) as second:
            self.assertTrue(first)
            self.assertFalse(second)
        with live.alone(self.root) as again:
            self.assertTrue(again)

    def test_a_damaged_record_owes_a_pass(self):
        (self.root / live.PASS).write_text("{")
        self.assertTrue(live.owed(self.root))

    def test_live_job_differences_catches_extra_captured_case(self):
        sys.path.insert(0, str(ROOT / "reference"))
        import live_job, zlib
        with tempfile.TemporaryDirectory() as tmp:
            fresh = Path(tmp) / "fresh"
            store = Path(tmp) / "store"
            fresh.mkdir()
            store.mkdir()
            (store / "cases.json").write_text(json.dumps({"cases": []}), encoding="utf-8")
            (fresh / "cases.json").write_text(json.dumps({"cases": [{"id": "new-unstored-case"}]}), encoding="utf-8")
            found = live_job.differences(fresh, store)
            self.assertIn("new-unstored-case: not in store", found)

            # Accent frames: accept different guest accent color in the same silhouette, reject a blank frame.
            w, h = 8, 6
            bg = bytes([32, 32, 32, 255])
            def make_box(color):
                buf = bytearray(bg * (w * h))
                for y in range(1, 5):
                    for x in range(2, 6):
                        buf[(y * w + x) * 4 : (y * w + x) * 4 + 4] = color
                return bytes(buf)
            want_px = make_box(bytes([200, 100, 50, 255]))
            alt_color_px = make_box(bytes([50, 120, 220, 255]))
            blank_px = bg * (w * h)
            case = {"id": "accent-Normal-Dark", "control": "accent", "width": w, "height": h, "file": "acc.bgra"}
            (store / "cases.json").write_text(json.dumps({"cases": [case]}), encoding="utf-8")
            (store / "acc.bgra.z").write_bytes(zlib.compress(want_px))
            (fresh / "cases.json").write_text(json.dumps({"cases": [case]}), encoding="utf-8")
            (fresh / "acc.bgra").write_bytes(alt_color_px)
            self.assertEqual(live_job.differences(fresh, store), [])
            (fresh / "acc.bgra").write_bytes(blank_px)
            blank_found = live_job.differences(fresh, store)
            self.assertTrue(any("accent frame is blank" in m for m in blank_found), blank_found)
            # Zero-alpha accent frame must be rejected.
            zero_alpha = bytearray(want_px)
            for k in range(3, len(zero_alpha), 4):
                zero_alpha[k] = 0
            (fresh / "acc.bgra").write_bytes(bytes(zero_alpha))
            alpha_found = live_job.differences(fresh, store)
            self.assertTrue(any("accent alpha differs" in m for m in alpha_found), alpha_found)
            # An ordinary control whose ID starts with "accent-" must NOT receive the accent exception.
            non_accent = {"id": "accent-label", "control": "Button", "case_group": "button", "width": w, "height": h, "file": "acc.bgra"}
            (store / "cases.json").write_text(json.dumps({"cases": [non_accent]}), encoding="utf-8")
            (fresh / "cases.json").write_text(json.dumps({"cases": [non_accent]}), encoding="utf-8")
            (fresh / "acc.bgra").write_bytes(alt_color_px)
            non_acc_found = live_job.differences(fresh, store)
            self.assertTrue(any("pixels differ" in m for m in non_acc_found), non_acc_found)

    @unittest.skipUnless(os.name != "nt", "POSIX process-group teardown test")
    def test_timeout_kills_whole_process_tree(self):
        with tempfile.TemporaryDirectory() as tmp:
            pid_file = Path(tmp) / "child.pid"
            script = (
                "import subprocess, sys, time; "
                "p = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(60)']); "
                f"open({str(pid_file)!r}, 'w').write(str(p.pid)); "
                "time.sleep(60)"
            )
            with self.assertRaises(subprocess.TimeoutExpired):
                live._run_with_tree_kill([sys.executable, "-c", script], cwd=tmp, timeout=1.0, check=True)
            if pid_file.is_file():
                child_pid = int(pid_file.read_text().strip())
                deadline = time.monotonic() + 2.0
                gone = False
                while time.monotonic() < deadline:
                    stat_path = Path(f"/proc/{child_pid}/stat")
                    if stat_path.parent.is_dir():
                        try:
                            stat = stat_path.read_text(encoding="utf-8")
                            rparen = stat.rfind(")")
                            if rparen != -1 and stat[rparen + 2 : rparen + 3] == "Z":
                                gone = True
                                break
                        except FileNotFoundError:
                            gone = True
                            break
                    try:
                        os.kill(child_pid, 0)
                    except ProcessLookupError:
                        gone = True
                        break
                    time.sleep(0.02)
                self.assertTrue(gone, f"grandchild pid {child_pid} still alive after process-group kill")


if __name__ == "__main__":
    unittest.main()
