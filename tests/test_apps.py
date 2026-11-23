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

    def sdk_used(self):
        return app_build.sdk_at(app_build.pinned(self.program), self.sdk, Path(self.temp.name, "cache"))

    def test_the_checkout_itself_is_used_at_the_pinned_commit(self):
        self.assertEqual(self.sdk_used(), self.sdk)

    def test_the_pinned_commit_is_used_after_the_sdk_moves_on_or_is_edited(self):
        (self.sdk / "file").write_text("two")
        self.assertEqual((self.sdk_used() / "file").read_text(), "one")
        git(self.sdk, "commit", "-qam", "two")
        self.assertEqual((self.sdk_used() / "file").read_text(), "one")

    def test_a_copy_that_was_changed_is_refused(self):
        (self.sdk / "file").write_text("two")
        for name in ("file", "extra"):
            copy = self.sdk_used()
            (copy / name).write_text("changed")
            self.assertRaisesRegex(SystemExit, "is not the SDK at that commit", self.sdk_used)
            (copy / name).write_text("one") if name == "file" else None

    def test_an_unknown_commit_a_short_or_a_missing_pin_is_refused(self):
        self.pin("0" * 40)
        self.assertRaisesRegex(SystemExit, "does not have the pinned commit", self.sdk_used)
        self.pin("abc")
        self.assertRaisesRegex(SystemExit, "not a full hash", self.sdk_used)
        (self.program / "sdk.json").unlink()
        self.assertRaisesRegex(SystemExit, "does not name a commit", self.sdk_used)


if __name__ == "__main__":
    unittest.main()
