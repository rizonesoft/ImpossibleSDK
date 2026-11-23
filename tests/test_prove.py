"""A proof is of a commit."""
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import prove


class Committed(unittest.TestCase):
    def test_uncommitted_work_is_refused(self):
        clean = {k: v for k, v in os.environ.items() if not k.startswith("GIT_")}
        with mock.patch.dict(os.environ, clean, clear=True), tempfile.TemporaryDirectory() as folder:
            def git(*arguments):
                subprocess.run(["git", "-C", folder, "-c", "user.name=T", "-c", "user.email=t@example.invalid", "-c", "core.hooksPath=", *arguments],
                               check=True, capture_output=True, timeout=60)
            git("init", "-q")
            Path(folder, "file").write_text("one")
            self.assertRaisesRegex(SystemExit, "uncommitted", prove.committed, folder)
            git("add", "-A")
            git("commit", "-qm", "one")
            self.assertRegex(prove.committed(folder), r"^[0-9a-f]{7,}$")
            Path(folder, "file").write_text("half")
            self.assertRaisesRegex(SystemExit, "uncommitted", prove.committed, folder)


if __name__ == "__main__":
    unittest.main()
