"""A program in its own folder is built only with the SDK it pins."""
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import app_build


def git(folder, *arguments):
    identity = ["-c", "user.name=Test", "-c", "user.email=test@example.invalid", "-c", "commit.gpgsign=false"]
    return subprocess.run(["git", "-C", str(folder), *identity, *arguments], capture_output=True, text=True, check=True).stdout.strip()


class Pin(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.sdk, self.program = Path(self.temp.name, "sdk"), Path(self.temp.name, "program")
        self.sdk.mkdir(), self.program.mkdir()
        git(self.sdk, "init", "-q")
        git(self.sdk, "config", "core.hooksPath", os.devnull)
        (self.sdk / "file").write_text("one")
        git(self.sdk, "add", "file")
        git(self.sdk, "commit", "-qm", "one")
        self.pin(git(self.sdk, "rev-parse", "HEAD"))

    def pin(self, commit):
        (self.program / "sdk.json").write_text(json.dumps({"commit": commit}))

    def problem(self):
        return app_build.sdk_problem(self.program, self.sdk)

    def test_the_pinned_commit_is_accepted(self):
        self.assertIsNone(self.problem())

    def test_another_commit_is_refused(self):
        self.pin("0" * 40)
        self.assertIn("pins Impossible SDK 000000000000", self.problem())

    def test_local_changes_are_refused(self):
        (self.sdk / "file").write_text("two")
        self.assertIn("has local changes", self.problem())

    def test_a_short_or_missing_pin_is_refused(self):
        self.pin("abc")
        self.assertIn("not a full hash", self.problem())
        (self.program / "sdk.json").unlink()
        self.assertIn("does not name a commit", self.problem())


if __name__ == "__main__":
    unittest.main()
