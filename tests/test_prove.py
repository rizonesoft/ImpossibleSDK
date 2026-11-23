"""A proof is of a commit, and never of a copy that someone edited."""
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import prove


@unittest.skipIf(os.name == "nt", "runs in WSL or Linux")
class Send(unittest.TestCase):
    def setUp(self):
        clean = {k: v for k, v in os.environ.items() if not k.startswith("GIT_")}
        patch = mock.patch.dict(os.environ, clean, clear=True)
        patch.start()
        self.addCleanup(patch.stop)
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.work, self.copy = Path(temp.name, "work"), Path(temp.name, "copy")
        self.git(Path(temp.name), "init", "-q", "-b", "main", str(self.copy))
        self.git(self.copy, "config", "receive.denyCurrentBranch", "updateInstead")
        self.commit(self.copy, "one")
        self.git(Path(temp.name), "clone", "-q", "-o", "proof", str(self.copy), str(self.work))

    def git(self, folder, *arguments):
        subprocess.run(["git", "-C", str(folder), "-c", "user.name=T", "-c", "user.email=t@example.invalid", *arguments],
                       check=True, capture_output=True, timeout=60)

    def commit(self, folder, text):
        (folder / "file").write_text(text)
        self.git(folder, "add", "-A")
        self.git(folder, "-c", "core.hooksPath=", "commit", "-qm", text)

    def test_the_copy_gets_the_commit(self):
        self.commit(self.work, "two")
        self.assertEqual(prove.send(self.work), self.copy)
        self.assertEqual((self.copy / "file").read_text(), "two")

    def test_uncommitted_work_is_refused(self):
        (self.work / "file").write_text("half")
        self.assertRaisesRegex(SystemExit, "uncommitted", prove.send, self.work)

    def test_an_edited_copy_is_refused_and_keeps_its_edit(self):
        (self.copy / "file").write_text("edited")
        self.commit(self.work, "two")
        self.assertRaises(SystemExit, prove.send, self.work)
        self.assertEqual((self.copy / "file").read_text(), "edited")

    def test_a_commit_made_in_the_copy_is_refused(self):
        self.commit(self.copy, "theirs")
        self.commit(self.work, "ours")
        self.assertRaises(SystemExit, prove.send, self.work)
        self.assertEqual((self.copy / "file").read_text(), "theirs")


if __name__ == "__main__":
    unittest.main()
